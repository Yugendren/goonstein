#include "battle.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PARRY_WINDOW   0.14f
#define PARRY_WIDE     0.24f
#define PERFECT_WINDOW 0.06f
#define HAND_SIZE      5
#define ENERGY_BASE    3
#define ENERGY_CAP     6
#define COUNTER_DAMAGE 4

// ---------------------------------------------------------------- data

static char *next_line(char **cur) {
    if (!*cur || !**cur) return NULL;
    char *line = *cur, *nl = strchr(line, '\n');
    if (nl) { *nl = 0; *cur = nl + 1; } else *cur = line + strlen(line);
    char *hash = strchr(line, '#'); if (hash) *hash = 0;
    return line;
}
// Tokenise with quoted strings kept whole.
static int tokenise(char *line, char *tok[], int max) {
    int n = 0; char *p = line;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '"') { p++; tok[n++] = p; while (*p && *p != '"') p++; if (*p) *p++ = 0; }
        else { tok[n++] = p; while (*p && *p != ' ' && *p != '\t') p++; if (*p) *p++ = 0; }
    }
    return n;
}

static int find_card(const Battle *b, const char *name) {
    for (int i = 0; i < b->ncards; i++) if (!strcmp(b->cards[i].name, name)) return i;
    return -1;
}

static bool load_cards(Battle *b, const char *path) {
    size_t n; char *text = SDL_LoadFile(path, &n);
    if (!text) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "cards missing: %s", path); return false; }
    char *cur = text, *line; int ln = 0;
    while ((line = next_line(&cur))) {
        ln++;
        char *tok[48]; int nt = tokenise(line, tok, 48);
        if (nt == 0) continue;
        if (strcmp(tok[0], "card") != 0 || nt < 2) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d expected card", path, ln); continue; }
        if (b->ncards >= CARDS_MAX) break;
        CardDef *c = &b->cards[b->ncards++];
        memset(c, 0, sizeof *c);
        snprintf(c->name, sizeof c->name, "%s", tok[1]);
        c->hits = 1; c->cost = 1; c->anim = ANIM_ATTACK; c->color = v3(0.8f, 0.8f, 0.8f);
        for (int i = 2; i + 1 <= nt - 1 || (i < nt && !strcmp(tok[i], "needs")); i += 2) {
            const char *k = tok[i], *v = i + 1 < nt ? tok[i + 1] : "";
            if (!strcmp(k, "cost")) c->cost = atoi(v);
            else if (!strcmp(k, "kind")) c->kind = !strcmp(v, "attack") ? CK_ATTACK : !strcmp(v, "guard") ? CK_GUARD : !strcmp(v, "draw") ? CK_DRAW : !strcmp(v, "heal") ? CK_HEAL : CK_BUFF;
            else if (!strcmp(k, "damage")) c->damage = atoi(v);
            else if (!strcmp(k, "hits")) c->hits = atoi(v);
            else if (!strcmp(k, "block")) c->block = atoi(v);
            else if (!strcmp(k, "draw")) c->draw = atoi(v);
            else if (!strcmp(k, "heal")) c->heal = atoi(v);
            else if (!strcmp(k, "effect")) snprintf(c->effect, sizeof c->effect, "%s", v);
            else if (!strcmp(k, "needs")) { if (!strcmp(v, "parried")) c->needs_parried = true; }
            else if (!strcmp(k, "anim")) c->anim = anim_from_name(v);
            else if (!strcmp(k, "desc")) snprintf(c->desc, sizeof c->desc, "%s", v);
            else if (!strcmp(k, "color")) { c->color.x = (float)atof(v); c->color.y = (float)atof(tok[i + 2]); c->color.z = (float)atof(tok[i + 3]); i += 2; }
            else SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d unknown card key %s", path, ln, k);
        }
    }
    SDL_free(text);
    return b->ncards > 0;
}

static bool load_deck(Battle *b, const char *path) {
    size_t n; char *text = SDL_LoadFile(path, &n);
    if (!text) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "deck missing: %s", path); return false; }
    char *cur = text, *line; b->ndraw = 0;
    while ((line = next_line(&cur))) {
        char *tok[4]; int nt = tokenise(line, tok, 4);
        if (nt < 1) continue;
        int id = find_card(b, tok[0]);
        if (id < 0) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "deck: unknown card %s", tok[0]); continue; }
        int count = nt > 1 ? atoi(tok[1]) : 1;
        for (int i = 0; i < count && b->ndraw < DECK_MAX; i++) b->draw_pile[b->ndraw++] = id;
    }
    SDL_free(text);
    return b->ndraw > 0;
}

static bool load_enemy(EnemyDef *e, const char *path) {
    size_t n; char *text = SDL_LoadFile(path, &n);
    if (!text) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "enemy missing: %s", path); return false; }
    memset(e, 0, sizeof *e); e->hp = 80; strcpy(e->name, "Enemy");
    char *cur = text, *line; int ln = 0;
    while ((line = next_line(&cur))) {
        ln++;
        char *tok[48]; int nt = tokenise(line, tok, 48);
        if (nt == 0) continue;
        if (!strcmp(tok[0], "name")) { char buf[64] = ""; for (int i = 1; i < nt; i++) { if (i > 1) strncat(buf, " ", sizeof buf - strlen(buf) - 1); strncat(buf, tok[i], sizeof buf - strlen(buf) - 1); } snprintf(e->name, sizeof e->name, "%s", buf); }
        else if (!strcmp(tok[0], "hp")) e->hp = atoi(tok[1]);
        else if (!strcmp(tok[0], "attack")) {
            if (e->nattacks >= ATTACKS_MAX || nt < 2) continue;
            EnemyAttack *a = &e->attacks[e->nattacks++];
            memset(a, 0, sizeof *a);
            snprintf(a->name, sizeof a->name, "%s", tok[1]);
            a->hits = 1; a->parryable = true; a->tell = v3(1, 0.5f, 0.2f); a->lead = 1.0f; a->tail = 0.6f; a->contact[0] = 0.4f;
            for (int i = 2; i < nt; i++) {
                const char *k = tok[i];
                if (!strcmp(k, "clip") && i + 1 < nt) snprintf(a->clip, sizeof a->clip, "%s", tok[++i]);
                else if (!strcmp(k, "damage") && i + 1 < nt) a->damage = atoi(tok[++i]);
                else if (!strcmp(k, "hits") && i + 1 < nt) a->hits = atoi(tok[++i]);
                else if (!strcmp(k, "parry") && i + 1 < nt) a->parryable = !strcmp(tok[++i], "yes");
                else if (!strcmp(k, "lead") && i + 1 < nt) a->lead = (float)atof(tok[++i]);
                else if (!strcmp(k, "tail") && i + 1 < nt) a->tail = (float)atof(tok[++i]);
                else if (!strcmp(k, "tell") && i + 3 < nt) { a->tell = v3((float)atof(tok[i + 1]), (float)atof(tok[i + 2]), (float)atof(tok[i + 3])); i += 3; }
                else if (!strcmp(k, "contact")) { int h = 0; while (i + 1 < nt && h < HITS_MAX && (tok[i + 1][0] == '0' || tok[i + 1][0] == '1' || tok[i + 1][0] == '.')) a->contact[h++] = (float)atof(tok[++i]); }
                else SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d unknown attack key %s", path, ln, k);
            }
            if (a->hits > HITS_MAX) a->hits = HITS_MAX;
        }
        else if (!strcmp(tok[0], "pattern")) {
            for (int i = 1; i < nt && e->npattern < PATTERN_MAX; i++) {
                int found = -1;
                for (int j = 0; j < e->nattacks; j++) if (!strcmp(e->attacks[j].name, tok[i])) found = j;
                if (found >= 0) e->pattern[e->npattern++] = found;
                else SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d pattern names unknown attack %s", path, ln, tok[i]);
            }
        }
    }
    SDL_free(text);
    if (e->npattern == 0) for (int i = 0; i < e->nattacks && i < PATTERN_MAX; i++) e->pattern[e->npattern++] = i;
    return e->nattacks > 0;
}

bool battle_load(Battle *b, const char *cards_path, const char *deck_path, const char *enemy_path) {
    memset(b, 0, sizeof *b);
    if (!load_cards(b, cards_path)) return false;
    if (!load_deck(b, deck_path)) return false;
    if (!load_enemy(&b->enemy, enemy_path)) return false;
    return true;
}

// ---------------------------------------------------------------- piles

static void shuffle(int *a, int n) { for (int i = n - 1; i > 0; i--) { int j = rand() % (i + 1); int t = a[i]; a[i] = a[j]; a[j] = t; } }

static void draw_card(Battle *b) {
    if (b->nhand >= HAND_MAX) return;
    if (b->ndraw == 0) {
        if (b->ndiscard == 0) return;
        memcpy(b->draw_pile, b->discard, (size_t)b->ndiscard * sizeof(int));
        b->ndraw = b->ndiscard; b->ndiscard = 0;
        shuffle(b->draw_pile, b->ndraw);
    }
    HandCard *h = &b->hand[b->nhand++];
    memset(h, 0, sizeof *h);
    h->def = b->draw_pile[--b->ndraw];
}

static void discard_hand(Battle *b) {
    for (int i = 0; i < b->nhand; i++) if (b->ndiscard < DECK_MAX) b->discard[b->ndiscard++] = b->hand[i].def;
    b->nhand = 0;
}

static void remove_from_hand(Battle *b, int i) {
    if (b->ndiscard < DECK_MAX) b->discard[b->ndiscard++] = b->hand[i].def;
    for (int k = i; k < b->nhand - 1; k++) b->hand[k] = b->hand[k + 1];
    b->nhand--;
}

// ---------------------------------------------------------------- start

static Vec3 fwd_of(float yaw) { return v3(sinf(yaw), 0, cosf(yaw)); }
static Vec3 right_of(float yaw) { Vec3 f = fwd_of(yaw); return v3(-f.z, 0, f.x); }

void battle_start(Battle *b, Vec3 centre, float stage_yaw, float spacing, int player_hp, int player_hp_max) {
    Vec3 f = fwd_of(stage_yaw);
    b->stage_yaw = stage_yaw;
    b->player_pos = v3_sub(centre, v3_scale(f, spacing * 0.5f));
    b->enemy_pos = v3_add(centre, v3_scale(f, spacing * 0.5f));
    b->player_hp = player_hp; b->player_hp_max = player_hp_max;
    b->enemy_hp = b->enemy_hp_max = b->enemy.hp;
    // rebuild the draw pile from everything and shuffle
    for (int i = 0; i < b->ndiscard; i++) if (b->ndraw < DECK_MAX) b->draw_pile[b->ndraw++] = b->discard[i];
    for (int i = 0; i < b->nhand; i++) if (b->ndraw < DECK_MAX) b->draw_pile[b->ndraw++] = b->hand[i].def;
    b->ndiscard = 0; b->nhand = 0;
    shuffle(b->draw_pile, b->ndraw);
    b->state = BT_INTRO; b->t = 0; b->round = 0;
    b->energy = 0; b->energy_max = ENERGY_BASE; b->banked = 0; b->guard = 0;
    b->pattern_i = 0; b->timescale = 1; b->hitstop = 0; b->hovered = -1; b->playing_card = -1;
    b->parries = b->perfects = b->hits_taken = 0;
    // opening shot: wide from the player's side
    Vec3 c = centre; Vec3 r = right_of(stage_yaw);
    b->shot_eye = v3_add(v3_add(v3_add(c, v3_scale(r, -10.0f)), v3_scale(f, -4.0f)), v3(0, 3.6f, 0));
    b->shot_target = v3_add(c, v3(0, 1.2f, 0)); b->shot_fov = 38;
    b->cam_eye = b->shot_eye; b->cam_target = b->shot_target; b->cam_fov = b->shot_fov;
}

// ---------------------------------------------------------------- helpers for presentation

static Vec3 chest(Vec3 pos, float h) { return v3(pos.x, pos.y + h, pos.z); }

static void shot_base(Battle *b) {
    Vec3 c = v3_lerp(b->player_pos, b->enemy_pos, 0.5f), r = right_of(b->stage_yaw), f = fwd_of(b->stage_yaw);
    // A stage view: from the side, a little toward the player, low enough to see the sky line.
    b->shot_eye = v3_add(v3_add(v3_add(c, v3_scale(r, -8.5f)), v3_scale(f, -2.5f)), v3(0, 2.9f, 0));
    b->shot_target = v3_add(c, v3(0, 1.25f, 0)); b->shot_fov = 36;
}
static void shot_player_attack(Battle *b) {
    Vec3 r = right_of(b->stage_yaw), f = fwd_of(b->stage_yaw);
    b->shot_eye = v3_add(v3_add(v3_sub(b->player_pos, v3_scale(f, 6.5f)), v3_scale(r, 4.8f)), v3(0, 2.5f, 0));
    b->shot_target = chest(v3_lerp(b->player_pos, b->enemy_pos, 0.62f), 1.2f); b->shot_fov = 38;
}
static void shot_enemy_attack(Battle *b) {
    Vec3 r = right_of(b->stage_yaw), f = fwd_of(b->stage_yaw);
    b->shot_eye = v3_add(v3_add(v3_sub(b->player_pos, v3_scale(f, 3.8f)), v3_scale(r, -3.0f)), v3(0, 2.0f, 0));
    b->shot_target = chest(v3_lerp(b->player_pos, b->enemy_pos, 0.55f), 1.4f); b->shot_fov = 44;
}
static void shot_win(Battle *b) {
    Vec3 r = right_of(b->stage_yaw), f = fwd_of(b->stage_yaw);
    float a = b->t * 0.25f;
    Vec3 off = v3_add(v3_scale(r, cosf(a) * 5.0f), v3_scale(f, sinf(a) * 5.0f));
    b->shot_eye = v3_add(v3_add(b->enemy_pos, off), v3(0, 2.2f, 0)); b->shot_target = chest(b->enemy_pos, 0.6f); b->shot_fov = 40;
}

static void spawn_sparks(Particles *ps, Vec3 at, Vec3 col, int n, float speed) { particles_burst(ps, PT_SPARK, at, v3(0, 0.6f, 0), n, speed, col, 0.07f, 0.5f); }

static void set_read(Battle *b, const char *s) { snprintf(b->last_read, sizeof b->last_read, "%s", s); b->last_read_t = 1.0f; }

// Game time at which hit i of the current enemy attack lands, from the clip fitting.
static float hit_time(const Battle *b, const CharModel *bm, int i) {
    const EnemyAttack *a = &b->enemy.attacks[b->cur_attack];
    int clip = bm->loaded ? model_find_clip(&bm->model, a->clip) : -1;
    if (clip < 0 || i == 0) return a->lead + i * 0.25f;
    float len = bm->model.clips[clip].duration;
    float c0 = a->contact[0] * len, ci = a->contact[i] * len;
    float rate2 = (len - c0) / fmaxf(a->tail, 0.05f);
    return a->lead + (ci - c0) / rate2;
}

static void play_bound(CharModel *cm, Anim a, float lead, float tail, float fade) {
    if (!cm->loaded) return;
    const AnimBinding *bd = &cm->bind[a];
    if (bd->clip < 0) { bd = &cm->bind[ANIM_IDLE]; if (bd->clip < 0) return; }
    const AnimClip *clip = &cm->model.clips[bd->clip];
    if (bd->contact >= 0 && (lead > 0 || tail > 0)) anim_play_fitted(&cm->player, &cm->model, bd->clip, bd->contact * clip->duration, lead, tail, fade);
    else if (!bd->loop && tail > 0) anim_play_fitted(&cm->player, &cm->model, bd->clip, 0, 0, lead + tail, fade);
    else anim_play(&cm->player, &cm->model, bd->clip, bd->rate, bd->loop, bd->hold || !bd->loop, fade);
}

// ---------------------------------------------------------------- turn logic

static void begin_player_turn(Battle *b, Uifx *fx) {
    b->round++;
    discard_hand(b);
    for (int i = 0; i < HAND_SIZE; i++) draw_card(b);
    b->energy = ENERGY_BASE + b->banked; if (b->energy > ENERGY_CAP) b->energy = ENERGY_CAP;
    b->energy_max = b->energy; b->banked = 0;
    b->guard = 0; b->parried_this_round = false; b->wide_windows = false;
    b->state = BT_PLAYER; b->t = 0; b->hovered = -1;
    uifx_spawn(fx, UIFX_BANNER, 0, 300, "YOUR TURN", v4(0.95f, 0.85f, 0.55f, 1), 3.0f, 1.3f);
    audio_play(SND_BLIP, 0.5f, 1.2f);
}

static void begin_enemy_turn(Battle *b, CharModel *bm, Boss *boss, Uifx *fx) {
    b->cur_attack = b->enemy.pattern[b->pattern_i % b->enemy.npattern];
    b->pattern_i++;
    b->state = BT_ENEMY_TELL; b->t = 0; b->hit_i = 0;
    for (int i = 0; i < HITS_MAX; i++) b->hit_done[i] = false;
    b->parry_pressed_t = -10; b->dodging = false;
    const EnemyAttack *a = &b->enemy.attacks[b->cur_attack];
    char banner[96]; snprintf(banner, sizeof banner, "%s", b->enemy.name);
    for (char *p = banner; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
    uifx_spawn(fx, UIFX_BANNER, 0, 300, banner, v4(a->tell.x, a->tell.y, a->tell.z, 1), 3.0f, 1.1f);
    boss->c.tell_color = a->tell; boss->c.tell = 0;
    for (int i = 0; i < a->hits; i++) b->hit_t[i] = hit_time(b, bm, i);
    if (bm->loaded) {
        int clip = model_find_clip(&bm->model, a->clip);
        if (clip >= 0) anim_play_fitted(&bm->player, &bm->model, clip, a->contact[0] * bm->model.clips[clip].duration, a->lead + 0.9f, a->tail, 0.15f);
        else play_bound(bm, ANIM_ATTACK, a->lead + 0.9f, a->tail, 0.15f);
    }
}

static void enemy_hit_lands(Battle *b, int i, Player *player, CharModel *pm, CharModel *bm, Particles *ps, Uifx *fx, CombatEvents *ev, Mat4 vp) {
    (void)bm;
    const EnemyAttack *a = &b->enemy.attacks[b->cur_attack];
    float th = b->hit_t[i];
    float window = b->wide_windows ? PARRY_WIDE : PARRY_WINDOW;
    Vec3 contact = v3_add(chest(b->player_pos, 1.1f), v3_scale(fwd_of(b->stage_yaw), 0.7f));
    float px, py; bool on = uifx_project(vp, chest(b->player_pos, 2.2f), &px, &py);
    if (!on) { px = 640; py = 300; }
    bool pressed = fabsf(b->parry_pressed_t - th) <= window;
    if (b->dodging && a->parryable == false) {
        uifx_spawn(fx, UIFX_BLOCK, px, py, "DODGED", v4(0.7f, 0.8f, 1, 1), 2.2f, 0.7f);
        set_read(b, "dodged"); audio_play(SND_WHIFF, 0.6f, 1.1f);
        return;
    }
    if (b->dodging) {
        uifx_spawn(fx, UIFX_BLOCK, px, py, "DODGED", v4(0.7f, 0.8f, 1, 1), 2.2f, 0.7f);
        audio_play(SND_WHIFF, 0.6f, 1.1f);
        return;
    }
    if (pressed && a->parryable) {
        bool perfect = fabsf(b->parry_pressed_t - th) <= PERFECT_WINDOW;
        b->parries++; b->parried_this_round = true;
        b->banked += 1; if (perfect) { b->perfects++; b->banked += 1; }
        if (b->banked > ENERGY_CAP - ENERGY_BASE) b->banked = ENERGY_CAP - ENERGY_BASE;
        uifx_spawn(fx, UIFX_PARRY, px, py - 10, perfect ? "PERFECT" : "PARRY", perfect ? v4(1, 0.95f, 0.5f, 1) : v4(0.95f, 0.85f, 0.55f, 1), perfect ? 4.0f : 3.2f, 0.8f);
        spawn_sparks(ps, contact, v3(3.0f, 2.4f, 1.2f), perfect ? 60 : 32, perfect ? 9.0f : 6.0f);
        play_bound(pm, ANIM_PARRY_HIT, 0.04f, 0.5f, 0.0f);
        ev->parried = true; ev->contact = contact;
        ev->shake = fmaxf(ev->shake, perfect ? 0.5f : 0.3f);
        b->hitstop = perfect ? 0.14f : 0.09f;
        if (perfect) b->timescale = 0.3f;
        audio_play(SND_PARRY, 1.0f, perfect ? 1.15f : 1.0f);
        set_read(b, perfect ? "perfect" : "parry");
        if (perfect) {
            // Counter: a quick riposte for a little damage
            b->enemy_hp -= COUNTER_DAMAGE; if (b->enemy_hp < 0) b->enemy_hp = 0;
            float ex, ey; if (uifx_project(vp, chest(b->enemy_pos, 2.6f), &ex, &ey)) { char s[16]; snprintf(s, sizeof s, "%d", COUNTER_DAMAGE); uifx_spawn(fx, UIFX_DAMAGE, ex, ey, s, v4(1, 0.9f, 0.6f, 1), 2.6f, 0.9f); }
        }
        return;
    }
    // Hit lands
    int dmg = a->damage;
    if (b->guard > 0) { int absorbed = dmg < b->guard ? dmg : b->guard; dmg -= absorbed; b->guard -= absorbed;
        if (absorbed > 0) { char s[24]; snprintf(s, sizeof s, "GUARD %d", absorbed); uifx_spawn(fx, UIFX_BLOCK, px - 40, py + 30, s, v4(0.6f, 0.7f, 0.95f, 1), 2.0f, 0.8f); } }
    b->player_hp -= dmg; if (b->player_hp < 0) b->player_hp = 0;
    b->hits_taken++;
    char s[16]; snprintf(s, sizeof s, "%d", dmg);
    uifx_spawn(fx, UIFX_DAMAGE, px, py, s, v4(1, 0.35f, 0.3f, 1), 3.4f, 0.9f);
    if (!a->parryable && b->parry_pressed_t > th - 1.0f) uifx_spawn(fx, UIFX_FAIL, px, py - 40, "UNBLOCKABLE", v4(0.9f, 0.3f, 0.9f, 1), 2.4f, 0.9f);
    else if (a->parryable && b->parry_pressed_t > -5 && b->parry_pressed_t < th) uifx_spawn(fx, UIFX_FAIL, px, py - 40, "EARLY", v4(1, 0.4f, 0.3f, 1), 2.4f, 0.9f);
    else if (a->parryable && b->parry_pressed_t > th) uifx_spawn(fx, UIFX_FAIL, px, py - 40, "LATE", v4(1, 0.4f, 0.3f, 1), 2.4f, 0.9f);
    spawn_sparks(ps, contact, v3(2.5f, 0.3f, 0.2f), 18, 4.0f);
    play_bound(pm, ANIM_HURT, 0, 0.45f, 0.0f);
    player->c.flash = 1.0f;
    ev->player_hit = true; ev->contact = contact; ev->shake = fmaxf(ev->shake, 0.5f);
    b->hitstop = 0.06f;
    audio_play(SND_HURT, 0.9f, 1.0f);
    if (b->player_hp <= 0) { b->state = BT_LOSE; b->t = 0; play_bound(pm, ANIM_DEAD, 0, 0, 0.1f); audio_play(SND_DEATH, 1, 1); }
}

static void card_hit_lands(Battle *b, const CardDef *c, Boss *boss, CharModel *bm, Particles *ps, Uifx *fx, CombatEvents *ev, Mat4 vp) {
    Vec3 contact = v3_add(chest(b->enemy_pos, 1.3f), v3_scale(fwd_of(b->stage_yaw), -0.8f));
    b->enemy_hp -= c->damage; if (b->enemy_hp < 0) b->enemy_hp = 0;
    float ex, ey; if (!uifx_project(vp, chest(b->enemy_pos, 2.7f), &ex, &ey)) { ex = 640; ey = 200; }
    char s[16]; snprintf(s, sizeof s, "%d", c->damage);
    uifx_spawn(fx, UIFX_DAMAGE, ex + (float)(rand() % 40 - 20), ey, s, v4(1, 0.85f, 0.5f, 1), c->damage >= 12 ? 4.2f : 3.2f, 0.9f);
    spawn_sparks(ps, contact, v3(2.2f, 1.8f, 1.0f), c->damage >= 12 ? 30 : 14, 5.0f);
    boss->c.flash = 1.0f;
    play_bound(bm, ANIM_HURT, 0, 0.4f, 0.0f);
    ev->boss_hit = true; ev->contact = contact; ev->shake = fmaxf(ev->shake, c->damage >= 12 ? 0.45f : 0.2f);
    b->hitstop = c->damage >= 12 ? 0.09f : 0.04f;
    audio_play(SND_HIT, 0.9f, c->damage >= 12 ? 0.8f : 1.0f);
}

static bool can_play(const Battle *b, int hand_i) {
    const CardDef *c = &b->cards[b->hand[hand_i].def];
    if (c->cost > b->energy) return false;
    if (c->needs_parried && !b->parried_this_round) return false;
    return true;
}

// Card layout in UI pixels: returns the rect of hand card i
static void card_rect(const Battle *b, int i, float *x, float *y, float *w, float *h) {
    const float CW = 150, CH = 210, GAP = 14;
    float total = b->nhand * CW + (b->nhand - 1) * GAP;
    float x0 = 640 - total * 0.5f;
    *w = CW; *h = CH;
    *x = x0 + i * (CW + GAP);
    *y = 800 - CH - 18 - b->hand[i].lift * 34.0f;
}
static bool inside(float px, float py, float x, float y, float w, float h) { return px >= x && px <= x + w && py >= y && py <= y + h; }
#define END_X 1090.0f
#define END_Y 560.0f
#define END_W 160.0f
#define END_H 52.0f

static void play_card(Battle *b, int hand_i, Player *player, CharModel *pm, Particles *ps, Uifx *fx) {
    (void)player;
    const CardDef *c = &b->cards[b->hand[hand_i].def];
    b->energy -= c->cost;
    b->playing_card = b->hand[hand_i].def;
    b->hand[hand_i].flying = true; b->hand[hand_i].fly_t = 0;
    b->state = BT_CARD; b->t = 0; b->card_hit_i = 0;
    for (int i = 0; i < HITS_MAX; i++) b->card_hit_done[i] = false;
    audio_play(SND_BLIP, 0.6f, 0.9f);
    switch (c->kind) {
    case CK_ATTACK: play_bound(pm, c->anim, 0.38f, 0.45f + (c->hits - 1) * 0.22f, 0.06f); audio_play(SND_SWING, 0.6f, 1.1f); break;
    case CK_GUARD: b->guard += c->block; play_bound(pm, ANIM_PARRY, 0.08f, 0.5f, 0.05f);
        { char s[24]; snprintf(s, sizeof s, "GUARD %d", b->guard); uifx_spawn(fx, UIFX_BLOCK, 640, 420, s, v4(0.6f, 0.75f, 1, 1), 2.6f, 0.9f); }
        particles_burst(ps, PT_SPORE, chest(b->player_pos, 1.0f), v3(0, 1, 0), 30, 1.5f, v3(0.8f, 1.2f, 2.5f), 0.05f, 1.2f);
        break;
    case CK_DRAW: for (int i = 0; i < c->draw; i++) draw_card(b); play_bound(pm, ANIM_IDLE, 0, 0, 0.1f); break;
    case CK_HEAL: b->player_hp += c->heal; if (b->player_hp > b->player_hp_max) b->player_hp = b->player_hp_max;
        { char s[16]; snprintf(s, sizeof s, "+%d", c->heal); uifx_spawn(fx, UIFX_HEAL, 640, 400, s, v4(0.6f, 1, 0.6f, 1), 3.0f, 1.0f); }
        particles_burst(ps, PT_SPORE, chest(b->player_pos, 0.8f), v3(0, 1, 0), 50, 1.2f, v3(1.2f, 2.8f, 1.0f), 0.06f, 1.6f);
        audio_play(SND_HEART, 0.6f, 1.4f);
        break;
    case CK_BUFF: if (!strcmp(c->effect, "window")) { b->wide_windows = true; uifx_spawn(fx, UIFX_LABEL, 640, 420, "WIDE WINDOWS", v4(0.7f, 1, 0.7f, 1), 2.4f, 1.0f); } break;
    }
}

// ---------------------------------------------------------------- tick

void battle_tick(Battle *b, const Input *in, float mx, float my, float dt_real,
                 Player *player, Boss *boss, CharModel *pm, CharModel *bm, Camera *cam,
                 Particles *ps, Uifx *fx, CombatEvents *ev) {
    // Time: hitstop freezes the fight (not the UI), slow motion scales it.
    b->timescale = damp(b->timescale, 1.0f, 5.0f, dt_real);
    float dt = dt_real * b->timescale;
    if (b->hitstop > 0) { b->hitstop -= dt_real; dt = 0; }
    b->t += dt;
    if (b->last_read_t > 0) b->last_read_t -= dt_real;
    b->intent_pulse += dt_real;

    // Characters stand on their marks facing each other
    player->c.pos = b->player_pos; player->c.yaw = b->stage_yaw;
    boss->c.pos = b->enemy_pos; boss->c.yaw = b->stage_yaw + PI;
    if (player->c.flash > 0) player->c.flash = fmaxf(0, player->c.flash - dt_real * 6);
    if (boss->c.flash > 0) boss->c.flash = fmaxf(0, boss->c.flash - dt_real * 5);
    Mat4 vp = camera_view_proj(cam, 1280.0f / 800.0f);

    // Hand hover / card fly animation (real time)
    b->hovered = -1;
    for (int i = 0; i < b->nhand; i++) {
        HandCard *h = &b->hand[i];
        if (h->flying) { h->fly_t += dt_real * 3.0f; continue; }
        float x, y, w, hh; card_rect(b, i, &x, &y, &w, &hh);
        bool hov = b->state == BT_PLAYER && inside(mx, my, x, y - 34, w, hh + 34);
        if (hov) b->hovered = i;
        h->lift = damp(h->lift, hov ? 1.0f : 0.0f, 14.0f, dt_real);
    }
    for (int i = 0; i < b->nhand; i++) if (b->hand[i].flying && b->hand[i].fly_t >= 1.0f) { remove_from_hand(b, i); i--; }
    b->end_hover = b->state == BT_PLAYER && inside(mx, my, END_X, END_Y, END_W, END_H);

    switch (b->state) {
    case BT_INTRO:
        shot_base(b);
        if (b->t == 0 && dt > 0) {}
        if (b->t > 1.2f) begin_player_turn(b, fx);
        break;

    case BT_PLAYER:
        shot_base(b);
        play_bound(pm, ANIM_IDLE, 0, 0, 0.2f);   // idle loops; play_bound only restarts on change? it restarts each call
        if (in->click) {
            if (b->hovered >= 0 && can_play(b, b->hovered)) play_card(b, b->hovered, player, pm, ps, fx);
            else if (b->hovered >= 0) { audio_play(SND_FAIL, 0.4f, 1.6f); set_read(b, "can't"); }
            else if (b->end_hover) begin_enemy_turn(b, bm, boss, fx);
        }
        if (in->skip) begin_enemy_turn(b, bm, boss, fx);   // Enter also ends the turn
        break;

    case BT_CARD: {
        const CardDef *c = &b->cards[b->playing_card];
        if (c->kind == CK_ATTACK) {
            shot_player_attack(b);
            for (int i = 0; i < c->hits; i++) {
                float th = 0.38f + i * 0.22f;
                if (!b->card_hit_done[i] && b->t >= th) { b->card_hit_done[i] = true; card_hit_lands(b, c, boss, bm, ps, fx, ev, vp); }
            }
            if (b->t > 0.38f + (c->hits - 1) * 0.22f + 0.5f) {
                if (b->enemy_hp <= 0) { b->state = BT_WIN; b->t = 0; play_bound(bm, ANIM_DEAD, 0, 0, 0.1f); play_bound(pm, ANIM_ROAR, 0, 0, 0.2f); audio_play(SND_DEATH, 1, 0.7f); audio_play(SND_STAGGER, 0.8f, 0.6f); }
                else { b->state = BT_PLAYER; b->t = 0; }
            }
        } else {
            shot_base(b);
            if (b->t > 0.55f) { b->state = BT_PLAYER; b->t = 0; }
        }
    } break;

    case BT_ENEMY_TELL: {
        shot_enemy_attack(b);
        boss->c.tell = clampf(b->t / 0.9f, 0, 1);
        if (b->t >= 0.9f) { b->state = BT_ENEMY_ATTACK; b->t = 0; boss->c.tell = 1; audio_play(SND_SWING, 0.9f, 0.6f); }
    } break;

    case BT_ENEMY_ATTACK: {
        const EnemyAttack *a = &b->enemy.attacks[b->cur_attack];
        shot_enemy_attack(b);
        if (in->parry || in->click || in->rclick) { b->parry_pressed_t = b->t; audio_play(SND_WHIFF, 0.35f, 1.3f); play_bound(pm, ANIM_PARRY, 0.05f, 0.35f, 0.02f); }
        if (in->dodge && !b->dodging) { b->dodging = true; b->dodge_t = b->t; play_bound(pm, ANIM_DODGE, 0, 0.45f, 0.03f); audio_play(SND_WHIFF, 0.5f, 0.9f); }
        if (b->dodging && b->t > b->dodge_t + 0.45f) b->dodging = false;
        float window = b->wide_windows ? PARRY_WIDE : PARRY_WINDOW;
        for (int i = 0; i < a->hits; i++) {
            if (b->hit_done[i]) continue;
            float th = b->hit_t[i];
            bool pressed_ok = a->parryable && fabsf(b->parry_pressed_t - th) <= window && b->parry_pressed_t <= b->t;
            // Resolve at contact if a parry is already in, else wait for the late edge of the window.
            if ((b->t >= th && pressed_ok) || b->t >= th + window) {
                b->hit_done[i] = true;
                enemy_hit_lands(b, i, player, pm, bm, ps, fx, ev, vp);
                if (b->state == BT_LOSE) break;
            }
        }
        boss->c.tell = fmaxf(0, 1.0f - b->t * 2.0f);
        float last = b->hit_t[a->hits - 1];
        if (b->state == BT_ENEMY_ATTACK && b->t > last + a->tail) { b->state = BT_ENEMY_RECOVER; b->t = 0; }
    } break;

    case BT_ENEMY_RECOVER:
        shot_base(b);
        if (b->t > 0.5f) { if (bm->loaded) play_bound(bm, ANIM_IDLE, 0, 0, 0.25f); begin_player_turn(b, fx); }
        break;

    case BT_WIN: shot_win(b); break;
    case BT_LOSE: shot_base(b); break;
    }

    // Camera: ease toward the current shot
    float lam = 7.0f;
    b->cam_eye = v3_damp(b->cam_eye, b->shot_eye, lam, dt_real);
    b->cam_target = v3_damp(b->cam_target, b->shot_target, lam, dt_real);
    b->cam_fov = damp(b->cam_fov, b->shot_fov, lam, dt_real);
    camera_set_scene(cam, b->cam_eye, b->cam_target, b->cam_fov, true);

    // Animation advance (fight time, so hitstop freezes the swing at the impact)
    if (pm->loaded) anim_update(&pm->player, &pm->model, dt);
    if (bm->loaded) anim_update(&bm->player, &bm->model, dt);
}

bool battle_over(const Battle *b, bool *won) {
    if (b->state == BT_WIN) { *won = true; return b->t > 3.5f; }
    if (b->state == BT_LOSE) { *won = false; return b->t > 2.5f; }
    return false;
}

// ---------------------------------------------------------------- ui

static void text_c(Gfx *g, float cx, float y, float sc, Vec4 col, const char *s) { float w = gfx_ui_text_width(sc, s); gfx_ui_text(g, cx - w * 0.5f, y, sc, col, s); }
static void bar(Gfx *g, float x, float y, float w, float h, float k, Vec4 back, Vec4 front) {
    gfx_ui_rect(g, x - 2, y - 2, w + 4, h + 4, v4(0, 0, 0, 0.75f));
    gfx_ui_rect(g, x, y, w, h, back);
    gfx_ui_rect(g, x, y, w * clampf(k, 0, 1), h, front);
}
static void frame_rect(Gfx *g, float x, float y, float w, float h, float th, Vec4 c) {
    gfx_ui_rect(g, x, y, w, th, c); gfx_ui_rect(g, x, y + h - th, w, th, c);
    gfx_ui_rect(g, x, y, th, h, c); gfx_ui_rect(g, x + w - th, y, th, h, c);
}

// Word-wrap a description into the card
static void wrap_text(Gfx *g, float x, float y, float maxw, float sc, Vec4 col, const char *s) {
    char line[96] = ""; char word[48]; const char *p = s; float ly = y;
    while (*p) {
        int wl = 0; while (*p && *p != ' ' && wl < 47) word[wl++] = *p++; word[wl] = 0; while (*p == ' ') p++;
        char test[128]; snprintf(test, sizeof test, "%s%s%s", line, line[0] ? " " : "", word);
        if (gfx_ui_text_width(sc, test) > maxw && line[0]) { gfx_ui_text(g, x, ly, sc, col, line); ly += 12 * sc; snprintf(line, sizeof line, "%s", word); }
        else snprintf(line, sizeof line, "%s", test);
    }
    if (line[0]) gfx_ui_text(g, x, ly, sc, col, line);
}

void battle_draw_ui(const Battle *b, Gfx *g, Mat4 vp) {
    Vec4 white = v4(0.95f, 0.93f, 0.9f, 1), dim = v4(0.65f, 0.63f, 0.6f, 1);
    // Health bars
    bar(g, 40, 40, 300, 14, (float)b->player_hp / b->player_hp_max, v4(0.25f, 0.06f, 0.06f, 1), v4(0.85f, 0.2f, 0.18f, 1));
    { char s[48]; snprintf(s, sizeof s, "%d / %d", b->player_hp, b->player_hp_max); gfx_ui_text(g, 44, 60, 1.4f, white, s); }
    if (b->guard > 0) { char s[24]; snprintf(s, sizeof s, "GUARD %d", b->guard); gfx_ui_text(g, 200, 60, 1.4f, v4(0.6f, 0.75f, 1, 1), s); }
    bar(g, 940, 40, 300, 14, (float)b->enemy_hp / b->enemy_hp_max, v4(0.2f, 0.05f, 0.1f, 1), v4(0.8f, 0.25f, 0.3f, 1));
    { float w = gfx_ui_text_width(1.6f, b->enemy.name); gfx_ui_text(g, 1240 - w, 60, 1.6f, white, b->enemy.name); }
    { char s[48]; snprintf(s, sizeof s, "%d / %d", b->enemy_hp, b->enemy_hp_max); float w = gfx_ui_text_width(1.2f, s); gfx_ui_text(g, 1240 - w, 80, 1.2f, dim, s); }

    // Intent over the enemy during the player's turn
    if (b->state == BT_PLAYER || b->state == BT_CARD) {
        const EnemyAttack *a = &b->enemy.attacks[b->enemy.pattern[b->pattern_i % b->enemy.npattern]];
        float ex, ey;
        if (uifx_project(vp, chest(b->enemy_pos, 3.3f), &ex, &ey)) {
            float pulse = 0.85f + 0.15f * sinf(b->intent_pulse * 4.0f);
            char s[64]; snprintf(s, sizeof s, "%s  %d%s", a->name, a->damage, a->hits > 1 ? " x" : "");
            if (a->hits > 1) snprintf(s + strlen(s), sizeof s - strlen(s), "%d", a->hits);
            for (char *p = s; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
            float w = gfx_ui_text_width(1.6f, s);
            gfx_ui_rect(g, ex - w * 0.5f - 10, ey - 6, w + 20, 26, v4(0, 0, 0, 0.6f));
            frame_rect(g, ex - w * 0.5f - 10, ey - 6, w + 20, 26, 2, v4(a->tell.x, a->tell.y, a->tell.z, pulse));
            text_c(g, ex, ey, 1.6f, v4(a->tell.x * 1.2f, a->tell.y * 1.2f, a->tell.z * 1.2f, 1), s);
            if (!a->parryable) text_c(g, ex, ey + 26, 1.1f, v4(0.9f, 0.5f, 1, 1), "UNBLOCKABLE - DODGE (SHIFT)");
        }
    }
    // Parry cue during the enemy attack: a square closing on the player at each hit
    if (b->state == BT_ENEMY_ATTACK) {
        const EnemyAttack *a = &b->enemy.attacks[b->cur_attack];
        float px, py;
        if (uifx_project(vp, chest(b->player_pos, 1.3f), &px, &py)) {
            for (int i = 0; i < a->hits; i++) {
                if (b->hit_done[i]) continue;
                float th = b->hit_t[i];
                float remain = th - b->t;
                if (remain > 0.9f || remain < -0.05f) continue;
                float k = clampf(remain / 0.9f, 0, 1);
                float size = 40 + 260 * k;
                float alpha = 1.0f - k * 0.6f;
                Vec4 col = a->parryable ? v4(1, 0.9f, 0.5f, alpha) : v4(0.9f, 0.4f, 1, alpha);
                frame_rect(g, px - size * 0.5f, py - size * 0.5f, size, size, k < 0.12f ? 6 : 3, col);
            }
            frame_rect(g, px - 20, py - 20, 40, 40, 3, v4(1, 1, 1, 0.8f));
        }
        text_c(g, 640, 720, 1.4f, dim, "CLICK / RMB / SPACE  deflect       SHIFT  dodge");
    }

    // Energy
    {
        gfx_ui_rect(g, 24, 678, 300, 110, v4(0, 0, 0, 0.55f));
        gfx_ui_text(g, 40, 690, 1.3f, dim, "ENERGY");
        for (int i = 0; i < ENERGY_CAP; i++) {
            float x = 40 + i * 30, y = 710;
            bool on = i < b->energy;
            gfx_ui_rect(g, x, y, 22, 22, on ? v4(1, 0.85f, 0.35f, 1) : v4(0.2f, 0.18f, 0.15f, 1));
            frame_rect(g, x, y, 22, 22, 2, v4(0.6f, 0.5f, 0.3f, 1));
        }
        if (b->banked > 0) { char s[32]; snprintf(s, sizeof s, "+%d next turn from parries", b->banked); gfx_ui_text(g, 40, 740, 1.1f, v4(0.95f, 0.85f, 0.55f, 1), s); }
        char s[48]; snprintf(s, sizeof s, "draw %d   discard %d   round %d", b->ndraw, b->ndiscard, b->round);
        gfx_ui_text(g, 40, 765, 1.1f, dim, s);
    }
    // End turn
    if (b->state == BT_PLAYER) {
        Vec4 c = b->end_hover ? v4(0.9f, 0.75f, 0.4f, 1) : v4(0.55f, 0.45f, 0.3f, 1);
        gfx_ui_rect(g, END_X, END_Y, END_W, END_H, v4(0.08f, 0.06f, 0.05f, 0.9f));
        frame_rect(g, END_X, END_Y, END_W, END_H, 3, c);
        text_c(g, END_X + END_W * 0.5f, END_Y + 18, 1.8f, c, "END TURN");
    }
    // Hand
    for (int i = 0; i < b->nhand; i++) {
        const HandCard *h = &b->hand[i];
        const CardDef *c = &b->cards[h->def];
        float x, y, w, hh; card_rect(b, i, &x, &y, &w, &hh);
        float a = 1.0f;
        if (h->flying) { float k = h->fly_t; y -= 260 * ease_in_out(k); a = 1.0f - k; }
        bool playable = b->state == BT_PLAYER && can_play(b, i);
        Vec4 col = v4(c->color.x, c->color.y, c->color.z, a);
        gfx_ui_rect(g, x + 4, y + 6, w, hh, v4(0, 0, 0, 0.5f * a));
        gfx_ui_rect(g, x, y, w, hh, v4(0.09f, 0.08f, 0.1f, 0.96f * a));
        gfx_ui_rect(g, x, y, w, 34, v4(col.x * 0.55f, col.y * 0.55f, col.z * 0.55f, a));
        frame_rect(g, x, y, w, hh, 3, playable ? (b->hovered == i ? v4(1, 0.95f, 0.7f, a) : col) : v4(0.3f, 0.3f, 0.32f, a));
        // cost
        gfx_ui_rect(g, x + 8, y + 6, 24, 24, playable ? v4(1, 0.85f, 0.35f, a) : v4(0.35f, 0.3f, 0.2f, a));
        { char s[8]; snprintf(s, sizeof s, "%d", c->cost); text_c(g, x + 20, y + 12, 1.5f, v4(0.1f, 0.08f, 0.05f, a), s); }
        // name
        { char nm[32]; snprintf(nm, sizeof nm, "%s", c->name); for (char *p = nm; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32; text_c(g, x + w * 0.5f + 12, y + 12, 1.5f, v4(1, 1, 1, a), nm); }
        // art placeholder: a glyph block in the card colour
        gfx_ui_rect(g, x + 16, y + 46, w - 32, 70, v4(col.x * 0.25f, col.y * 0.25f, col.z * 0.25f, a));
        { float cx = x + w * 0.5f, cy = y + 81;
          if (c->kind == CK_ATTACK) for (int k = 0; k < c->hits; k++) gfx_ui_rect(g, cx - 6 + (k - (c->hits - 1) * 0.5f) * 18, cy - 20, 12, 40, col);
          else if (c->kind == CK_GUARD) frame_rect(g, cx - 18, cy - 20, 36, 40, 5, col);
          else gfx_ui_rect(g, cx - 14, cy - 14, 28, 28, col); }
        wrap_text(g, x + 12, y + 128, w - 24, 1.15f, v4(0.9f, 0.9f, 0.88f, a), c->desc);
        if (!playable && b->state == BT_PLAYER) gfx_ui_rect(g, x, y, w, hh, v4(0, 0, 0, 0.45f * a));
    }
    // Outcome
    if (b->state == BT_WIN) {
        float k = clampf(b->t / 0.6f, 0, 1);
        gfx_ui_rect(g, 0, 330, 1280, 120, v4(0, 0, 0, 0.6f * k));
        text_c(g, 640, 360, 4.0f * (0.6f + 0.4f * k), v4(1, 0.9f, 0.6f, k), "THE COUNT FALLS");
        char s[96]; snprintf(s, sizeof s, "parries %u   perfect %u   hits taken %u   rounds %d", b->parries, b->perfects, b->hits_taken, b->round);
        text_c(g, 640, 415, 1.4f, dim, s);
    } else if (b->state == BT_LOSE) {
        float k = clampf(b->t / 0.6f, 0, 1);
        gfx_ui_rect(g, 0, 330, 1280, 100, v4(0, 0, 0, 0.7f * k));
        text_c(g, 640, 365, 3.5f, v4(0.8f, 0.15f, 0.15f, k), "THE COUNT CONTINUES");
    }
}

void battle_bot(const Battle *b, const CharModel *bm, Input *in, float *mx, float *my, unsigned tick) {
    in->click = in->rclick = in->parry = in->dodge = in->skip = false;
    if (b->state == BT_PLAYER) {
        int pick = -1;
        for (int i = 0; i < b->nhand; i++) if (!b->hand[i].flying && can_play(b, i)) { pick = i; break; }
        if (pick >= 0) {
            float x, y, w, h; card_rect(b, pick, &x, &y, &w, &h);
            *mx = x + w * 0.5f; *my = y + h * 0.5f;
            if (tick % 12 == 0 && b->hovered == pick) in->click = true;   // hover first, then click
        } else {
            *mx = END_X + END_W * 0.5f; *my = END_Y + END_H * 0.5f;
            if (tick % 12 == 0 && b->end_hover) in->click = true;
        }
    } else if (b->state == BT_ENEMY_ATTACK) {
        const EnemyAttack *a = &b->enemy.attacks[b->cur_attack];
        for (int i = 0; i < a->hits; i++) {
            if (b->hit_done[i]) continue;
            float th = b->hit_t[i]; (void)bm;
            if (!a->parryable) { if (b->t >= th - 0.3f && !b->dodging) in->dodge = true; }
            else if (b->t >= th - 0.02f && b->parry_pressed_t < th - 0.5f) in->parry = true;
            break;
        }
    }
}
