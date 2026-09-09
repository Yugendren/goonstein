#include "combat.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *ANIM_NAMES[ANIM_COUNT] = {
    "idle", "walk", "attack", "parry", "parry_hit", "dodge", "hurt", "kneel", "dead", "roar", "stagger", "windup", "strike", "run", "attack2", "attack3",
    "sprint", "block", "hurt_head", "hurt_heavy", "attack_run" };
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
    if (!c->scripted_moving) { c->speed = damp(c->speed, 0, 10, dt); return; }
    c->move_t += dt;
    float k = clampf(c->move_t / c->move_dur, 0, 1);
    Vec3 prev = c->pos;
    c->pos = v3_lerp(c->move_from, c->move_to, k);
    float moved = v3_len(v3_sub(c->pos, prev));
    c->walk_phase += moved * 5.0f;
    c->speed = damp(c->speed, moved / fmaxf(dt, 1e-5f), 10, dt);
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
    PlayerDef o = { .hp = 100, .speed = 3.2f, .sprint_mult = 1.6f, .turn_speed = 14, .attack_windup = 0.18f, .attack_active = 0.12f,
                    .attack_recovery = 0.35f, .attack_damage = 8, .attack_range = 1.9f, .attack_posture = 6,
                    .parry_window = 0.15f, .parry_recovery = 0.35f, .parry_hitstop = 0.12f,
                    .dodge_time = 0.45f, .dodge_iframes = 0.3f, .dodge_dist = 3.0f, .hurt_time = 0.45f,
                    .posture = 100, .posture_regen = 26, .posture_delay = 0.9f, .block_posture = 0.85f,
                    .deflect_posture = 4, .stagger_time = 1.1f, .knockback = 2.2f,
                    .size = v3(0.5f, 1.8f, 0.5f), .color = v3(0.55f, 0.5f, 0.45f) };
    char *cur = text, *line; int ln = 0;
    while ((line = next_line(&cur))) {
        ln++;
        char *key = strtok(line, " \t"); if (!key) continue;
        if (0) {}
        KEYF("hp", o.hp) KEYF("speed", o.speed) KEYF("sprint_mult", o.sprint_mult) KEYF("turn_speed", o.turn_speed)
        KEYF("attack_windup", o.attack_windup) KEYF("attack_active", o.attack_active) KEYF("attack_recovery", o.attack_recovery)
        KEYF("attack_damage", o.attack_damage) KEYF("attack_range", o.attack_range) KEYF("attack_posture", o.attack_posture)
        KEYF("parry_window", o.parry_window) KEYF("parry_recovery", o.parry_recovery) KEYF("parry_hitstop", o.parry_hitstop)
        KEYF("dodge_time", o.dodge_time) KEYF("dodge_iframes", o.dodge_iframes) KEYF("dodge_dist", o.dodge_dist)
        KEYF("hurt_time", o.hurt_time)
        KEYF("posture", o.posture) KEYF("posture_regen", o.posture_regen) KEYF("posture_delay", o.posture_delay)
        KEYF("block_posture", o.block_posture) KEYF("deflect_posture", o.deflect_posture)
        KEYF("stagger_time", o.stagger_time) KEYF("knockback", o.knockback)
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
    float lead[PLAYER_SWINGS]; memcpy(lead, p->swing_lead, sizeof lead);   // clip timings survive a reset
    c->pos = pos; c->yaw = yaw;
    c->hp = c->hp_max = p->def.hp;
    c->posture = c->posture_max = p->def.posture > 0 ? p->def.posture : 100;
    c->anim = ANIM_IDLE; c->anim_t = 0; c->flash = 0; c->tell = 0; c->speed = 0; c->scripted_moving = false;
    p->state = PS_FREE; p->t = 0; p->hit_applied = false;
    p->combo = 0; p->sprint_t = 0; p->guarding = false; p->staggered = false;
    p->buf_attack = p->buf_parry = p->buf_dodge = 0; p->regen_delay = 0; p->iframes = 0; p->knock = v3(0, 0, 0);
    memcpy(p->swing_lead, lead, sizeof lead);
}

static void player_enter(Player *p, PState s, Anim a) {
    p->state = s; p->t = 0; p->hit_applied = false; p->guarding = false; p->staggered = false;
    character_set_anim(&p->c, a);
}

// The chain, per swing: A, B, the heavy finisher C, and the sprint attack. Timings come off the
// animation (swing_lead), everything else is a multiplier on the player def so one file still tunes it.
static const float SWING_ACTIVE[PLAYER_SWINGS] = { 1.0f, 1.0f, 1.15f, 1.1f };   // x def.attack_active
static const float SWING_RECOV [PLAYER_SWINGS] = { 0.85f, 0.9f, 1.35f, 1.1f };  // x def.attack_recovery
static const float SWING_DAMAGE[PLAYER_SWINGS] = { 1.0f, 1.0f, 1.6f, 1.4f };    // x def.attack_damage
static const float SWING_STEP  [PLAYER_SWINGS] = { 1.8f, 1.6f, 2.4f, 6.0f };    // m/s of root motion

static void player_swing(Player *p, int swing, Boss *boss, float mlen, CombatEvents *ev) {
    static const Anim A[PLAYER_SWINGS] = { ANIM_ATTACK, ANIM_ATTACK2, ANIM_ATTACK3, ANIM_ATTACK_RUN };
    const PlayerDef *d = &p->def;
    float lead = p->swing_lead[swing] > 0.001f ? p->swing_lead[swing] : d->attack_windup;
    player_enter(p, PS_ATTACK, A[swing]);
    p->combo = swing;
    p->atk_lead = clampf(lead, 0.12f, 0.34f);         // the blade lands on the clip's contact frame
    p->atk_active = d->attack_active * SWING_ACTIVE[swing];
    p->atk_recovery = d->attack_recovery * SWING_RECOV[swing];
    p->atk_damage = d->attack_damage * SWING_DAMAGE[swing];
    p->atk_step = SWING_STEP[swing];
    p->buf_attack = 0;
    ev->player_swing = true;
    if (boss && boss->state != BS_DEAD && mlen < 0.1f) p->c.yaw = yaw_to(p->c.pos, boss->c.pos);
}

static void player_dodge(Player *p, Vec3 move_dir, float mlen) {
    p->dodge_dir = mlen > 0.1f ? move_dir : v3_scale(forward(p->c.yaw), -1);
    player_enter(p, PS_DODGE, ANIM_DODGE);
    p->iframes = p->def.dodge_iframes;
    p->buf_dodge = 0;
}

// Right mouse tapped: the deflect window opens at the moment of the press, not at the tick boundary.
static void player_guard(Player *p, float press_age, float dt) {
    player_enter(p, PS_PARRY, ANIM_PARRY);
    p->t = clampf(press_age, 0, dt);
    p->buf_parry = 0;
}

static void player_posture_hit(Player *p, float amount, CombatEvents *ev) {
    p->c.posture -= amount;
    p->regen_delay = p->def.posture_delay;
    if (p->c.posture > 0) return;
    p->c.posture = p->c.posture_max;                  // the break resets the bar, the stagger is the cost
    player_enter(p, PS_HURT, ANIM_STAGGER);
    p->staggered = true;
    p->knock = v3(0, 0, 0);
    ev->player_staggered = true;
    ev->hitstop = fmaxf(ev->hitstop, 0.09f); ev->shake = fmaxf(ev->shake, 0.5f);
}

void player_update(Player *p, const Input *in, Vec3 move_dir, const Level *lv, Boss *boss, float dt, CombatEvents *ev) {
    Character *c = &p->c; const PlayerDef *d = &p->def;
    c->anim_t += dt; p->t += dt;
    if (c->flash > 0) c->flash = fmaxf(0, c->flash - dt * 6);

    if (p->state == PS_SCRIPTED) { character_script_update(c, dt); c->speed = damp(c->speed, 0, 10, dt); return; }
    if (p->state == PS_DEAD) { c->speed = 0; return; }

    float mlen = v3_len(move_dir);
    if (mlen > 1) { move_dir = v3_scale(move_dir, 1.0f / mlen); mlen = 1; }

    // Input buffer: a press stays queued for INPUT_BUFFER seconds, so a swing asked for during
    // recovery comes out the moment the window opens instead of being eaten.
    p->buf_attack = fmaxf(0, p->buf_attack - dt);
    p->buf_parry  = fmaxf(0, p->buf_parry - dt);
    p->buf_dodge  = fmaxf(0, p->buf_dodge - dt);
    if (in->attack) p->buf_attack = INPUT_BUFFER;
    if (in->parry)  p->buf_parry = INPUT_BUFFER;
    if (in->dodge)  p->buf_dodge = INPUT_BUFFER;
    p->sprint_t = in->sprint ? p->sprint_t + dt : 0;
    if (p->iframes > 0) p->iframes = fmaxf(0, p->iframes - dt);

    // Posture regen: standing free or holding guard, and only once the last hit has stopped ringing.
    if (p->regen_delay > 0) p->regen_delay -= dt;
    else if (p->state == PS_FREE || (p->state == PS_PARRY && p->guarding))
        c->posture = fminf(c->posture_max, c->posture + d->posture_regen * (p->guarding ? 0.6f : 1.0f) * dt);

    // Knockback carries through whatever state you are in.
    if (v3_len(p->knock) > 0.02f) {
        c->pos = level_move(lv, c->pos, c->radius, c->height, v3_scale(p->knock, dt));
        p->knock = v3_scale(p->knock, expf(-9.0f * dt));
    } else p->knock = v3(0, 0, 0);

    bool sprinting = in->sprint && p->sprint_t > 0.25f && mlen > 0.05f;   // hold to sprint; a tap already dodged
    float want_speed = 0;

    switch (p->state) {
    case PS_FREE: {
        if (p->buf_parry > 0) { player_guard(p, in->parry ? in->parry_age : 0, dt); break; }
        if (p->buf_attack > 0) { player_swing(p, sprinting ? 3 : 0, boss, mlen, ev); break; }
        if (p->buf_dodge > 0) { player_dodge(p, move_dir, mlen); break; }
        if (in->rmouse_held) { player_guard(p, 0, dt); p->t = d->parry_window; break; }   // held from before: straight to block
        if (mlen > 0.05f) {
            float target_yaw = atan2f(move_dir.x, move_dir.z);
            c->yaw = angle_damp(c->yaw, target_yaw, d->turn_speed, dt);
            Vec3 delta = v3_scale(move_dir, d->speed * (sprinting ? d->sprint_mult : 1.0f) * dt);
            Vec3 prev = c->pos;
            c->pos = level_move(lv, c->pos, c->radius, c->height, delta);
            float moved = v3_len(v3_sub(c->pos, prev));
            c->walk_phase += moved * 5.0f;
            p->step_timer += moved;
            if (p->step_timer > (sprinting ? 1.1f : 0.85f)) { p->step_timer = 0; ev->footstep = true; }
            want_speed = moved / fmaxf(dt, 1e-5f);
            character_set_anim(c, sprinting ? ANIM_RUN : ANIM_WALK);
        } else {
            character_set_anim(c, ANIM_IDLE);
            p->step_timer = 0.5f;
        }
    } break;

    case PS_ATTACK: {
        float t = p->t, lead = p->atk_lead, act = p->atk_active, rec = p->atk_recovery;
        if (t >= lead && !p->hit_applied) {
            p->hit_applied = true;
            if (boss && boss->state != BS_DEAD && character_in_arc(c, boss->c.pos, d->attack_range + boss->c.radius)) {
                bool heavy = boss->state == BS_STAGGER;
                float mult = heavy ? boss->def.stagger_damage_mult : 1.0f;
                boss->c.hp -= p->atk_damage * mult;
                boss->c.posture -= d->attack_posture * SWING_DAMAGE[p->combo];
                boss->regen_delay = 2.0f;
                boss->c.flash = 1.0f;
                ev->contact = v3_add(c->pos, v3_scale(forward(c->yaw), 1.1f)); ev->contact.y += boss->c.height * 0.45f;
                ev->boss_hit = true;
                ev->hitstop = fmaxf(ev->hitstop, heavy || p->combo >= 2 ? 0.09f : 0.04f);
                ev->shake = fmaxf(ev->shake, 0.15f);
                if (boss->c.hp <= 0) { boss->c.hp = 0; boss->state = BS_DEAD; boss->t = 0; character_set_anim(&boss->c, ANIM_DEAD); ev->boss_died = true; }
            }
        }
        // Root motion: each swing carries you forward, hardest on the sprint attack. It stops
        // closing once the target is inside the blade, so the two do not end up standing in each other.
        float room = 1e9f;
        if (boss && boss->state != BS_DEAD) room = v3_len(v3_sub(boss->c.pos, c->pos)) - boss->c.radius - c->radius;
        if (t < lead + act && room > 0.35f) {
            float sp = p->atk_step * (t < lead ? 0.55f : 1.0f) * clampf(room, 0, 1);
            c->pos = level_move(lv, c->pos, c->radius, c->height, v3_scale(forward(c->yaw), sp * dt));
            want_speed = sp * 0.5f;
        }
        // Cancels open on the contact frame: deflect and dodge immediately, the next swing once the
        // blade has finished travelling.
        bool done = t >= lead + act + rec;
        if (p->hit_applied && p->buf_parry > 0) { player_guard(p, 0, dt); break; }
        if (p->hit_applied && p->buf_dodge > 0) { player_dodge(p, move_dir, mlen); break; }
        if (p->buf_attack > 0 && (t >= lead + act || done)) { player_swing(p, p->combo >= 2 ? 0 : p->combo + 1, boss, mlen, ev); break; }
        if (done) player_enter(p, PS_FREE, ANIM_IDLE);
    } break;

    case PS_PARRY: {
        if (boss && boss->state != BS_DEAD && mlen < 0.1f) c->yaw = angle_damp(c->yaw, yaw_to(c->pos, boss->c.pos), 8, dt);
        else if (mlen > 0.05f) c->yaw = angle_damp(c->yaw, atan2f(move_dir.x, move_dir.z), d->turn_speed * 0.4f, dt);
        if (!p->guarding && p->t >= d->parry_window) {
            bool clang = c->anim == ANIM_PARRY_HIT;    // a deflect just landed: let it read before settling
            if (in->rmouse_held && (!clang || p->t >= d->parry_window + 0.2f)) { p->guarding = true; character_set_anim(c, ANIM_BLOCK); }
            else if (!clang && !p->hit_applied) { p->hit_applied = true; ev->parry_whiff = true; }
        }
        if (p->guarding) {
            if (!in->rmouse_held) { player_enter(p, PS_FREE, ANIM_IDLE); break; }
            if (in->parry && p->t > 0.12f) { player_guard(p, in->parry_age, dt); break; }     // re-tap: fresh deflect window
            if (p->buf_attack > 0) { player_swing(p, 0, boss, mlen, ev); break; }
            if (p->buf_dodge > 0) { player_dodge(p, move_dir, mlen); break; }
        } else if (p->t >= d->parry_window + d->parry_recovery) player_enter(p, PS_FREE, ANIM_IDLE);
    } break;

    case PS_DODGE: {
        float k = p->t / d->dodge_time;
        float sp = (1.0f - k) * 2.0f * d->dodge_dist / d->dodge_time;  // decelerating
        c->pos = level_move(lv, c->pos, c->radius, c->height, v3_scale(p->dodge_dir, sp * dt));
        want_speed = sp * 0.4f;
        if (p->t > d->dodge_time * 0.6f && p->buf_attack > 0) { player_swing(p, 0, boss, mlen, ev); break; }
        if (p->t >= d->dodge_time) player_enter(p, PS_FREE, ANIM_IDLE);
    } break;

    case PS_HURT: {
        if (p->t >= (p->staggered ? d->stagger_time : d->hurt_time)) player_enter(p, PS_FREE, ANIM_IDLE);
    } break;

    default: break;
    }
    c->speed = damp(c->speed, want_speed, 12, dt);
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
    b->strafe_dir = 1; c->speed = 0;
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
    ev->contact = v3_add(p->c.pos, v3_scale(forward(p->c.yaw), 0.8f)); ev->contact.y += p->c.height * 0.62f;
    if (p->state == PS_DODGE && p->iframes > 0) return;                                // dodged
    bool guard = p->state == PS_PARRY;
    if (guard && !m->parryable) ev->parry_unblockable = true;                          // nothing stops a grab
    else if (guard && !p->guarding && p->t <= pd->parry_window) {                      // deflected
        b->c.posture -= m->posture_on_parry;
        b->regen_delay = 3.0f;
        b->c.flash = 0.6f;
        p->c.posture = fmaxf(1.0f, p->c.posture - pd->deflect_posture);
        p->regen_delay = pd->posture_delay * 0.5f;
        ev->parried = true; ev->hitstop = fmaxf(ev->hitstop, pd->parry_hitstop); ev->shake = fmaxf(ev->shake, 0.35f);
        character_set_anim(&p->c, ANIM_PARRY_HIT);
        p->t = pd->parry_window + pd->parry_recovery * 0.45f;   // short recovery only
        return;
    }
    else if (guard) {                                                                 // blocked: no damage, posture pays
        if (!p->guarding) ev->parry_early = true;                                     // tapped too early, caught it on the guard
        b->regen_delay = 1.0f;
        p->c.flash = 0.5f;
        p->knock = v3_scale(v3_sub(p->c.pos, b->c.pos), 0.8f); p->knock.y = 0;
        ev->player_blocked = true; ev->hitstop = fmaxf(ev->hitstop, 0.04f); ev->shake = fmaxf(ev->shake, 0.25f);
        player_posture_hit(p, m->damage * pd->block_posture, ev);
        return;
    }
    float dmg = m->damage * (p->state == PS_HURT && p->staggered ? 1.5f : 1.0f);       // a broken guard is a free hit
    p->c.hp -= dmg;
    p->c.flash = 1.0f;
    ev->player_hit = true;
    ev->hitstop = fmaxf(ev->hitstop, dmg >= 30 ? 0.09f : 0.04f);
    ev->shake = fmaxf(ev->shake, 0.5f);
    if (p->c.hp <= 0) { p->c.hp = 0; player_enter(p, PS_DEAD, ANIM_DEAD); ev->player_died = true; return; }
    // Hit reaction by size, with a shove on the heavy ones.
    Anim react = dmg < 20 ? ANIM_HURT : dmg < 32 ? ANIM_HURT_HEAD : ANIM_HURT_HEAVY;
    player_enter(p, PS_HURT, react);
    if (react == ANIM_HURT_HEAVY) {
        Vec3 away = v3_sub(p->c.pos, b->c.pos); away.y = 0;
        float len = v3_len(away);
        p->knock = len > 0.01f ? v3_scale(away, pd->knockback / len) : v3_scale(forward(b->c.yaw), pd->knockback);
    }
    p->c.posture = fmaxf(1.0f, p->c.posture - dmg * 0.5f);   // getting hit rattles you, but never breaks on its own
    p->regen_delay = pd->posture_delay;
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
    float want_speed = 0;

    switch (b->state) {
    case BS_IDLE:
        c->yaw = angle_damp(c->yaw, yaw_to(c->pos, p->c.pos), 6, dt);
        b->think -= dt;
        if (p->state == PS_DEAD) { character_set_anim(c, ANIM_IDLE); break; }
        // A broken guard is an invitation: stop thinking and swing.
        if (p->state == PS_HURT && p->staggered && dist <= d->attack_range + 0.6f) b->think = fminf(b->think, 0.12f);
        // Circle the player between moves so the pauses read as menace, not as a statue. A roar or
        // any other one-shot that brought us here is left alone to play out.
        bool loco = c->anim == ANIM_IDLE || c->anim == ANIM_WALK;
        if (loco && b->think > 0.15f && dist < d->attack_range + 2.5f && dist > 1.0f) {
            Vec3 right = v3(cosf(c->yaw), 0, -sinf(c->yaw));
            Vec3 want = v3_scale(right, b->strafe_dir);
            // hold its own reach while it circles: step in if the player drifts out, back off if they crowd in
            want = v3_add(want, v3_scale(forward(c->yaw), clampf(dist - d->attack_range * 0.9f, -1, 1) * 0.8f));
            Vec3 prev = c->pos;
            c->pos = level_move(lv, c->pos, c->radius, c->height, v3_scale(want, d->speed * 0.5f * dt));
            float moved = v3_len(v3_sub(c->pos, prev));
            c->walk_phase += moved * 3.0f;
            want_speed = moved / fmaxf(dt, 1e-5f);
            character_set_anim(c, ANIM_WALK);
        } else if (loco) character_set_anim(c, ANIM_IDLE);
        if (b->think <= 0) {
            b->strafe_dir = -b->strafe_dir;
            if (dist > d->attack_range) boss_enter(b, BS_APPROACH, ANIM_WALK);
            else { b->move = boss_pick_move(b, p); boss_enter(b, BS_WINDUP, ANIM_WINDUP); c->move_id = b->move; c->tell_color = d->moves[b->move].tell; }
        }
        break;

    case BS_APPROACH: {
        c->yaw = angle_damp(c->yaw, yaw_to(c->pos, p->c.pos), 8, dt);
        Vec3 step = v3_scale(forward(c->yaw), d->speed * (b->phase2 ? 1.25f : 1.0f) * dt);
        Vec3 prev = c->pos;
        c->pos = level_move(lv, c->pos, c->radius, c->height, step);
        float moved = v3_len(v3_sub(c->pos, prev));
        c->walk_phase += moved * 3.0f;
        want_speed = moved / fmaxf(dt, 1e-5f);
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
        if (m->step > 0 && dist > c->radius + p->c.radius + 0.3f) {   // lunge in, but do not walk through them
            float sp = m->step / m->active;
            c->pos = level_move(lv, c->pos, c->radius, c->height, v3_scale(forward(c->yaw), sp * dt));
            want_speed = sp * 0.3f;
        }
        // The hit lands at the midpoint of the active window.
        if (!b->hit_applied && b->t >= m->active * 0.5f) {
            b->hit_applied = true;
            if (character_in_arc(c, p->c.pos, m->range + p->c.radius)) boss_land_hit(b, p, m, ev);
        }
        // The attack clip was fitted over active + recovery, so recovery keeps playing it out.
        if (b->t >= m->active) { boss_enter(b, BS_RECOVER, ANIM_STRIKE); c->tell = 0; }
    } break;

    case BS_RECOVER: {
        c->tell = fmaxf(0, c->tell - dt * 3);
        // A broken guard is a free hit: cut the recovery short and chain straight into the punish.
        bool punish = p->state == PS_HURT && p->staggered && dist <= d->attack_range + 1.0f;
        if (b->t >= m->recovery * (punish ? 0.4f : 1.0f)) {
            b->last_move = b->move;
            float combo = punish ? 1.0f : d->combo_chance * (b->phase2 ? 1.5f : 1.0f);
            if (p->state != PS_DEAD && dist <= d->attack_range + 0.5f && randf() < combo) {
                b->move = boss_pick_move(b, p); boss_enter(b, BS_WINDUP, ANIM_WINDUP);
                c->move_id = b->move; c->tell_color = d->moves[b->move].tell;
            } else {
                boss_enter(b, BS_IDLE, ANIM_IDLE);
                b->think = lerpf(d->think_min, d->think_max, randf()) * (b->phase2 ? 0.7f : 1.0f);
            }
        }
    } break;

    case BS_STAGGER:
        if (b->t >= d->stagger_time) {
            c->posture = c->posture_max;
            boss_enter(b, BS_IDLE, ANIM_ROAR);
            b->think = 1.0f;
        }
        break;

    default: break;
    }
    c->speed = damp(c->speed, want_speed, 10, dt);
}
