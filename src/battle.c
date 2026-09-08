#include "battle.h"
#include "audio.h"
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Judgement windows (seconds either side of the beat), osu-style tiers
#define W_PERFECT 0.045f
#define W_GREAT   0.095f
#define W_GOOD    0.150f
#define APPROACH  0.85f      // how long the approach ring takes to close
#define RING_R    46.0f      // hit circle radius in UI pixels
#define HAND_SIZE      5
#define ENERGY_BASE    3
#define ENERGY_CAP     6
#define COUNTER_DAMAGE 4
#define SFX(rel) (HOLLOW_ASSET_DIR "/sprites/ninja/Audio/Sounds/" rel)

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
                else if (!strcmp(k, "charge") && i + 1 < nt) snprintf(a->charge, sizeof a->charge, "%s", tok[++i]);
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

// ---------------------------------------------------------------- sprite effects

void battle_load_fx(Battle *b, Gfx *g, const char *path) {
    b->fx_loaded = sprite_def_load(g, &b->fxdef, path);
    for (int i = 0; i < FX_MAX; i++) b->fx[i].alive = false;
}

static void fx_spawn(Battle *b, const char *anim, Vec3 pos, float scale, Vec3 tint, bool flip) {
    if (!b->fx_loaded) return;
    int a = sprite_find_anim(&b->fxdef, anim);
    if (a < 0) return;
    for (int i = 0; i < FX_MAX; i++) {
        SpriteFx *f = &b->fx[i];
        if (f->alive) continue;
        f->alive = true; sprite_actor_init(&f->actor, &b->fxdef); sprite_play(&f->actor, a, 1, true);
        f->pos = pos; f->scale = scale; f->tint = tint; f->flip = flip;
        return;
    }
}

static void fx_update(Battle *b, float dt) {
    for (int i = 0; i < FX_MAX; i++) { SpriteFx *f = &b->fx[i]; if (!f->alive) continue; sprite_update(&f->actor, dt); if (f->actor.finished) f->alive = false; }
}

void battle_draw_world(Battle *b, Gfx *g) {
    for (int i = 0; i < FX_MAX; i++) {
        const SpriteFx *f = &b->fx[i];
        if (!f->alive) continue;
        Material m = material_default(); m.emissive = v3_scale(f->tint, 1.2f);
        gfx_set_material(g, &m);
        SpriteActor a = f->actor; a.facing = f->flip ? FACE_RIGHT : FACE_LEFT;
        sprite_actor_draw(g, &a, f->pos, v4(f->tint.x, f->tint.y, f->tint.z, 1), f->scale);
    }
    gfx_set_material(g, NULL);
}

// ---------------------------------------------------------------- piles
static void card_begin_play(Battle *b, int i);
static void card_begin_discard(Battle *b, int i);

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
    h->phase = CP_DRAWING; h->x = 1330; h->y = 760; h->rot = 0.6f; h->sc = 0.6f;
    h->vx = -900 + (float)(rand() % 300); h->vy = -500; h->vrot = -6;
}

static void discard_hand(Battle *b) {
    for (int i = 0; i < b->nhand; i++) if (b->hand[i].phase == CP_HAND || b->hand[i].phase == CP_DRAWING) card_begin_discard(b, i);
}

static void card_begin_play(Battle *b, int i) { b->hand[i].phase = CP_PLAYING; b->hand[i].phase_t = 0; b->hand[i].vy -= 900; b->hand[i].vrot += 3; }
static void card_begin_discard(Battle *b, int i) { b->hand[i].phase = CP_DISCARDING; b->hand[i].phase_t = 0; b->hand[i].vx -= 600; b->hand[i].vrot -= 4; }

static void remove_from_hand(Battle *b, int i) {
    if (b->ndiscard < DECK_MAX) b->discard[b->ndiscard++] = b->hand[i].def;
    for (int k = i; k < b->nhand - 1; k++) b->hand[k] = b->hand[k + 1];
    b->nhand--;
    // keep indices that point into the hand valid
    if (b->dragging == i) b->dragging = -1; else if (b->dragging > i) b->dragging--;
    if (b->hovered == i) b->hovered = -1; else if (b->hovered > i) b->hovered--;
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
    b->pattern_i = 0; b->timescale = 1; b->hitstop = 0; b->hovered = -1; b->playing_card = -1; b->dragging = -1; b->drop_target = 0;
    b->parries = b->perfects = b->hits_taken = 0; b->combo = b->max_combo = 0; b->last_judge = J_NONE; b->judge_t = -1;
    for (int i = 0; i < HITS_MAX; i++) { b->burst_t[i] = -1; b->hit_judge[i] = J_NONE; }
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
    b->shot_eye = v3_add(v3_add(v3_add(c, v3_scale(r, -9.0f)), v3_scale(f, -1.0f)), v3(0, 2.6f, 0));
    b->shot_target = v3_add(c, v3(0, 1.5f, 0)); b->shot_fov = 36;
}
static bool g_flat_shots = false;   // set when the actors are sprites
static void shot_push(Battle *b, float toward_enemy, float dist_k, float fov) {
    Vec3 c = v3_lerp(b->player_pos, b->enemy_pos, toward_enemy), r = right_of(b->stage_yaw), f = fwd_of(b->stage_yaw);
    b->shot_eye = v3_add(v3_add(v3_add(c, v3_scale(r, -8.5f * dist_k)), v3_scale(f, -2.0f * dist_k)), v3(0, 2.6f * dist_k + 0.6f, 0));
    b->shot_target = v3_add(c, v3(0, 1.3f, 0)); b->shot_fov = fov;
}
static void shot_player_attack(Battle *b) {
    if (g_flat_shots) { shot_push(b, 0.6f, 0.72f, 34); return; }
    Vec3 r = right_of(b->stage_yaw), f = fwd_of(b->stage_yaw);
    b->shot_eye = v3_add(v3_add(v3_sub(b->player_pos, v3_scale(f, 6.5f)), v3_scale(r, 4.8f)), v3(0, 2.5f, 0));
    b->shot_target = chest(v3_lerp(b->player_pos, b->enemy_pos, 0.62f), 1.2f); b->shot_fov = 38;
}
static void shot_enemy_attack(Battle *b) {
    if (g_flat_shots) { shot_push(b, 0.42f, 0.78f, 36); return; }
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
    if (bm->is_sprite) return charmodel_sprite_contact(bm, a->clip, i, a->lead);
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
    if (cm->is_sprite) {
        const SpriteAnim *sa = &cm->sdef.anims[bd->clip];
        if (sa->ncontact > 0 && lead > 0) sprite_play_fitted(&cm->sprite, bd->clip, lead);
        else if (!sa->loop && tail > 0) { float nat = sprite_anim_duration(&cm->sdef, bd->clip, 1); sprite_play(&cm->sprite, bd->clip, nat > 0.01f ? nat / (lead + tail) : 1, true); }
        else sprite_play(&cm->sprite, bd->clip, bd->rate > 0 ? bd->rate : 1, false);
        return;
    }
    const AnimClip *clip = &cm->model.clips[bd->clip];
    if (bd->contact >= 0 && (lead > 0 || tail > 0)) anim_play_fitted(&cm->player, &cm->model, bd->clip, bd->contact * clip->duration, lead, tail, fade);
    else if (!bd->loop && tail > 0) anim_play_fitted(&cm->player, &cm->model, bd->clip, 0, 0, lead + tail, fade);
    else anim_play(&cm->player, &cm->model, bd->clip, bd->rate, bd->loop, bd->hold || !bd->loop, fade);
}

// ---------------------------------------------------------------- turn logic

static void begin_player_turn(Battle *b, Uifx *fx) {
    b->round++;
    dbg_log("player turn %d", b->round);
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
    b->parry_pressed_t = -10; b->press_used = true; b->dodging = false;
    for (int i = 0; i < HITS_MAX; i++) { b->burst_t[i] = -1; b->hit_judge[i] = J_NONE; }
    const EnemyAttack *a = &b->enemy.attacks[b->cur_attack];
    char banner[96]; snprintf(banner, sizeof banner, "%s", b->enemy.name);
    for (char *p = banner; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
    uifx_spawn(fx, UIFX_BANNER, 0, 300, banner, v4(a->tell.x, a->tell.y, a->tell.z, 1), 3.0f, 1.1f);
    boss->c.tell_color = a->tell; boss->c.tell = 0;
    dbg_log("enemy turn: %s (%d hits, parry %s)", a->name, a->hits, a->parryable ? "yes" : "no");
    for (int i = 0; i < a->hits; i++) b->hit_t[i] = hit_time(b, bm, i);
    if (bm->is_sprite) {
        charmodel_sprite_play(bm, a->charge[0] ? a->charge : "idle", 0, true);
    } else if (bm->loaded) {
        int clip = model_find_clip(&bm->model, a->clip);
        if (clip >= 0) anim_play_fitted(&bm->player, &bm->model, clip, a->contact[0] * bm->model.clips[clip].duration, a->lead + 0.9f, a->tail, 0.15f);
        else play_bound(bm, ANIM_ATTACK, a->lead + 0.9f, a->tail, 0.15f);
    }
}

static Vec3 hit_world(const Battle *b, int i) {
    // Where hit i's circle sits: on the player's body, alternating sides for multi-hit attacks
    Vec3 r = right_of(b->stage_yaw);
    float side = i == 0 ? 0.0f : (i % 2 ? 0.55f : -0.55f);
    return v3_add(chest(b->player_pos, 1.25f + (i >= 2 ? 0.35f : 0.0f)), v3_scale(r, side));
}

static void enemy_hit_lands(Battle *b, int i, Player *player, CharModel *pm, CharModel *bm, Particles *ps, Uifx *fx, CombatEvents *ev, Mat4 vp) {
    (void)bm;
    const EnemyAttack *a = &b->enemy.attacks[b->cur_attack];
    float th = b->hit_t[i];
    float wide = b->wide_windows ? 1.5f : 1.0f;
    Vec3 contact = v3_add(chest(b->player_pos, 1.1f), v3_scale(fwd_of(b->stage_yaw), 0.7f));
    float px, py; if (!uifx_project(vp, hit_world(b, i), &px, &py)) { px = 640; py = 300; }
    float offset = b->parry_pressed_t - th;             // negative = early
    bool have_press = !b->press_used && fabsf(offset) <= W_GOOD * wide;
    Judge j = J_MISS;
    if (have_press) j = fabsf(offset) <= W_PERFECT * wide ? J_PERFECT : fabsf(offset) <= W_GREAT * wide ? J_GREAT : J_GOOD;
    b->burst_t[i] = 0; b->hit_judge[i] = j;

    if (b->dodging) {
        uifx_spawn(fx, UIFX_BLOCK, px, py - 70, "DODGED", v4(0.7f, 0.8f, 1, 1), 2.2f, 0.7f);
        set_read(b, "dodged"); audio_play(SND_WHIFF, 0.6f, 1.1f);
        return;
    }
    if (have_press && a->parryable) {
        b->press_used = true;
        b->parries++; b->parried_this_round = true;
        b->combo++; if (b->combo > b->max_combo) b->max_combo = b->combo;
        b->last_judge = j; b->judge_t = 0; b->last_offset = offset;
        int gain = j == J_PERFECT ? 2 : j == J_GREAT ? 1 : 0;
        if (b->combo % 4 == 0) gain += 1;                 // combo bonus every fourth parry
        b->banked += gain; if (b->banked > ENERGY_CAP - ENERGY_BASE) b->banked = ENERGY_CAP - ENERGY_BASE;
        const char *txt = j == J_PERFECT ? "PERFECT" : j == J_GREAT ? "GREAT" : "GOOD";
        dbg_log("hit %d %s offset %+.3f combo %d", i, txt, offset, b->combo);
        Vec4 col = j == J_PERFECT ? v4(1, 0.95f, 0.5f, 1) : j == J_GREAT ? v4(0.6f, 1, 0.7f, 1) : v4(0.7f, 0.85f, 1, 1);
        uifx_spawn(fx, UIFX_PARRY, px, py - 80, txt, col, j == J_PERFECT ? 4.2f : 3.2f, 0.8f);
        if (b->combo >= 2) { char cs[16]; snprintf(cs, sizeof cs, "x%d", b->combo); uifx_spawn(fx, UIFX_DAMAGE, px + 90, py - 60, cs, v4(1, 0.8f, 0.4f, 1), 2.2f + fminf(b->combo, 12) * 0.12f, 0.7f); }
        spawn_sparks(ps, contact, j == J_PERFECT ? v3(3.0f, 2.6f, 1.2f) : v3(1.6f, 2.4f, 2.6f), j == J_PERFECT ? 64 : 28, j == J_PERFECT ? 9.0f : 5.5f);
        fx_spawn(b, j == J_PERFECT ? "spark" : "parry", v3(contact.x, contact.y - 0.6f, contact.z), j == J_PERFECT ? 1.6f : 1.2f, v3(1.0f, 0.95f, 0.6f), false);
        play_bound(pm, ANIM_PARRY_HIT, 0.04f, 0.5f, 0.0f);
        ev->parried = true; ev->contact = contact;
        ev->shake = fmaxf(ev->shake, j == J_PERFECT ? 0.5f : 0.25f);
        b->hitstop = j == J_PERFECT ? 0.14f : j == J_GREAT ? 0.09f : 0.05f;
        if (j == J_PERFECT) b->timescale = 0.3f;
        float pitch = 1.0f + 0.03f * (float)(b->combo < 12 ? b->combo : 12);
        audio_play(SND_PARRY, 1.0f, pitch * (j == J_PERFECT ? 1.12f : 1.0f));
        audio_play_file(SFX("Hit & Impact/Hit5.wav"), 0.6f, pitch * 1.3f);
        if (j == J_GOOD) audio_play(SND_BLIP, 0.4f, 0.8f);
        b->perfects += j == J_PERFECT;
        set_read(b, txt);
        if (j == J_PERFECT) {
            int cd = COUNTER_DAMAGE + b->combo / 3;
            b->enemy_hp -= cd; if (b->enemy_hp < 0) b->enemy_hp = 0;
            float ex, ey; if (uifx_project(vp, chest(b->enemy_pos, 2.6f), &ex, &ey)) { char t[16]; snprintf(t, sizeof t, "%d", cd); uifx_spawn(fx, UIFX_DAMAGE, ex, ey, t, v4(1, 0.9f, 0.6f, 1), 2.8f, 0.9f); }
        }
        return;
    }
    // Miss: the hit lands
    dbg_log("hit %d MISS press %.3f beat %.3f dmg %d", i, b->parry_pressed_t, th, a->damage);
    b->combo = 0; b->last_judge = J_MISS; b->judge_t = 0; b->last_offset = have_press ? offset : (b->parry_pressed_t > th - 0.6f ? offset : 0);
    int dmg = a->damage;
    if (b->guard > 0) { int absorbed = dmg < b->guard ? dmg : b->guard; dmg -= absorbed; b->guard -= absorbed;
        if (absorbed > 0) { char t[24]; snprintf(t, sizeof t, "GUARD %d", absorbed); uifx_spawn(fx, UIFX_BLOCK, px - 40, py + 30, t, v4(0.6f, 0.7f, 0.95f, 1), 2.0f, 0.8f); } }
    b->player_hp -= dmg; if (b->player_hp < 0) b->player_hp = 0;
    b->hits_taken++;
    char t[16]; snprintf(t, sizeof t, "%d", dmg);
    uifx_spawn(fx, UIFX_DAMAGE, px, py, t, v4(1, 0.35f, 0.3f, 1), 3.4f, 0.9f);
    if (!a->parryable && b->parry_pressed_t > th - 0.8f) uifx_spawn(fx, UIFX_FAIL, px, py - 70, "UNBLOCKABLE", v4(0.9f, 0.3f, 0.9f, 1), 2.4f, 0.9f);
    else if (a->parryable && !b->press_used && offset < 0 && offset > -0.6f) uifx_spawn(fx, UIFX_FAIL, px, py - 70, "EARLY", v4(1, 0.4f, 0.3f, 1), 2.4f, 0.9f);
    else if (a->parryable && !b->press_used && offset > 0) uifx_spawn(fx, UIFX_FAIL, px, py - 70, "LATE", v4(1, 0.4f, 0.3f, 1), 2.4f, 0.9f);
    else uifx_spawn(fx, UIFX_FAIL, px, py - 70, "MISS", v4(1, 0.4f, 0.3f, 1), 2.4f, 0.9f);
    spawn_sparks(ps, contact, v3(2.5f, 0.3f, 0.2f), 18, 4.0f);
    fx_spawn(b, "hit", v3(contact.x, contact.y - 0.6f, contact.z), 1.4f, v3(1.0f, 0.5f, 0.4f), false);
    play_bound(pm, ANIM_HURT, 0, 0.45f, 0.0f);
    player->c.flash = 1.0f;
    ev->player_hit = true; ev->contact = contact; ev->shake = fmaxf(ev->shake, 0.5f);
    b->hitstop = 0.06f;
    audio_play(SND_FAIL, 0.5f, 1.0f); audio_play_file(SFX("Hit & Impact/Hit2.wav"), 0.9f, 0.9f);
    if (b->player_hp <= 0) { b->state = BT_LOSE; b->t = 0; play_bound(pm, ANIM_DEAD, 0, 0, 0.1f); audio_play(SND_DEATH, 1, 1); }
}

static void card_hit_lands(Battle *b, const CardDef *c, Boss *boss, CharModel *bm, Particles *ps, Uifx *fx, CombatEvents *ev, Mat4 vp) {
    Vec3 contact = v3_add(chest(b->enemy_pos, 1.3f), v3_scale(fwd_of(b->stage_yaw), -0.8f));
    b->enemy_hp -= c->damage; if (b->enemy_hp < 0) b->enemy_hp = 0;
    float ex, ey; if (!uifx_project(vp, chest(b->enemy_pos, 2.7f), &ex, &ey)) { ex = 640; ey = 200; }
    char s[16]; snprintf(s, sizeof s, "%d", c->damage);
    uifx_spawn(fx, UIFX_DAMAGE, ex + (float)(rand() % 40 - 20), ey, s, v4(1, 0.85f, 0.5f, 1), c->damage >= 12 ? 4.2f : 3.2f, 0.9f);
    spawn_sparks(ps, contact, v3(2.2f, 1.8f, 1.0f), c->damage >= 12 ? 30 : 14, 5.0f);
    fx_spawn(b, c->damage >= 12 ? "cutx" : "slash", v3(contact.x, contact.y - 0.7f, contact.z), c->damage >= 12 ? 2.2f : 1.6f, v3(1.0f, 0.9f, 0.8f), false);
    boss->c.flash = 1.0f;
    play_bound(bm, ANIM_HURT, 0, 0.4f, 0.0f);
    ev->boss_hit = true; ev->contact = contact; ev->shake = fmaxf(ev->shake, c->damage >= 12 ? 0.45f : 0.2f);
    b->hitstop = c->damage >= 12 ? 0.09f : 0.04f;
    if (c->damage >= 12) audio_play_file(SFX("Hit & Impact/Hit3.wav"), 0.9f, 0.85f); else audio_play_file(SFX("Hit & Impact/Hit1.wav"), 0.9f, 1.0f);
}

static bool can_play(const Battle *b, int hand_i) {
    const CardDef *c = &b->cards[b->hand[hand_i].def];
    if (c->cost > b->energy) return false;
    if (c->needs_parried && !b->parried_this_round) return false;
    return true;
}

// Fan layout: slot i of n has a centre and a rotation along an arc at the bottom of the screen.
#define CARD_W 150.0f
#define CARD_H 210.0f
static int hand_slots(const Battle *b) { int n = 0; for (int i = 0; i < b->nhand; i++) if (b->hand[i].phase == CP_HAND || b->hand[i].phase == CP_DRAWING) n++; return n; }
static void fan_slot(int slot, int n, float *cx, float *cy, float *rot) {
    float spread = fminf(1.0f, 6.5f / (float)(n > 1 ? n : 1));
    float k = n > 1 ? ((float)slot - (n - 1) * 0.5f) : 0.0f;
    float ang = k * 0.058f * spread * 2.0f;
    float radius = 1300.0f;
    *cx = 640 + sinf(ang) * radius;
    *cy = 800 - CARD_H * 0.5f - 12 + (1.0f - cosf(ang)) * radius;
    *rot = ang;
}
static bool inside(float px, float py, float x, float y, float w, float h) { return px >= x && px <= x + w && py >= y && py <= y + h; }
static bool inside_card(const HandCard *h, float px, float py) {
    // test in the card's local frame
    float ca = cosf(-h->rot), sa = sinf(-h->rot);
    float dx = px - h->x, dy = py - h->y;
    float lx = dx * ca - dy * sa, ly = dx * sa + dy * ca;
    float hw = CARD_W * 0.5f * h->sc, hh = CARD_H * 0.5f * h->sc;
    return lx >= -hw && lx <= hw && ly >= -hh - 30 && ly <= hh;
}
#define END_X 1090.0f
#define END_Y 560.0f
#define END_W 160.0f
#define END_H 52.0f

static void spring(float *x, float *v, float target, float k, float d, float dt) {
    float a = (target - *x) * k - *v * d;
    *v += a * dt; *x += *v * dt;
}

static void update_cards(Battle *b, float mx, float my, float dt) {
    int n = hand_slots(b), slot = 0;
    b->hovered = -1;
    // hover pick: of the cards under the mouse, the one whose centre is nearest (fans overlap)
    float best = 1e9f;
    for (int i = 0; i < b->nhand; i++) {
        if (b->state != BT_PLAYER || b->dragging >= 0 || b->hand[i].phase != CP_HAND || !inside_card(&b->hand[i], mx, my)) continue;
        float d = fabsf(mx - b->hand[i].x) - (b->hovered == i ? 20.0f : 0.0f);   // a little hysteresis on the current pick
        if (d < best) { best = d; b->hovered = i; }
    }
    for (int i = 0; i < b->nhand; i++) {
        HandCard *h = &b->hand[i];
        float tx, ty, trot, tsc = 1.0f;
        h->phase_t += dt;
        if (h->phase == CP_DRAWING || h->phase == CP_HAND) {
            fan_slot(slot++, n, &tx, &ty, &trot);
            if (h->phase == CP_DRAWING && h->phase_t > 0.25f) h->phase = CP_HAND;
            bool hov = b->hovered == i;
            h->hover = damp(h->hover, hov ? 1.0f : 0.0f, 16.0f, dt);
            ty -= 70.0f * h->hover; tsc = 1.0f + 0.14f * h->hover;
            // hovered cards stand upright and tilt toward the mouse
            trot = lerpf(trot, (mx - h->x) * 0.0012f, h->hover);
            // neighbours make room
            if (b->hovered >= 0 && !hov) { float away = (float)(i - b->hovered); tx += (away > 0 ? 1 : -1) * 22.0f / fabsf(away); }
        } else if (h->phase == CP_DRAG) {
            tx = mx; ty = my; trot = (mx - h->x) * 0.0015f; tsc = 1.06f;   // the card centres under the cursor
        } else if (h->phase == CP_PLAYING) {
            tx = 640; ty = 330; trot = 0; tsc = 0.35f;
            if (h->phase_t > 0.45f) { remove_from_hand(b, i); i--; continue; }
        } else { // discarding
            tx = -120; ty = 700; trot = -0.8f; tsc = 0.6f;
            if (h->phase_t > 0.5f) { remove_from_hand(b, i); i--; continue; }
        }
        float k = h->phase == CP_PLAYING ? 320 : h->phase == CP_DRAG ? 700 : 240, d = 2.0f * sqrtf(k) * 0.95f;
        spring(&h->x, &h->vx, tx, k, d, dt);
        spring(&h->y, &h->vy, ty, k, d, dt);
        spring(&h->rot, &h->vrot, trot, k, d, dt);
        spring(&h->sc, &h->vsc, tsc, k, d, dt);
    }
}

static void play_card(Battle *b, int hand_i, Player *player, CharModel *pm, Particles *ps, Uifx *fx) {
    (void)player;
    const CardDef *c = &b->cards[b->hand[hand_i].def];
    b->energy -= c->cost;
    b->playing_card = b->hand[hand_i].def;
    card_begin_play(b, hand_i);
    b->state = BT_CARD; b->t = 0; b->card_hit_i = 0;
    for (int i = 0; i < HITS_MAX; i++) b->card_hit_done[i] = false;
    audio_play_file(SFX("Menu/Accept.wav"), 0.5f, 1.0f);
    switch (c->kind) {
    case CK_ATTACK: play_bound(pm, c->anim, 0.38f, 0.45f + (c->hits - 1) * 0.22f, 0.06f); audio_play_file(SFX("Whoosh & Slash/Slash2.wav"), 0.7f, 1.0f + 0.1f * (c->hits - 1)); break;
    case CK_GUARD: b->guard += c->block; play_bound(pm, ANIM_PARRY, 0.08f, 0.5f, 0.05f);
        { char s[24]; snprintf(s, sizeof s, "GUARD %d", b->guard); uifx_spawn(fx, UIFX_BLOCK, 640, 420, s, v4(0.6f, 0.75f, 1, 1), 2.6f, 0.9f); }
        particles_burst(ps, PT_SPORE, chest(b->player_pos, 1.0f), v3(0, 1, 0), 30, 1.5f, v3(0.8f, 1.2f, 2.5f), 0.05f, 1.2f);
        break;
    case CK_DRAW: for (int i = 0; i < c->draw; i++) draw_card(b); play_bound(pm, ANIM_IDLE, 0, 0, 0.1f); break;
    case CK_HEAL: b->player_hp += c->heal; if (b->player_hp > b->player_hp_max) b->player_hp = b->player_hp_max;
        { char s[16]; snprintf(s, sizeof s, "+%d", c->heal); uifx_spawn(fx, UIFX_HEAL, 640, 400, s, v4(0.6f, 1, 0.6f, 1), 3.0f, 1.0f); }
        particles_burst(ps, PT_SPORE, chest(b->player_pos, 0.8f), v3(0, 1, 0), 50, 1.2f, v3(1.2f, 2.8f, 1.0f), 0.06f, 1.6f);
        audio_play_file(SFX("Magic & Skill/Heal.wav"), 0.7f, 1.0f);
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

    g_flat_shots = pm->is_sprite;
    // Characters stand on their marks facing each other
    player->c.pos = b->player_pos; player->c.yaw = b->stage_yaw;
    boss->c.pos = b->enemy_pos; boss->c.yaw = b->stage_yaw + PI;
    if (player->c.flash > 0) player->c.flash = fmaxf(0, player->c.flash - dt_real * 6);
    if (boss->c.flash > 0) boss->c.flash = fmaxf(0, boss->c.flash - dt_real * 5);
    Mat4 vp = camera_view_proj(cam, 1280.0f / 800.0f);
    if (!uifx_project(vp, chest(b->enemy_pos, 1.6f), &b->enemy_sx, &b->enemy_sy)) { b->enemy_sx = 300; b->enemy_sy = 400; }
    if (!uifx_project(vp, chest(b->player_pos, 1.0f), &b->player_sx, &b->player_sy)) { b->player_sx = 980; b->player_sy = 460; }

    update_cards(b, mx, my, dt_real);
    b->end_hover = b->state == BT_PLAYER && inside(mx, my, END_X, END_Y, END_W, END_H);
    if (b->judge_t >= 0) b->judge_t += dt_real;
    for (int i = 0; i < HITS_MAX; i++) if (b->burst_t[i] >= 0) b->burst_t[i] += dt_real;

    switch (b->state) {
    case BT_INTRO:
        shot_base(b);
        if (b->t == 0 && dt > 0) {}
        if (b->t > 1.2f) begin_player_turn(b, fx);
        break;

    case BT_PLAYER: {
        shot_base(b);
        play_bound(pm, ANIM_IDLE, 0, 0, 0.2f);
        // Drag a card from the hand and drop it on a target: attacks on the enemy, everything else on yourself.
        if (b->dragging < 0 && in->click) {
            // The card under the cursor, whether or not it has finished lifting into hover
            int pick = -1; float best = 1e9f;
            for (int i = 0; i < b->nhand; i++) {
                if (b->hand[i].phase != CP_HAND && b->hand[i].phase != CP_DRAWING) continue;
                if (!inside_card(&b->hand[i], mx, my)) continue;
                float d = fabsf(mx - b->hand[i].x); if (d < best) { best = d; pick = i; }
            }
            if (pick >= 0 && can_play(b, pick)) {
                b->dragging = pick; b->hand[pick].phase = CP_DRAG; b->drag_t = 0; b->drag_x0 = mx; b->drag_y0 = my;
                b->drop_target = b->cards[b->hand[pick].def].kind == CK_ATTACK ? 1 : 2;   // auto target from the moment it is picked up
                dbg_log("pick %s at %.0f %.0f (card %.0f %.0f)", b->cards[b->hand[pick].def].name, mx, my, b->hand[pick].x, b->hand[pick].y);
                audio_play_file(SFX("Menu/Accept.wav"), 0.3f, 1.3f);
            }
            else if (pick >= 0) { audio_play(SND_FAIL, 0.4f, 1.6f); set_read(b, "can't"); dbg_log("pick refused: %s (energy %d)", b->cards[b->hand[pick].def].name, b->energy); }
            else if (b->end_hover) begin_enemy_turn(b, bm, boss, fx);
            else dbg_log("press at %.0f %.0f hit nothing (hand %d)", mx, my, b->nhand);
        }
        if (b->dragging >= 0) {
            b->drag_t += dt_real; b->drag_mx = mx; b->drag_my = my;
            const CardDef *c = &b->cards[b->hand[b->dragging].def];
            // The card keeps its auto target; steering the cursor near another valid target retargets it.
            // (With one enemy the attack target never changes; with several the nearest within reach wins.)
            if (c->kind == CK_ATTACK) { float de = hypotf(mx - b->enemy_sx, my - b->enemy_sy); if (de < 260) b->drop_target = 1; }
            bool in_hand_zone = my > 600;
            if (!in->mouse_held) {
                bool quick = b->drag_t < 0.18f && hypotf(mx - b->drag_x0, my - b->drag_y0) < 12;   // a plain click plays straight away
                int idx = b->dragging; b->dragging = -1;
                if (quick || !in_hand_zone) { b->hand[idx].phase = CP_HAND; dbg_log("release -> play %s target %d%s", b->cards[b->hand[idx].def].name, b->drop_target, quick ? " (quick)" : ""); play_card(b, idx, player, pm, ps, fx); }
                else { b->hand[idx].phase = CP_HAND; b->hand[idx].vy -= 200; dbg_log("release in hand: cancelled %s", b->cards[b->hand[idx].def].name); }
                b->drop_target = 0;
            }
        }
        if (in->skip && b->dragging < 0) begin_enemy_turn(b, bm, boss, fx);   // Enter also ends the turn
    } break;

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
        if (in->parry || in->click || in->rclick) { b->parry_pressed_t = b->t - 0.9f; b->press_used = false; audio_play(SND_WHIFF, 0.35f, 1.3f); play_bound(pm, ANIM_PARRY, 0.05f, 0.35f, 0.02f); }
        if (b->t >= 0.9f) { b->state = BT_ENEMY_ATTACK; b->t = 0; boss->c.tell = 1; audio_play_file(SFX("Whoosh & Slash/Slash4.wav"), 0.9f, 0.7f);
            if (bm->is_sprite) { const EnemyAttack *a = &b->enemy.attacks[b->cur_attack]; charmodel_sprite_play(bm, a->clip, a->lead, true); } }
    } break;

    case BT_ENEMY_ATTACK: {
        const EnemyAttack *a = &b->enemy.attacks[b->cur_attack];
        shot_enemy_attack(b);
        if (in->parry || in->click || in->rclick) { b->parry_pressed_t = b->t; b->press_used = false; audio_play(SND_WHIFF, 0.35f, 1.3f); play_bound(pm, ANIM_PARRY, 0.05f, 0.35f, 0.02f); dbg_log("parry press t=%.3f (beats %.3f %.3f)", b->t, b->hit_t[0], a->hits > 1 ? b->hit_t[1] : 0.0f); }
        if (in->dodge && !b->dodging) { b->dodging = true; b->dodge_t = b->t; play_bound(pm, ANIM_DODGE, 0, 0.45f, 0.03f); audio_play(SND_WHIFF, 0.5f, 0.9f); }
        if (b->dodging && b->t > b->dodge_t + 0.45f) b->dodging = false;
        float window = W_GOOD * (b->wide_windows ? 1.5f : 1.0f);
        for (int i = 0; i < a->hits; i++) {
            if (b->hit_done[i]) continue;
            float th = b->hit_t[i];
            bool pressed_ok = a->parryable && !b->press_used && fabsf(b->parry_pressed_t - th) <= window;
            // Resolve at the beat if a press is already in, else at the late edge of the window.
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
    if (pm->loaded) { if (pm->is_sprite) { sprite_update(&pm->sprite, dt); charmodel_sprite_settle(pm); } else anim_update(&pm->player, &pm->model, dt); }
    if (bm->loaded) { if (bm->is_sprite) { sprite_update(&bm->sprite, dt); charmodel_sprite_settle(bm); } else anim_update(&bm->player, &bm->model, dt); }
    fx_update(b, dt);
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
static void wrap_text(Gfx *g, float x, float y, float maxw, float sc, Vec4 col, const char *s) __attribute__((unused));
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
    // Rhythm read during the enemy attack: hit circles with approach rings, osu-style
    if (b->state == BT_ENEMY_TELL || b->state == BT_ENEMY_ATTACK || b->state == BT_ENEMY_RECOVER) {
        const EnemyAttack *a = &b->enemy.attacks[b->cur_attack];
        for (int i = 0; i < a->hits; i++) {
            float px, py; if (!uifx_project(vp, hit_world(b, i), &px, &py)) continue;
            float th = b->hit_t[i];
            float remain = b->state == BT_ENEMY_TELL ? (0.9f - b->t) + th : th - b->t;
            Vec4 col = a->parryable ? v4(1, 0.9f, 0.5f, 1) : v4(0.9f, 0.4f, 1, 1);
            if (!b->hit_done[i] && remain <= APPROACH && remain > -0.2f && !(b->state == BT_ENEMY_RECOVER)) {
                float k = clampf(remain / APPROACH, 0, 1);           // 1 far .. 0 on the beat
                float alpha = 1.0f - k * 0.5f;
                gfx_ui_disc(g, px, py, RING_R, v4(col.x * 0.25f, col.y * 0.25f, col.z * 0.25f, 0.75f * alpha));
                gfx_ui_ring(g, px, py, RING_R, 5, v4(col.x, col.y, col.z, alpha));
                char num[4]; snprintf(num, sizeof num, "%d", i + 1);
                gfx_ui_text_xf(g, px, py, 2.6f, 0, v4(1, 1, 1, alpha), num);
                float ar = RING_R + (RING_R * 2.6f) * k;             // approach ring
                gfx_ui_ring(g, px, py, ar, k < 0.1f ? 7 : 4, v4(col.x, col.y, col.z, 0.95f * alpha));
                if (!a->parryable) gfx_ui_text_xf(g, px, py + RING_R + 22, 1.3f, 0, v4(0.95f, 0.5f, 1, alpha), "SHIFT DODGE");
            }
            if (b->burst_t[i] >= 0 && b->burst_t[i] < 0.45f) {      // burst on judgement
                float k = b->burst_t[i] / 0.45f;
                Judge j = b->hit_judge[i];
                Vec4 c = j == J_PERFECT ? v4(1, 0.95f, 0.5f, 1) : j == J_GREAT ? v4(0.6f, 1, 0.7f, 1) : j == J_GOOD ? v4(0.7f, 0.85f, 1, 1) : v4(1, 0.35f, 0.3f, 1);
                gfx_ui_ring(g, px, py, RING_R + 120 * ease_in_out(k), 6 * (1 - k) + 1, v4(c.x, c.y, c.z, 1 - k));
                if (j == J_PERFECT) gfx_ui_ring(g, px, py, RING_R + 60 * k, 3, v4(1, 1, 1, 1 - k));
            }
        }
        // timing bar: where the last press fell relative to the beat
        if (b->judge_t >= 0 && b->judge_t < 1.6f && b->last_judge != J_NONE) {
            float bx = 640, by = 545, bw = 320;
            float alpha = 1.0f - clampf((b->judge_t - 1.0f) / 0.6f, 0, 1);
            gfx_ui_rect(g, bx - bw * 0.5f, by - 6, bw, 12, v4(0, 0, 0, 0.6f * alpha));
            gfx_ui_rect(g, bx - bw * 0.5f * (W_GOOD / W_GOOD), by - 6, bw, 12, v4(0.35f, 0.45f, 0.6f, 0.5f * alpha));
            gfx_ui_rect(g, bx - bw * 0.5f * (W_GREAT / W_GOOD), by - 6, bw * (W_GREAT / W_GOOD), 12, v4(0.35f, 0.65f, 0.45f, 0.6f * alpha));
            gfx_ui_rect(g, bx - bw * 0.5f * (W_PERFECT / W_GOOD), by - 6, bw * (W_PERFECT / W_GOOD), 12, v4(0.95f, 0.85f, 0.4f, 0.8f * alpha));
            float off = clampf(b->last_offset / W_GOOD, -1.1f, 1.1f);
            gfx_ui_rect(g, bx + off * bw * 0.5f - 3, by - 12, 6, 24, v4(1, 1, 1, alpha));
            text_c(g, bx - bw * 0.5f - 44, by - 5, 1.1f, v4(0.8f, 0.8f, 0.8f, alpha), "EARLY");
            text_c(g, bx + bw * 0.5f + 40, by - 5, 1.1f, v4(0.8f, 0.8f, 0.8f, alpha), "LATE");
        }
        text_c(g, 640, 740, 1.4f, dim, "CLICK / RMB / SPACE on the beat to deflect       SHIFT  dodge");
    }
    // Combo counter
    if (b->combo >= 2) {
        char cs[24]; snprintf(cs, sizeof cs, "%d COMBO", b->combo);
        float pulse = 1.0f + 0.25f * fmaxf(0, 1.0f - b->judge_t * 3.0f);
        gfx_ui_text_xf(g, 1100, 660, (1.8f + fminf(b->combo, 20) * 0.06f) * pulse, -0.06f, v4(1, 0.85f, 0.45f, 1), cs);
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
        text_c(g, 640, 556, 1.1f, dim, "pick up a card and release to play it on its target; drop it back into the hand to cancel");
    }
    // Drop targets while dragging
    if (b->dragging >= 0) {
        const CardDef *c = &b->cards[b->hand[b->dragging].def];
        float pulse = 0.6f + 0.4f * sinf(b->intent_pulse * 8.0f);
        if (c->kind == CK_ATTACK) {
            bool on = b->drop_target == 1;
            Vec4 col = on ? v4(1, 0.85f, 0.4f, 0.95f) : v4(1, 0.55f, 0.35f, 0.5f);
            // Curved arrow from the dragged card to the highlighted target
            const HandCard *h = &b->hand[b->dragging];
            float x0 = h->x, y0 = h->y - CARD_H * 0.5f * h->sc, x2 = b->enemy_sx, y2 = b->enemy_sy;
            float x1 = (x0 + x2) * 0.5f, y1 = fminf(y0, y2) - 180;
            float px = x0, py = y0;
            const int N = 28;
            for (int k = 1; on && k <= N; k++) {
                float t = (float)k / N, u = 1 - t;
                float x = u * u * x0 + 2 * u * t * x1 + t * t * x2, y = u * u * y0 + 2 * u * t * y1 + t * t * y2;
                if (k < N - 1) gfx_ui_disc(g, x, y, 4 + 5 * t, v4(col.x, col.y, col.z, col.w * (0.35f + 0.65f * t)));
                if (k == N) {   // arrowhead
                    float dx = x - px, dy = y - py, len = hypotf(dx, dy); if (len < 1e-3f) { dx = 0; dy = -1; len = 1; }
                    dx /= len; dy /= len; float nx = -dy, ny = dx, sz = 26;
                    float q[8] = { x + dx * sz, y + dy * sz, x + nx * sz * 0.6f, y + ny * sz * 0.6f, x - dx * sz * 0.3f, y - dy * sz * 0.3f, x - nx * sz * 0.6f, y - ny * sz * 0.6f };
                    gfx_ui_quad(g, q, col);
                }
                px = x; py = y;
            }
            gfx_ui_ring(g, b->enemy_sx, b->enemy_sy, on ? 175 : 165, on ? 6 : 3, v4(col.x, col.y, col.z, on ? 0.9f : 0.3f + 0.2f * pulse));
            text_c(g, b->enemy_sx, b->enemy_sy - 200, 1.4f, col, "RELEASE TO ATTACK");
        } else {
            bool on = b->drop_target == 2;
            Vec4 col = on ? v4(0.6f, 0.9f, 1, 0.9f) : v4(0.5f, 0.7f, 1, 0.35f + 0.2f * pulse);
            gfx_ui_ring(g, b->player_sx, b->player_sy, on ? 120 : 110, on ? 6 : 3, col);
            text_c(g, b->player_sx, b->player_sy - 145, 1.3f, col, "RELEASE TO USE");
        }
    }
    // Hand: cards drawn in their own rotated frames, hovered or dragged card last so it sits on top
    for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < b->nhand; i++) {
        const HandCard *h = &b->hand[i];
        bool top = b->hovered == i || b->dragging == i;
        if ((pass == 0) == top) continue;
        const CardDef *c = &b->cards[h->def];
        float a = h->phase == CP_PLAYING ? 1.0f - clampf(h->phase_t / 0.45f, 0, 1) : 1.0f;
        if (h->phase == CP_DISCARDING) a = 1.0f - clampf(h->phase_t / 0.5f, 0, 1);
        bool playable = b->state == BT_PLAYER && (h->phase == CP_HAND || h->phase == CP_DRAG) && can_play(b, i);
        Vec4 col = v4(c->color.x, c->color.y, c->color.z, a);
        float ca = cosf(h->rot), sa = sinf(h->rot), sc = h->sc;
        #define CQ(lx, ly, lw, lh, colr) do { float _q[8]; float _c[4][2] = {{(lx), (ly)}, {(lx) + (lw), (ly)}, {(lx) + (lw), (ly) + (lh)}, {(lx), (ly) + (lh)}}; \
            for (int _k = 0; _k < 4; _k++) { float _x = _c[_k][0] * sc, _y = _c[_k][1] * sc; _q[_k * 2] = h->x + _x * ca - _y * sa; _q[_k * 2 + 1] = h->y + _x * sa + _y * ca; } \
            gfx_ui_quad(g, _q, (colr)); } while (0)
        #define CT(lx, ly, tsc, colr, txt) do { float _x = (lx) * sc, _y = (ly) * sc; gfx_ui_text_xf(g, h->x + _x * ca - _y * sa, h->y + _x * sa + _y * ca, (tsc) * sc, h->rot, (colr), (txt)); } while (0)
        float L = -CARD_W * 0.5f, T = -CARD_H * 0.5f;
        CQ(L + 5, T + 7, CARD_W, CARD_H, v4(0, 0, 0, 0.5f * a));                                   // shadow
        CQ(L, T, CARD_W, CARD_H, v4(0.09f, 0.08f, 0.1f, 0.96f * a));                                // body
        CQ(L, T, CARD_W, 34, v4(col.x * 0.55f, col.y * 0.55f, col.z * 0.55f, a));                   // title band
        Vec4 fr = playable ? (top ? v4(1, 0.95f, 0.7f, a) : col) : v4(0.3f, 0.3f, 0.32f, a);
        CQ(L, T, CARD_W, 3, fr); CQ(L, T + CARD_H - 3, CARD_W, 3, fr); CQ(L, T, 3, CARD_H, fr); CQ(L + CARD_W - 3, T, 3, CARD_H, fr);
        CQ(L + 8, T + 6, 24, 24, playable ? v4(1, 0.85f, 0.35f, a) : v4(0.35f, 0.3f, 0.2f, a));    // cost
        { char s[8]; snprintf(s, sizeof s, "%d", c->cost); CT(L + 20, T + 18, 1.5f, v4(0.1f, 0.08f, 0.05f, a), s); }
        { char nm[32]; snprintf(nm, sizeof nm, "%s", c->name); for (char *p = nm; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32; CT(12, T + 18, 1.5f, v4(1, 1, 1, a), nm); }
        CQ(L + 16, T + 46, CARD_W - 32, 70, v4(col.x * 0.25f, col.y * 0.25f, col.z * 0.25f, a));   // art well
        if (c->kind == CK_ATTACK) for (int k = 0; k < c->hits; k++) CQ(-6 + (k - (c->hits - 1) * 0.5f) * 18, T + 61, 12, 40, col);
        else if (c->kind == CK_GUARD) { CQ(-18, T + 61, 36, 5, col); CQ(-18, T + 96, 36, 5, col); CQ(-18, T + 61, 5, 40, col); CQ(13, T + 61, 5, 40, col); }
        else CQ(-14, T + 67, 28, 28, col);
        // description, wrapped into up to three lines
        { char line[4][40] = {{0}}; int nl = 0; char word[40]; const char *p = c->desc; char cur[40] = "";
          while (*p && nl < 4) { int wl = 0; while (*p && *p != ' ' && wl < 39) word[wl++] = *p++; word[wl] = 0; while (*p == ' ') p++;
              char test[80]; snprintf(test, sizeof test, "%s%s%s", cur, cur[0] ? " " : "", word);
              if (gfx_ui_text_width(1.15f, test) > CARD_W - 24 && cur[0]) { snprintf(line[nl++], 40, "%s", cur); snprintf(cur, sizeof cur, "%s", word); }
              else snprintf(cur, sizeof cur, "%s", test); }
          if (cur[0] && nl < 4) snprintf(line[nl++], 40, "%s", cur);
          for (int k = 0; k < nl; k++) CT(0, T + 134 + k * 14, 1.15f, v4(0.9f, 0.9f, 0.88f, a), line[k]); }
        if (!playable && b->state == BT_PLAYER && h->phase == CP_HAND) CQ(L, T, CARD_W, CARD_H, v4(0, 0, 0, 0.45f * a));
        #undef CQ
        #undef CT
    }
    // Deck and discard piles
    { char s[16]; gfx_ui_rect(g, 1180, 690, 70, 96, v4(0.12f, 0.1f, 0.12f, 0.9f)); frame_rect(g, 1180, 690, 70, 96, 3, v4(0.5f, 0.45f, 0.35f, 1));
      snprintf(s, sizeof s, "%d", b->ndraw); text_c(g, 1215, 730, 2.2f, white, s); text_c(g, 1215, 758, 1.0f, dim, "DRAW"); }
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
        for (int i = 0; i < b->nhand; i++) if (b->hand[i].phase == CP_HAND && can_play(b, i)) { pick = i; break; }
        static float vx = 640, vy = 700;   // the bot's own cursor, persistent across ticks
        if (b->dragging >= 0) {
            // carry the cursor to the target, then let go
            const CardDef *c = &b->cards[b->hand[b->dragging].def];
            float tx = 640, ty = 430; (void)c;
            float dx = tx - vx, dy = ty - vy, d = hypotf(dx, dy);
            float step = 45.0f; if (d > step) { vx += dx / d * step; vy += dy / d * step; } else { vx = tx; vy = ty; }
            in->mouse_held = d > 30;
            *mx = vx; *my = vy;
            return;
        }
        if (pick >= 0) {
            vx = b->hand[pick].x; vy = b->hand[pick].y; *mx = vx; *my = vy; in->mouse_held = false;
            if (tick % 12 == 0 && b->hovered == pick) { in->click = true; in->mouse_held = true; }   // press to start the drag
        } else {
            bool dealing = false;
            for (int i = 0; i < b->nhand; i++) if (b->hand[i].phase == CP_DRAWING || b->hand[i].phase == CP_PLAYING) dealing = true;
            if (!dealing) { *mx = END_X + END_W * 0.5f; *my = END_Y + END_H * 0.5f; if (tick % 12 == 0 && b->end_hover) in->click = true; }
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
