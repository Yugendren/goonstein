#include "combat.h"
#include "terrain.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Quake's air-control trick: the wishspeed fed to the mid-air accel step is capped to this, so a
// jump can be steered a little but never run on, without needing a separate "no air movement" case.
#define AIR_WISH 0.9f

static const char *ANIM_NAMES[ANIM_COUNT] = {
    "idle", "walk", "attack", "parry", "parry_hit", "dodge", "hurt", "kneel", "dead", "roar", "stagger", "windup", "strike", "run", "attack2", "attack3",
    "sprint", "block", "hurt_head", "hurt_heavy", "attack_run",
    "down", "getup", "knocked", "gun_idle", "gun_fire", "gun_reload", "melee_idle", "melee_swing" };
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
    if (dur <= 0.0f) { c->pos = to; c->scripted_moving = false; c->ground_block = -1; c->grounded = false; return; }   // a skipped scene: this is a teleport, so their footing goes with it
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
    PlayerDef o = { .hp = 100, .speed = 3.2f, .sprint_mult = 1.6f, .turn_speed = 14,
                    .accel = 10, .air_accel = 10, .friction = 8, .stop_speed = 1.4f, .jump_height = 1.0f, .crouch_mult = 0.5f,
                    .sprint_ramp = 1.4f, .speed_cap = 8.8f, .coyote = 0.12f, .jump_buffer = 0.15f, .air_wish = 1.6f,
                    .slide_time = 0.8f, .slide_decel = 1.6f, .slide_enter = 4.6f, .slide_exit = 2.2f,
                    .mantle_min = 0.55f, .mantle_max = 2.2f, .vault_max = 1.2f, .roll_fall = 4.0f, .wallrun_time = 1.2f,
                    .attack_windup = 0.18f, .attack_active = 0.12f,
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
        KEYF("accel", o.accel) KEYF("air_accel", o.air_accel) KEYF("friction", o.friction) KEYF("stop_speed", o.stop_speed)
        KEYF("jump_height", o.jump_height) KEYF("crouch_mult", o.crouch_mult)
        KEYF("sprint_ramp", o.sprint_ramp) KEYF("speed_cap", o.speed_cap)
        KEYF("coyote", o.coyote) KEYF("jump_buffer", o.jump_buffer) KEYF("air_wish", o.air_wish)
        KEYF("slide_time", o.slide_time) KEYF("slide_decel", o.slide_decel)
        KEYF("slide_enter", o.slide_enter) KEYF("slide_exit", o.slide_exit)
        KEYF("mantle_min", o.mantle_min) KEYF("mantle_max", o.mantle_max) KEYF("vault_max", o.vault_max)
        KEYF("roll_fall", o.roll_fall) KEYF("wallrun_time", o.wallrun_time)
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
    // --- traversal --- a reset lands you standing still on the ground with no run built up
    p->trav = TM_NONE; p->trav_t = p->trav_dur = 0; p->trav_speed = 0; p->trav_arc = 0; p->wall_side = 0;
    p->momentum = 0; p->run_speed = 0; p->air_t = 0; p->buf_jump = 0; p->slide_cd = 0; p->last_vy = 0;
    c->hvel = v3(0, 0, 0); c->vy = 0; c->traversing = false; c->body_height = c->height;
    c->land_impact = c->land_drop = 0; c->fall_from = pos.y;
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

// ---------------------------------------------------------------- traversal
//
// Mirror's Edge on top of the Quake base: momentum that has to be built, a jump that forgives, and
// three scripted moves the world picks for you rather than a key.
//
// The whole section is a pure function of the inputs and the blocks under them. That is not a
// stylistic preference: it is what lets a client start a mantle on the frame it asks for one and
// the host replay the same intent against the same level and land on the same ledge, so
// reconciliation has nothing left to argue about (see netgame.c). Nothing is authored in the level
// either -- a ledge is any block top, deck or hillside between knee and head height with room to
// stand on, found by probing three columns in front of the feet.

static const char *TRAV_NAMES[] = { "none", "slide", "mantle", "vault", "roll", "wallrun" };
const char *trav_name(TravMode m) { return ((int)m >= 0 && (int)m < 6) ? TRAV_NAMES[m] : "?"; }

#define SLIDE_HEIGHT   0.55f    // fraction of standing height a slide fits through: a low gap is the point of it
#define SLIDE_STEER    1.1f     // m/s of wish speed left for aiming the slide; more and it is a run on your back
#define SLIDE_JUMP_KEEP 1.10f   // a slide jumped out of leaves with a tenth more speed than it had: the long hop
#define ROLL_TIME      0.55f
#define ROLL_KEEP      0.78f    // a hard landing costs a fifth of the run, not all of it
#define WALL_MIN_SPEED 4.6f
#define WALL_GRAVITY   0.22f    // fraction of gravity a wall run leaves you: it sags, it does not float
#define MANTLE_LIFT    0.62f    // fraction of the move spent going up before the forward half finishes
// Gravity and the ground contact are game.c's (game_ground_character runs right after
// player_update), so all a jump does here is hand back an upward vy sized to clear jump_height.
// This must match GAME_GRAVITY in game.c: jump_height is what is authored, the impulse is derived.
#define PLAYER_JUMP_GRAVITY 20.0f

float world_top(const World *w, float x, float z, float ceiling) {
    float best = -1e9f;
    if (w->tr && w->tr->present) { if (terrain_inside(w->tr, x, z)) best = terrain_height(w->tr, x, z); }
    else best = 0.0f;                   // a level with no terrain has a floor at zero, which is what game.c assumes
    if (best > ceiling) best = -1e9f;   // a hillside that keeps climbing past the head is a hill, not a ledge
    float b = level_top_at(w->lv, x, z, ceiling, NULL);
    return b > best ? b : best;
}

bool world_clear(const World *w, float x, float z, float radius, float y0, float y1) {
    if (!level_clear(w->lv, x, z, radius, y0, y1)) return false;
    // Standing room also means the ground is not already up there: a ledge cut into a hillside has
    // no block over it and would otherwise read as clear with a metre of dirt in the way.
    if (w->tr && w->tr->present && terrain_inside(w->tr, x, z) && terrain_height(w->tr, x, z) > y0 + 0.20f) return false;
    return true;
}

// What the probe found: the top it would climb to, and for a vault the ground on the far side.
typedef struct Ledge { float top; Vec3 at; bool vault; Vec3 land; } Ledge;

// Three columns in front of the feet: the ledge itself, half a metre past it (is this a ledge or
// the bottom of a wall), and the face in between (is there actually something in the way). A fourth
// is asked only for a vault: somewhere to come down on.
static bool traverse_probe(const Player *p, const World *w, Vec3 dir, float speed, Ledge *out) {
    const Character *c = &p->c; const PlayerDef *d = &p->def;
    float foot = c->pos.y, ceiling = foot + d->mantle_max;
    float reach = c->radius + 0.45f;
    Vec3 at = v3_add(c->pos, v3_scale(dir, reach));
    float top = world_top(w, at.x, at.z, ceiling + 0.02f);
    float h = top - foot;
    if (h < d->mantle_min || h > d->mantle_max) return false;
    if (!world_clear(w, at.x, at.z, c->radius * 0.8f, top + 0.08f, top + c->height * 0.85f)) return false;
    Vec3 past = v3_add(c->pos, v3_scale(dir, reach + 0.55f));
    if (world_top(w, past.x, past.z, top + 0.6f) > top + 0.30f) return false;   // it keeps going up: a wall
    Vec3 face = v3_add(c->pos, v3_scale(dir, c->radius + 0.12f));
    if (world_top(w, face.x, face.z, ceiling + 0.02f) < foot + d->mantle_min * 0.5f) return false;   // nothing in the way
    out->top = top; out->at = v3(at.x, top, at.z); out->vault = false; out->land = out->at;
    // Low and fast is gone over, not climbed onto -- but only if there is somewhere to land, or a
    // vault over a parapet drops you off the roof.
    if (h <= d->vault_max && speed >= 4.2f) {
        Vec3 land = v3_add(c->pos, v3_scale(dir, reach + 1.05f));
        float lt = world_top(w, land.x, land.z, top + 0.05f);
        if (lt > -1e8f && lt < top - 0.20f
            && world_clear(w, land.x, land.z, c->radius * 0.8f, lt + 0.08f, lt + c->height * 0.85f)) {
            out->vault = true; out->land = v3(land.x, lt, land.z);
        }
    }
    return true;
}

static void traverse_enter(Player *p, TravMode m, float dur, Vec3 to, Vec3 dir, float speed) {
    p->trav = m; p->trav_t = 0; p->trav_dur = dur;
    p->trav_from = p->c.pos; p->trav_to = to; p->trav_dir = dir; p->trav_speed = speed; p->trav_arc = 0;
    p->c.traversing = (m == TM_MANTLE || m == TM_VAULT || m == TM_WALLRUN);   // these three own pos.y themselves
    p->c.vy = 0;
}

// A correction from the host landed mid-climb: carry the path with it. Moving the body instead
// would be undone by the next tick of the lerp, and the eye would ring at the snapshot rate.
void player_traverse_shift(Player *p, Vec3 delta) {
    if (p->trav != TM_MANTLE && p->trav != TM_VAULT) return;
    p->trav_from = v3_add(p->trav_from, delta);
    p->trav_to = v3_add(p->trav_to, delta);
}

// Everything that can start a traversal, in the order they beat each other: a slide is a decision
// you made, a mantle is one the wall made for you, a wall run is what is left.
static void traverse_try(Player *p, const Input *in, Vec3 move_dir, float mlen, const World *w,
                         bool blocked, CombatEvents *ev) {
    Character *c = &p->c; const PlayerDef *d = &p->def;
    float speed = hypotf(c->hvel.x, c->hvel.z), carried = fmaxf(speed, p->run_speed);
    Vec3 dir = speed > 1.0f ? v3(c->hvel.x / speed, 0, c->hvel.z / speed)
                            : (mlen > 0.05f ? move_dir : v3(sinf(c->yaw), 0, cosf(c->yaw)));

    // Crouch at a sprint is a slide. The cooldown is what stops a held Ctrl from chaining slides
    // forever off the speed each one leaves behind.
    // `carried`, not the speed this instant: pressing Ctrl already halved the wish speed and took a
    // tick of friction, so the speed you asked to slide at is gone by the time this looks at it.
    if (c->grounded && in->crouch && p->slide_cd <= 0 && carried >= d->slide_enter && d->slide_time > 0) {
        traverse_enter(p, TM_SLIDE, d->slide_time, c->pos, dir, carried);
        p->n_slide++; ev->slide_start = true;
        character_set_anim(c, ANIM_SLIDE);
        return;
    }

    // A ledge: walked into while grounded, or jumped at. Standing still against a wall does nothing
    // -- the move belongs to the run, not to the wall.
    bool reaching = blocked || (!c->grounded && speed > 1.2f);
    if (reaching && (carried > 1.2f || mlen > 0.3f)) {
        Ledge l;
        if (traverse_probe(p, w, dir, carried, &l)) {
            if (l.vault) {
                traverse_enter(p, TM_VAULT, 0.28f + 0.08f * clampf((l.top - c->pos.y) / d->vault_max, 0, 1),
                               l.land, dir, carried);
                // Over the obstacle, not through it: the straight line from here to the far side
                // runs inside the thing being vaulted, so the path is bulged clear of its top.
                p->trav_arc = (l.top - fmaxf(c->pos.y, l.land.y)) + 0.18f;
                p->n_vault++; ev->vault = true;
                character_set_anim(c, ANIM_VAULT);
            } else {
                float k = clampf((l.top - c->pos.y - d->mantle_min) / fmaxf(d->mantle_max - d->mantle_min, 0.01f), 0, 1);
                Vec3 to = v3_add(v3(l.at.x, l.top, l.at.z), v3_scale(dir, 0.18f));
                traverse_enter(p, TM_MANTLE, lerpf(0.35f, 0.60f, k), to, dir, carried);
                p->n_mantle++; ev->mantle = true;
                character_set_anim(c, ANIM_MANTLE);
            }
            c->hvel = v3(0, 0, 0);
            return;
        }
    }

    // A wall run is the last thing tried and the first thing to give up: airborne, quick, jump still
    // held, a face alongside and nothing under the feet. Anything less and it fires on every corner.
    if (d->wallrun_time > 0 && !c->grounded && in->jump_held && carried >= WALL_MIN_SPEED && c->vy < 2.0f) {
        Vec3 n; float gap;
        float y0 = c->pos.y + 0.4f, y1 = c->pos.y + c->height * 0.9f;
        if (level_wall_near(w->lv, c->pos.x, c->pos.z, c->radius + 0.30f, y0, y1, &n, &gap)
            && world_top(w, c->pos.x, c->pos.z, c->pos.y - 0.6f) < c->pos.y - 1.2f) {
            Vec3 along = v3(-n.z, 0, n.x);
            float s = v3_dot(v3(c->hvel.x, 0, c->hvel.z), along);
            if (fabsf(s) >= WALL_MIN_SPEED * 0.8f) {
                if (s < 0) { along = v3_scale(along, -1); s = -s; }
                p->wall_normal = n;
                p->wall_side = v3_dot(v3_cross(v3(0, 1, 0), along), n) > 0 ? 1.0f : -1.0f;
                traverse_enter(p, TM_WALLRUN, d->wallrun_time, c->pos, along, s);
                p->n_wallrun++;
                character_set_anim(c, ANIM_WALLRUN);
            }
        }
    }
}

// A hard landing carried forward instead of stopped dead.
static void traverse_roll(Player *p, Vec3 dir, float speed, CombatEvents *ev) {
    traverse_enter(p, TM_ROLL, ROLL_TIME, p->c.pos, dir, fmaxf(speed, 3.0f));
    p->c.traversing = false;          // a roll is on the ground: gravity and the floor still apply
    p->n_roll++; ev->roll = true;
    character_set_anim(&p->c, ANIM_ROLL);
}

// One tick of whatever is already running. Returns with p->trav cleared when the move is over.
static void traverse_update(Player *p, const Input *in, Vec3 move_dir, float mlen, const World *w,
                            float dt, CombatEvents *ev) {
    Character *c = &p->c; const PlayerDef *d = &p->def;
    p->trav_t += dt;
    float k = clampf(p->trav_t / fmaxf(p->trav_dur, 1e-4f), 0, 1);

    switch (p->trav) {
    case TM_MANTLE: case TM_VAULT: {
        // Up first, then over. Both halves are smoothsteps, so the climb starts and ends with zero
        // vertical speed -- the eye rides pos.y straight out of here and a corner in this curve is
        // a corner you would see.
        float lift = p->trav == TM_MANTLE ? MANTLE_LIFT : 0.75f;
        float ky = smoothstep(clampf(k / lift, 0, 1));
        float kx = smoothstep(clampf((k - (1.0f - lift)) / lift, 0, 1));
        c->pos.x = lerpf(p->trav_from.x, p->trav_to.x, kx);
        c->pos.z = lerpf(p->trav_from.z, p->trav_to.z, kx);
        // (1 - cos) rather than a sine: it leaves and arrives with zero vertical speed, and a kink
        // in the eye's path at the moment a vault starts is exactly what a vault must not have.
        c->pos.y = lerpf(p->trav_from.y, p->trav_to.y, ky) + p->trav_arc * 0.5f * (1.0f - cosf(2.0f * PI * k));
        c->speed = p->trav_speed;
        if (k >= 1.0f) {
            // A climb costs you the run-up; a vault is the whole point of not stopping.
            float out = p->trav == TM_VAULT ? p->trav_speed : fminf(p->trav_speed * 0.55f, 3.6f);
            c->hvel = v3_scale(p->trav_dir, out);
            c->traversing = false; c->grounded = true; c->vy = 0;
            p->trav = TM_NONE; p->slide_cd = fmaxf(p->slide_cd, 0.1f);
        }
    } break;

    case TM_SLIDE: {
        float sp = fmaxf(0.0f, p->trav_speed - d->slide_decel * p->trav_t);
        // Steering, not driving: enough to aim at a gap, never enough to keep the slide alive.
        if (mlen > 0.05f) {
            Vec3 want = v3_scale(move_dir, SLIDE_STEER);
            Vec3 v = v3_add(v3_scale(p->trav_dir, sp), v3_scale(want, dt * 2.0f));
            float l = hypotf(v.x, v.z);
            if (l > 0.01f) p->trav_dir = v3(v.x / l, 0, v.z / l);
        }
        c->hvel = v3_scale(p->trav_dir, sp);
        Vec3 prev = c->pos;
        c->pos = level_move(w->lv, c->pos, c->radius, c->height * SLIDE_HEIGHT, v3_scale(c->hvel, dt));
        if (dt > 1e-5f) { c->hvel.x = (c->pos.x - prev.x) / dt; c->hvel.z = (c->pos.z - prev.z) / dt; }
        c->speed = sp;
        c->walk_phase += v3_len(v3_sub(c->pos, prev)) * 2.0f;
        if (mlen > 0.05f) c->yaw = angle_damp(c->yaw, atan2f(p->trav_dir.x, p->trav_dir.z), d->turn_speed * 0.5f, dt);
        // Jumping out of a slide is the long hop: the slide's speed, plus a tenth, plus the jump.
        bool jumped = p->buf_jump > 0 && c->grounded;
        // Ctrl let go under a low gap is a wish, not an order: the slide holds until there is
        // headroom, which is what makes a culvert passable rather than a place you get stuck.
        bool roof = !world_clear(w, c->pos.x, c->pos.z, c->radius * 0.9f, c->pos.y + 0.2f, c->pos.y + c->height * 0.95f);
        bool over = (p->trav_t >= p->trav_dur) || sp <= d->slide_exit || (!in->crouch && !roof);
        if (jumped || (over && !roof)) {
            float out = jumped ? fminf(sp * SLIDE_JUMP_KEEP, d->speed_cap) : sp;
            c->hvel = v3_scale(p->trav_dir, out);
            if (jumped) { c->vy = sqrtf(2.0f * PLAYER_JUMP_GRAVITY * d->jump_height) * 1.06f; c->grounded = false; p->buf_jump = 0; p->n_jump++; }
            p->trav = TM_NONE; p->slide_cd = 0.45f;
            character_set_anim(c, ANIM_IDLE);
        }
    } break;

    case TM_ROLL: {
        float sp = lerpf(p->trav_speed, p->trav_speed * ROLL_KEEP, smoothstep(k));
        c->hvel = v3_scale(p->trav_dir, sp);
        Vec3 prev = c->pos;
        c->pos = level_move(w->lv, c->pos, c->radius, c->height * 0.7f, v3_scale(c->hvel, dt));
        if (dt > 1e-5f) { c->hvel.x = (c->pos.x - prev.x) / dt; c->hvel.z = (c->pos.z - prev.z) / dt; }
        c->speed = sp;
        if (k >= 1.0f) { p->trav = TM_NONE; character_set_anim(c, ANIM_IDLE); }
    } break;

    case TM_WALLRUN: {
        // The wall holds you up and takes what it likes in return: a fraction of gravity, and the
        // run ends the moment the face does.
        c->vy -= PLAYER_JUMP_GRAVITY * WALL_GRAVITY * dt;
        c->hvel = v3_scale(p->trav_dir, p->trav_speed);
        Vec3 push = v3_scale(p->wall_normal, -0.6f);   // lean into it so a gap in the wall ends the run
        Vec3 prev = c->pos;
        c->pos = level_move(w->lv, c->pos, c->radius, c->height,
                            v3_add(v3_scale(c->hvel, dt), v3_scale(push, dt)));
        c->pos.y += c->vy * dt;
        if (dt > 1e-5f) { c->hvel.x = (c->pos.x - prev.x) / dt; c->hvel.z = (c->pos.z - prev.z) / dt; }
        c->speed = p->trav_speed;
        Vec3 n; float gap;
        bool still = level_wall_near(w->lv, c->pos.x, c->pos.z, c->radius + 0.42f,
                                     c->pos.y + 0.4f, c->pos.y + c->height * 0.9f, &n, &gap)
                     && v3_dot(n, p->wall_normal) > 0.7f;
        bool jumped = p->buf_jump > 0;
        if (jumped) {
            c->hvel = v3_add(v3_scale(p->trav_dir, p->trav_speed * 0.9f), v3_scale(p->wall_normal, 3.4f));
            c->vy = sqrtf(2.0f * PLAYER_JUMP_GRAVITY * d->jump_height) * 1.05f;
            p->buf_jump = 0; p->n_jump++; ev->wall_jump = true;
        }
        if (jumped || !still || !in->jump_held || p->trav_t >= p->trav_dur) {
            p->trav = TM_NONE; p->wall_side = 0; c->traversing = false;
            character_set_anim(c, ANIM_FALL);
        }
    } break;

    default: p->trav = TM_NONE; c->traversing = false; break;
    }
}

void player_update(Player *p, const Input *in, Vec3 move_dir, const World *w, Boss *boss, float dt, CombatEvents *ev) {
    Character *c = &p->c; const PlayerDef *d = &p->def;
    if (p->state != PS_FREE) c->hvel = v3(0, 0, 0);   // another state's root motion owns c->pos this tick; do not fight it with stale ground velocity, and land back in PS_FREE from a stand
    c->anim_t += dt; p->t += dt;
    if (c->flash > 0) c->flash = fmaxf(0, c->flash - dt * 6);

    // --- traversal --- A landing that happened while you were busy being hit is a landing that
    // already cost you; it must not still be waiting to turn into a roll when the state clears.
    if (p->state != PS_FREE) { c->land_impact = 0; c->land_drop = 0; }

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
    // --- traversal --- The jump is buffered like every other press and forgiven for a moment past
    // the edge, because the frame you left the deck on and the frame you meant to jump on are two
    // different frames about a third of the time.
    p->buf_jump = fmaxf(0, p->buf_jump - dt);
    if (in->jump) p->buf_jump = d->jump_buffer > 0 ? d->jump_buffer : 0.15f;
    if (p->slide_cd > 0) p->slide_cd = fmaxf(0, p->slide_cd - dt);
    if (c->grounded) p->air_t = 0; else p->air_t += dt;

    // Posture regen: standing free or holding guard, and only once the last hit has stopped ringing.
    if (p->regen_delay > 0) p->regen_delay -= dt;
    else if (p->state == PS_FREE || (p->state == PS_PARRY && p->guarding))
        c->posture = fminf(c->posture_max, c->posture + d->posture_regen * (p->guarding ? 0.6f : 1.0f) * dt);

    // Knockback carries through whatever state you are in.
    if (v3_len(p->knock) > 0.02f) {
        c->pos = level_move(w->lv, c->pos, c->radius, c->height, v3_scale(p->knock, dt));
        p->knock = v3_scale(p->knock, expf(-9.0f * dt));
    } else p->knock = v3(0, 0, 0);

    bool sprinting = in->sprint && p->sprint_t > 0.25f && mlen > 0.05f;   // hold to sprint; a tap already dodged
    // --- traversal --- Momentum builds rather than switches on: sprint_ramp seconds of holding
    // Shift to reach the top speed, and it is kept while the feet are off the ground, so a hop
    // costs you nothing and a stop costs you the run-up. This ramp, not the accel coefficient, is
    // what you feel -- Quake's accel reaches any wish speed inside a couple of ticks.
    if (sprinting && p->trav != TM_SLIDE) p->momentum = fminf(1.0f, p->momentum + dt / fmaxf(d->sprint_ramp, 0.05f));
    else if (c->grounded && p->trav == TM_NONE) p->momentum = fmaxf(0.0f, p->momentum - dt / 0.5f);
    c->body_height = p->trav == TM_SLIDE ? c->height * SLIDE_HEIGHT : c->height;
    // Running into a wall zeroes hvel inside one tick (level_move gives back what actually
    // happened), so by the time anything notices you are blocked the speed you arrived at is gone.
    // This remembers it, bleeding off at 6 m/s per second -- a fifth of a second of memory, which is
    // the difference between vaulting a crate and climbing it.
    p->run_speed = fmaxf(hypotf(c->hvel.x, c->hvel.z), p->run_speed - 6.0f * dt);
    float want_speed = 0;

    switch (p->state) {
    case PS_FREE: {
        // --- traversal --- A scripted move owns the tick: a mantle is not a moment to swing, and
        // the moves that DO belong to it (jumping out of a slide, off a wall) are handled inside.
        if (p->trav != TM_NONE) { traverse_update(p, in, move_dir, mlen, w, dt, ev); want_speed = c->speed; break; }

        // A landing is decided here and reported by game.c's grounding, because only the grounding
        // knows it happened and only the movement knows what to do about it. Anything under
        // roll_fall keeps its momentum: that is the whole difference between this and a shooter.
        if (c->land_impact > 0.0f) {
            float drop = c->land_drop, sp0 = hypotf(c->hvel.x, c->hvel.z);
            ev->landed = c->land_impact;
            c->land_impact = 0; c->land_drop = 0;
            if (drop > d->roll_fall && sp0 > 2.0f) {
                Vec3 rd = v3(c->hvel.x / sp0, 0, c->hvel.z / sp0);
                traverse_roll(p, rd, sp0, ev);
                want_speed = c->speed; break;
            }
        }

        if (p->buf_parry > 0) { player_guard(p, in->parry ? in->parry_age : 0, dt); break; }
        if (p->buf_attack > 0) { player_swing(p, sprinting ? 3 : 0, boss, mlen, ev); break; }
        if (p->buf_dodge > 0) { player_dodge(p, move_dir, mlen); break; }
        if (in->rmouse_held) { player_guard(p, 0, dt); p->t = d->parry_window; break; }   // held from before: straight to block

        // Half-Life/Quake ground movement: friction bleeds hvel toward zero every tick you are
        // grounded, whether or not there is input, so stopping is a slide rather than a snap; then
        // wishdir/wishspeed gets accelerated toward. accel/air_accel are Quake's sv_accelerate-shaped
        // coefficients, not m/s^2 -- what actually limits air control is AIR_WISH capping the
        // wishspeed the accel step chases while airborne.
        float top_speed = d->speed * (1.0f + (d->sprint_mult - 1.0f) * p->momentum);
        float wishspeed = top_speed * mlen * (in->crouch ? d->crouch_mult : 1.0f);
        // A jump this tick skips the friction: one 60 Hz bite out of 7 m/s is most of a metre per
        // second, and paying it on every landing is the difference between a chain of hops and a
        // series of stops. The speed cap is what keeps that from becoming a bunny-hop exploit.
        bool jump_now = p->buf_jump > 0 && (c->grounded || p->air_t < d->coyote) && c->vy <= 0.01f;
        if (c->grounded && !jump_now) {
            float speed = v3_len(c->hvel);
            if (speed > 0.01f) {
                float control = fmaxf(speed, d->stop_speed);
                float drop = control * d->friction * dt;
                c->hvel = v3_scale(c->hvel, fmaxf(0, speed - drop) / speed);
            } else c->hvel = v3(0, 0, 0);
        }
        float accel_used = c->grounded ? d->accel : d->air_accel;
        float air_wish = d->air_wish > 0 ? d->air_wish : AIR_WISH;
        float wishspeed_used = c->grounded ? wishspeed : fminf(wishspeed, air_wish);
        float cur = v3_dot(c->hvel, move_dir);
        float add = wishspeed_used - cur;
        if (add > 0) {
            float accelspeed = fminf(accel_used * wishspeed_used * dt, add);
            c->hvel = v3_add(c->hvel, v3_scale(move_dir, accelspeed));
        }
        if (d->speed_cap > 0.5f) {
            float sp = hypotf(c->hvel.x, c->hvel.z);
            if (sp > d->speed_cap) c->hvel = v3_scale(c->hvel, d->speed_cap / sp);
        }

        // Move, then take the velocity back off what actually happened: walking into a wall must
        // not build up a charge that fires the player sideways the moment the wall ends.
        Vec3 prev = c->pos;
        Vec3 asked = v3_scale(c->hvel, dt);
        c->pos = level_move(w->lv, c->pos, c->radius, c->body_height, asked);
        if (dt > 1e-5f) { c->hvel.x = (c->pos.x - prev.x) / dt; c->hvel.z = (c->pos.z - prev.z) / dt; }
        float moved = v3_len(v3_sub(c->pos, prev));
        // Walking into something: the move asked for real distance and got almost none of it. That
        // is the signal the mantle probe runs on, so a wall never has to be marked up as climbable.
        float asked_len = hypotf(asked.x, asked.z);
        bool blocked = asked_len > 0.004f && hypotf(c->pos.x - prev.x, c->pos.z - prev.z) < asked_len * 0.45f;
        c->walk_phase += moved * 5.0f;
        p->step_timer += moved;
        want_speed = hypotf(c->hvel.x, c->hvel.z);   // horizontal speed actually achieved, not the input
        // Cadence follows speed because the stride does: a sprint covers more ground per footfall
        // than a walk, so the steps come faster without anything counting time.
        float stride = clampf(0.80f + want_speed * 0.085f, 0.80f, 1.45f);
        if (c->grounded && p->step_timer > stride) { p->step_timer = 0; ev->footstep = true; }

        if (mlen > 0.05f) c->yaw = angle_damp(c->yaw, atan2f(move_dir.x, move_dir.z), d->turn_speed, dt);
        if (!c->grounded) character_set_anim(c, c->vy > 1.0f ? ANIM_JUMP : ANIM_FALL);
        else if (want_speed > 0.2f) character_set_anim(c, sprinting ? ANIM_RUN : ANIM_WALK);
        else { character_set_anim(c, ANIM_IDLE); p->step_timer = 0.5f; }

        // The jump itself: gravity and ground contact are game.c's, so all this hands back is an
        // upward vy sized to clear jump_height. Coyote time and the buffer are in jump_now above.
        if (jump_now) {
            c->vy = sqrtf(2.0f * PLAYER_JUMP_GRAVITY * d->jump_height);
            c->grounded = false; p->buf_jump = 0; p->air_t = d->coyote; p->n_jump++;
            character_set_anim(c, ANIM_JUMP);
        }
        // Last: what the world offers. A slide, a ledge or a wall, decided from the speed and the
        // blocks, never from a key of its own.
        traverse_try(p, in, move_dir, mlen, w, blocked, ev);
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
            c->pos = level_move(w->lv, c->pos, c->radius, c->height, v3_scale(forward(c->yaw), sp * dt));
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
        c->pos = level_move(w->lv, c->pos, c->radius, c->height, v3_scale(p->dodge_dir, sp * dt));
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
