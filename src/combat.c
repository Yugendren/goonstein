#include "combat.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *ANIM_NAMES[ANIM_COUNT] = {
    "idle", "walk", "attack", "parry", "parry_hit", "dodge", "hurt", "kneel", "dead", "roar", "stagger", "windup", "strike" };
const char *anim_name(Anim a) { return (a >= 0 && a < ANIM_COUNT) ? ANIM_NAMES[a] : "?"; }
Anim anim_from_name(const char *s) {
    for (int i = 0; i < ANIM_COUNT; i++) if (!strcmp(s, ANIM_NAMES[i])) return (Anim)i;
    return ANIM_IDLE;
}

void character_set_anim(Character *c, Anim a) {
    if (c->anim != a) { c->anim = a; c->anim_t = 0; }
}

static float randf(void) { return (float)rand() / (float)RAND_MAX; }
static float yaw_to(Vec3 from, Vec3 to) { return atan2f(to.x - from.x, to.z - from.z); }
static Vec3 forward(float yaw) { return v3(sinf(yaw), 0, cosf(yaw)); }

void character_separate(Character *a, Character *b, const Level *lv) {
    Vec3 d = v3_sub(b->pos, a->pos); d.y = 0;
    float dist = v3_len(d), min_d = a->radius + b->radius;
    if (dist >= min_d) return;
    if (dist < 0.01f) { d = v3(1, 0, 0); dist = 1; }
    Vec3 n = v3_scale(d, 1.0f / dist);
    float push = min_d - dist;
    float wa = b->radius / min_d, wb = a->radius / min_d;   // lighter one moves more
    a->pos = level_move(lv, a->pos, a->radius, a->height, v3_scale(n, -push * wa));
    b->pos = level_move(lv, b->pos, b->radius, b->height, v3_scale(n, push * wb));
}

bool character_in_arc(const Character *from, Vec3 target, float range) {
    Vec3 d = v3_sub(target, from->pos); d.y = 0;
    float dist = v3_len(d);
    if (dist > range) return false;
    if (dist < 0.01f) return true;
    float cosang = v3_dot(v3_scale(d, 1.0f / dist), forward(from->yaw));
    return cosang > cosf(50.0f * DEG2RAD);
}

void character_script_move(Character *c, Vec3 to, float dur) {
    c->move_from = c->pos; c->move_to = to; c->move_t = 0; c->move_dur = dur;
    c->scripted_moving = true;
    if (dur <= 0.0f) { c->pos = to; c->scripted_moving = false; return; }
    c->yaw = yaw_to(c->pos, to);
    character_set_anim(c, ANIM_WALK);
}

void character_script_update(Character *c, float dt) {
    c->anim_t += dt;
    if (c->flash > 0) c->flash = fmaxf(0, c->flash - dt * 6);
    if (!c->scripted_moving) return;
    c->move_t += dt;
    float k = clampf(c->move_t / c->move_dur, 0, 1);
    Vec3 prev = c->pos;
    c->pos = v3_lerp(c->move_from, c->move_to, k);
    c->walk_phase += v3_len(v3_sub(c->pos, prev)) * 5.0f;
    if (k >= 1.0f) { c->scripted_moving = false; character_set_anim(c, ANIM_IDLE); }
}

// ---------------------------------------------------------------- data files

static char *next_line(char **cur) {
    if (!*cur || !**cur) return NULL;
    char *line = *cur;
    char *nl = strchr(line, '\n');
    if (nl) { *nl = 0; *cur = nl + 1; } else *cur = line + strlen(line);
    char *hash = strchr(line, '#');
    if (hash) *hash = 0;
    return line;
}

#define KEYF(k, dst) else if (!strcmp(key, k)) { dst = (float)atof(strtok(NULL, " \t")); }

bool boss_def_load(BossDef *d, const char *path) {
    size_t n; char *text = SDL_LoadFile(path, &n);
    if (!text) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "boss def missing: %s", path); return false; }
    BossDef o = { .hp = 100, .posture = 100, .posture_regen = 8, .stagger_time = 2.5f, .speed = 2.0f,
                  .attack_range = 2.2f, .think_min = 0.4f, .think_max = 1.2f, .phase2_hp = 0.5f,
                  .phase2_windup_mult = 0.75f, .stagger_damage_mult = 3.0f, .combo_chance = 0.35f,
                  .size = v3(1.2f, 2.6f, 1.2f), .color = v3(0.35f, 0.3f, 0.4f) };
    strcpy(o.name, "Boss");
    char *cur = text, *line; int ln = 0;
    while ((line = next_line(&cur))) {
        ln++;
        char *key = strtok(line, " \t"); if (!key) continue;
        if (!strcmp(key, "name")) { char *v = strtok(NULL, ""); if (v) { while (*v == ' ' || *v == '\t') v++; snprintf(o.name, sizeof o.name, "%s", v); } }
        KEYF("hp", o.hp) KEYF("posture", o.posture) KEYF("posture_regen", o.posture_regen)
        KEYF("stagger_time", o.stagger_time) KEYF("speed", o.speed) KEYF("attack_range", o.attack_range)
        KEYF("think_min", o.think_min) KEYF("think_max", o.think_max) KEYF("phase2_hp", o.phase2_hp)
        KEYF("phase2_windup_mult", o.phase2_windup_mult) KEYF("stagger_damage_mult", o.stagger_damage_mult)
        KEYF("combo_chance", o.combo_chance)
        else if (!strcmp(key, "size") || !strcmp(key, "color")) {
            Vec3 v; v.x = (float)atof(strtok(NULL, " \t")); v.y = (float)atof(strtok(NULL, " \t")); v.z = (float)atof(strtok(NULL, " \t"));
            if (key[0] == 's') o.size = v; else o.color = v;
        }
        else if (!strcmp(key, "move")) {
            if (o.nmoves >= 16) continue;
            BossMove m = { .windup = 0.8f, .active = 0.15f, .recovery = 0.8f, .damage = 25, .range = 2.2f,
                           .parryable = true, .posture_on_parry = 25, .step = 0, .tell = v3(1, 0.3f, 0.2f), .weight = 1, .contact = 0.4f };
            char *nm = strtok(NULL, " \t"); if (!nm) continue;
            snprintf(m.name, sizeof m.name, "%s", nm);
            char *k;
            while ((k = strtok(NULL, " \t"))) {
                char *v = strtok(NULL, " \t"); if (!v) break;
                if (!strcmp(k, "windup")) m.windup = (float)atof(v);
                else if (!strcmp(k, "active")) m.active = (float)atof(v);
                else if (!strcmp(k, "recovery")) m.recovery = (float)atof(v);
                else if (!strcmp(k, "damage")) m.damage = (float)atof(v);
                else if (!strcmp(k, "range")) m.range = (float)atof(v);
                else if (!strcmp(k, "parry")) m.parryable = !strcmp(v, "yes") || !strcmp(v, "1");
                else if (!strcmp(k, "posture")) m.posture_on_parry = (float)atof(v);
                else if (!strcmp(k, "step")) m.step = (float)atof(v);
                else if (!strcmp(k, "weight")) m.weight = (float)atof(v);
                else if (!strcmp(k, "clip")) snprintf(m.clip, sizeof m.clip, "%s", v);
                else if (!strcmp(k, "contact")) m.contact = (float)atof(v);
                else if (!strcmp(k, "tell")) { m.tell.x = (float)atof(v); m.tell.y = (float)atof(strtok(NULL, " \t")); m.tell.z = (float)atof(strtok(NULL, " \t")); }
                else SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d unknown move key %s", path, ln, k);
            }
            o.moves[o.nmoves++] = m;
        }
        else SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d unknown key %s", path, ln, key);
    }
    SDL_free(text);
    if (o.nmoves == 0) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: boss has no moves", path); return false; }
    *d = o;
    return true;
}

bool player_def_load(PlayerDef *d, const char *path) {
    size_t n; char *text = SDL_LoadFile(path, &n);
    if (!text) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "player def missing: %s", path); return false; }
    PlayerDef o = { .hp = 100, .speed = 3.2f, .turn_speed = 14, .attack_windup = 0.18f, .attack_active = 0.12f,
                    .attack_recovery = 0.35f, .attack_damage = 8, .attack_range = 1.9f, .attack_posture = 6,
                    .parry_window = 0.15f, .parry_recovery = 0.35f, .parry_hitstop = 0.12f,
                    .dodge_time = 0.45f, .dodge_iframes = 0.3f, .dodge_dist = 3.0f, .hurt_time = 0.45f,
                    .size = v3(0.5f, 1.8f, 0.5f), .color = v3(0.55f, 0.5f, 0.45f) };
    char *cur = text, *line; int ln = 0;
    while ((line = next_line(&cur))) {
        ln++;
        char *key = strtok(line, " \t"); if (!key) continue;
        if (0) {}
        KEYF("hp", o.hp) KEYF("speed", o.speed) KEYF("turn_speed", o.turn_speed)
        KEYF("attack_windup", o.attack_windup) KEYF("attack_active", o.attack_active) KEYF("attack_recovery", o.attack_recovery)
        KEYF("attack_damage", o.attack_damage) KEYF("attack_range", o.attack_range) KEYF("attack_posture", o.attack_posture)
        KEYF("parry_window", o.parry_window) KEYF("parry_recovery", o.parry_recovery) KEYF("parry_hitstop", o.parry_hitstop)
        KEYF("dodge_time", o.dodge_time) KEYF("dodge_iframes", o.dodge_iframes) KEYF("dodge_dist", o.dodge_dist)
        KEYF("hurt_time", o.hurt_time)
        else if (!strcmp(key, "size") || !strcmp(key, "color")) {
            Vec3 v; v.x = (float)atof(strtok(NULL, " \t")); v.y = (float)atof(strtok(NULL, " \t")); v.z = (float)atof(strtok(NULL, " \t"));
            if (key[0] == 's') o.size = v; else o.color = v;
        }
        else SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d unknown key %s", path, ln, key);
    }
    SDL_free(text);
    *d = o;
    return true;
}

// ---------------------------------------------------------------- player

void player_init(Player *p, const PlayerDef *d, Vec3 pos, float yaw) {
    memset(p, 0, sizeof *p);
    p->def = *d;
    p->c.radius = d->size.x * 0.5f; p->c.height = d->size.y;
    player_reset(p, pos, yaw);
}

void player_reset(Player *p, Vec3 pos, float yaw) {
    Character *c = &p->c;
    c->pos = pos; c->yaw = yaw;
    c->hp = c->hp_max = p->def.hp;
    c->posture = c->posture_max = 100;
    c->anim = ANIM_IDLE; c->anim_t = 0; c->flash = 0; c->tell = 0; c->scripted_moving = false;
    p->state = PS_FREE; p->t = 0; p->hit_applied = false;
}

static void player_enter(Player *p, PState s, Anim a) {
    p->state = s; p->t = 0; p->hit_applied = false;
    character_set_anim(&p->c, a);
}

void player_update(Player *p, const Input *in, Vec3 move_dir, const Level *lv, Boss *boss, float dt, CombatEvents *ev) {
    Character *c = &p->c; const PlayerDef *d = &p->def;
    c->anim_t += dt; p->t += dt;
    if (c->flash > 0) c->flash = fmaxf(0, c->flash - dt * 6);

    if (p->state == PS_SCRIPTED) { character_script_update(c, dt); return; }
    if (p->state == PS_DEAD) return;

    float mlen = v3_len(move_dir);
    if (mlen > 1) { move_dir = v3_scale(move_dir, 1.0f / mlen); mlen = 1; }

    switch (p->state) {
    case PS_FREE: {
        if (in->parry)  { player_enter(p, PS_PARRY, ANIM_PARRY); break; }
        if (in->attack) { player_enter(p, PS_ATTACK, ANIM_ATTACK); ev->player_swing = true;
                          if (boss && mlen < 0.1f) c->yaw = yaw_to(c->pos, boss->c.pos); break; }
        if (in->dodge)  { p->dodge_dir = mlen > 0.1f ? move_dir : v3_scale(forward(c->yaw), -1);
                          player_enter(p, PS_DODGE, ANIM_DODGE); ev->player_swing = false; break; }
        if (mlen > 0.05f) {
            float target_yaw = atan2f(move_dir.x, move_dir.z);
            c->yaw = angle_damp(c->yaw, target_yaw, d->turn_speed, dt);
            Vec3 delta = v3_scale(move_dir, d->speed * dt);
            Vec3 prev = c->pos;
            c->pos = level_move(lv, c->pos, c->radius, c->height, delta);
            float moved = v3_len(v3_sub(c->pos, prev));
            c->walk_phase += moved * 5.0f;
            p->step_timer += moved;
            if (p->step_timer > 0.85f) { p->step_timer = 0; ev->footstep = true; }
            character_set_anim(c, ANIM_WALK);
        } else {
            character_set_anim(c, ANIM_IDLE);
            p->step_timer = 0.5f;
        }
    } break;

    case PS_ATTACK: {
        float t = p->t;
        if (t >= d->attack_windup && !p->hit_applied) {
            p->hit_applied = true;
            if (boss && boss->state != BS_DEAD && character_in_arc(c, boss->c.pos, d->attack_range + boss->c.radius)) {
                float mult = boss->state == BS_STAGGER ? boss->def.stagger_damage_mult : 1.0f;
                boss->c.hp -= d->attack_damage * mult;
                boss->c.posture -= d->attack_posture;
                boss->regen_delay = 2.0f;
                boss->c.flash = 1.0f;
                ev->boss_hit = true; ev->hitstop = fmaxf(ev->hitstop, mult > 1 ? 0.08f : 0.04f); ev->shake = fmaxf(ev->shake, 0.15f);
                if (boss->c.hp <= 0) { boss->c.hp = 0; boss->state = BS_DEAD; boss->t = 0; character_set_anim(&boss->c, ANIM_DEAD); ev->boss_died = true; }
            }
        }
        // small forward step during the swing
        if (t < d->attack_windup + d->attack_active) c->pos = level_move(lv, c->pos, c->radius, c->height, v3_scale(forward(c->yaw), 1.2f * dt));
        if (t >= d->attack_windup + d->attack_active + d->attack_recovery) player_enter(p, PS_FREE, ANIM_IDLE);
        // buffered follow-up
        else if (t > d->attack_windup + d->attack_active + d->attack_recovery * 0.6f) {
            if (in->attack) { player_enter(p, PS_ATTACK, ANIM_ATTACK); ev->player_swing = true; }
            else if (in->parry) player_enter(p, PS_PARRY, ANIM_PARRY);
            else if (in->dodge) { p->dodge_dir = mlen > 0.1f ? move_dir : v3_scale(forward(c->yaw), -1); player_enter(p, PS_DODGE, ANIM_DODGE); }
        }
    } break;

    case PS_PARRY: {
        // Window is checked by the boss when its hit lands (p->t <= parry_window).
        if (p->t >= d->parry_window + d->parry_recovery) player_enter(p, PS_FREE, ANIM_IDLE);
    } break;

    case PS_DODGE: {
        float k = p->t / d->dodge_time;
        float sp = (1.0f - k) * 2.0f * d->dodge_dist / d->dodge_time;  // decelerating
        c->pos = level_move(lv, c->pos, c->radius, c->height, v3_scale(p->dodge_dir, sp * dt));
        if (p->t >= d->dodge_time) player_enter(p, PS_FREE, ANIM_IDLE);
    } break;

    case PS_HURT: {
        if (p->t >= d->hurt_time) player_enter(p, PS_FREE, ANIM_IDLE);
    } break;

    default: break;
    }
}

// ---------------------------------------------------------------- boss

void boss_init(Boss *b, const BossDef *d, Vec3 pos, float yaw) {
    memset(b, 0, sizeof *b);
    b->def = *d;
    b->c.radius = d->size.x * 0.5f; b->c.height = d->size.y;
    boss_reset(b, pos, yaw);
}

void boss_reset(Boss *b, Vec3 pos, float yaw) {
    Character *c = &b->c;
    c->pos = pos; c->yaw = yaw;
    c->hp = c->hp_max = b->def.hp;
    c->posture = c->posture_max = b->def.posture;
    c->anim = ANIM_IDLE; c->anim_t = 0; c->flash = 0; c->tell = 0; c->scripted_moving = false;
    b->state = BS_IDLE; b->t = 0; b->move = 0; b->last_move = -1; b->think = 0.8f; b->phase2 = false; b->regen_delay = 0;
}

static void boss_enter(Boss *b, BState s, Anim a) {
    b->state = s; b->t = 0; b->hit_applied = false;
    character_set_anim(&b->c, a);
}

static int boss_pick_move(Boss *b, const Player *p) {
    const BossDef *d = &b->def;
    float dist = v3_len(v3_sub(p->c.pos, b->c.pos));
    float total = 0;
    float w[16];
    for (int i = 0; i < d->nmoves; i++) {
        w[i] = d->moves[i].weight;
        if (i == b->last_move) w[i] *= 0.35f;                    // avoid repeats
        if (dist > d->moves[i].range + d->moves[i].step + 0.5f) w[i] *= 0.25f; // prefer moves that reach
        total += w[i];
    }
    float r = randf() * total;
    for (int i = 0; i < d->nmoves; i++) { r -= w[i]; if (r <= 0) return i; }
    return d->nmoves - 1;
}

static void boss_land_hit(Boss *b, Player *p, const BossMove *m, CombatEvents *ev) {
    const PlayerDef *pd = &p->def;
    if (p->state == PS_DEAD) return;
    if (p->state == PS_DODGE && p->t <= pd->dodge_iframes) return;                     // dodged
    if (p->state == PS_PARRY && p->t <= pd->parry_window && m->parryable) {           // parried
        b->c.posture -= m->posture_on_parry;
        b->regen_delay = 3.0f;
        b->c.flash = 0.6f;
        ev->parried = true; ev->hitstop = fmaxf(ev->hitstop, pd->parry_hitstop); ev->shake = fmaxf(ev->shake, 0.35f);
        character_set_anim(&p->c, ANIM_PARRY_HIT);
        p->t = pd->parry_window;  // short recovery only
        return;
    }
    p->c.hp -= m->damage;
    p->c.flash = 1.0f;
    ev->player_hit = true; ev->hitstop = fmaxf(ev->hitstop, 0.06f); ev->shake = fmaxf(ev->shake, 0.5f);
    if (p->c.hp <= 0) { p->c.hp = 0; player_enter(p, PS_DEAD, ANIM_DEAD); ev->player_died = true; }
    else player_enter(p, PS_HURT, ANIM_HURT);
}

void boss_update(Boss *b, Player *p, const Level *lv, float dt, CombatEvents *ev) {
    Character *c = &b->c; const BossDef *d = &b->def;
    c->anim_t += dt; b->t += dt;
    if (c->flash > 0) c->flash = fmaxf(0, c->flash - dt * 5);

    if (b->state == BS_SCRIPTED) { character_script_update(c, dt); return; }
    if (b->state == BS_DEAD) { c->tell = 0; return; }

    // Posture regen and stagger
    if (b->regen_delay > 0) b->regen_delay -= dt;
    else if (b->state != BS_STAGGER) c->posture = fminf(c->posture_max, c->posture + d->posture_regen * dt);
    if (c->posture <= 0 && b->state != BS_STAGGER) {
        c->posture = 0;
        boss_enter(b, BS_STAGGER, ANIM_STAGGER);
        c->tell = 0;
        ev->boss_staggered = true; ev->shake = fmaxf(ev->shake, 0.6f); ev->hitstop = fmaxf(ev->hitstop, 0.1f);
    }
    if (!b->phase2 && c->hp <= c->hp_max * d->phase2_hp) { b->phase2 = true; ev->phase2 = true; }

    Vec3 to_player = v3_sub(p->c.pos, c->pos); to_player.y = 0;
    float dist = v3_len(to_player);
    float windup_mult = b->phase2 ? d->phase2_windup_mult : 1.0f;
    const BossMove *m = &d->moves[b->move];

    switch (b->state) {
    case BS_IDLE:
        c->yaw = angle_damp(c->yaw, yaw_to(c->pos, p->c.pos), 6, dt);
        b->think -= dt;
        if (p->state == PS_DEAD) { character_set_anim(c, ANIM_IDLE); break; }
        if (b->think <= 0) {
            if (dist > d->attack_range) boss_enter(b, BS_APPROACH, ANIM_WALK);
            else { b->move = boss_pick_move(b, p); boss_enter(b, BS_WINDUP, ANIM_WINDUP); c->move_id = b->move; c->tell_color = d->moves[b->move].tell; }
        }
        break;

    case BS_APPROACH: {
        c->yaw = angle_damp(c->yaw, yaw_to(c->pos, p->c.pos), 8, dt);
        Vec3 step = v3_scale(forward(c->yaw), d->speed * (b->phase2 ? 1.25f : 1.0f) * dt);
        Vec3 prev = c->pos;
        c->pos = level_move(lv, c->pos, c->radius, c->height, step);
        c->walk_phase += v3_len(v3_sub(c->pos, prev)) * 3.0f;
        if (fmodf(c->walk_phase, PI) < 0.1f && b->t > 0.1f) ev->boss_footstep = true;
        if (dist <= d->attack_range) { b->move = boss_pick_move(b, p); boss_enter(b, BS_WINDUP, ANIM_WINDUP); c->move_id = b->move; c->tell_color = d->moves[b->move].tell; }
        else if (b->t > 4.0f) { boss_enter(b, BS_IDLE, ANIM_IDLE); b->think = 0.2f; }
    } break;

    case BS_WINDUP: {
        float w = m->windup * windup_mult;
        // Track the player for the first 60% of the windup, then commit.
        if (b->t < w * 0.6f) c->yaw = angle_damp(c->yaw, yaw_to(c->pos, p->c.pos), 10, dt);
        c->tell = clampf(b->t / w, 0, 1);
        if (b->t >= w) { boss_enter(b, BS_ACTIVE, ANIM_STRIKE); c->tell = 1; ev->boss_swing = true; }
    } break;

    case BS_ACTIVE: {
        if (m->step > 0) {
            float sp = m->step / m->active;
            c->pos = level_move(lv, c->pos, c->radius, c->height, v3_scale(forward(c->yaw), sp * dt));
        }
        // The hit lands at the midpoint of the active window.
        if (!b->hit_applied && b->t >= m->active * 0.5f) {
            b->hit_applied = true;
            if (character_in_arc(c, p->c.pos, m->range + p->c.radius)) boss_land_hit(b, p, m, ev);
        }
        if (b->t >= m->active) { boss_enter(b, BS_RECOVER, ANIM_IDLE); c->tell = 0; }
    } break;

    case BS_RECOVER:
        c->tell = fmaxf(0, c->tell - dt * 3);
        if (b->t >= m->recovery) {
            b->last_move = b->move;
            float combo = d->combo_chance * (b->phase2 ? 1.5f : 1.0f);
            if (p->state != PS_DEAD && dist <= d->attack_range + 0.5f && randf() < combo) {
                b->move = boss_pick_move(b, p); boss_enter(b, BS_WINDUP, ANIM_WINDUP);
                c->move_id = b->move; c->tell_color = d->moves[b->move].tell;
            } else {
                boss_enter(b, BS_IDLE, ANIM_IDLE);
                b->think = lerpf(d->think_min, d->think_max, randf()) * (b->phase2 ? 0.7f : 1.0f);
            }
        }
        break;

    case BS_STAGGER:
        if (b->t >= d->stagger_time) {
            c->posture = c->posture_max;
            boss_enter(b, BS_IDLE, ANIM_ROAR);
            b->think = 1.0f;
        }
        break;

    default: break;
    }
}
