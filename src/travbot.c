// Traversal bot: a headless course runner for the parkour set (combat.h's TravMode), the way
// itembot.c is one for the loot loop and weaponbot.c is one for guns. Off by default -- picking a
// fight or fetching loot must not notice this file exists -- and on only when the environment says
// HOLLOW_BOT contains "traverse", which is how a capture asks one process to run laps of the level's
// ledges and drop-offs instead of doing the loot circuit. The point of the run is the *log*: sprint,
// mantle, vault and slide counters climbing every second, on a level-agnostic target picker so any
// level with something to jump on exercises the same moves without a human at the keyboard.
#include "game.h"
#include "debug.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

// A local copy of netgame.c's wrap_pi, same as itembot.c's and weaponbot.c's: three lines, not
// worth reaching across translation units for.
static float wrap_pi(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }

// HOLLOW_BOT=traverse gates the whole file. "Contains" rather than an exact match (unlike
// weaponbot's HOLLOW_BOT=shoot) because a capture script may want to say HOLLOW_BOT=traverse,shoot
// across a multi-process run without this file and weaponbot.c fighting over the same variable.
static bool bot_enabled(void) {
    static int on = -1;
    if (on < 0) { const char *e = SDL_getenv("HOLLOW_BOT"); on = (e && strstr(e, "traverse")) ? 1 : 0; }
    return on == 1;
}

// Everything traverse_bot_input remembers between ticks. One static instance, same reasoning as
// ItemBotState and WeaponBotState: only the local player ever drives this.
typedef struct TravBotState {
    bool     have_target;
    Vec3     target;               // world point currently being walked to (top of a block, or a wander point)
    unsigned target_tick;          // g->tick when `target` was chosen, for the 6 s give-up
    int      last_block;           // index of the block last targeted, so the picker alternates instead of
                                    // re-choosing the same ledge every time the 2 m arrival radius fires
    unsigned wander_count;         // bumped each time no block qualifies; drives the 40 degree wander turn
    unsigned blocked_ticks;        // consecutive ticks spent wanting to move but barely moving
    unsigned last_jump;            // g->tick of the last jump press, for the one-per-20-ticks limit
    unsigned slide_end;            // g->tick the current slide hold ends, 0 = not sliding
    unsigned last_tick;            // for level-reload detection (g->tick going backwards)
} TravBotState;

static TravBotState s_tbot = { .last_block = -1 };

// Everything traverse_bot_log remembers between the once-a-tick calls: a rolling one-second window
// of speed and airtime, and the traversal counters' values as of the last line printed (the log
// prints *deltas*, since n_mantle etc. only ever count up for the whole run).
typedef struct TravLogState {
    unsigned last_tick;
    float    accum_t;              // seconds accumulated toward the next 1 s line
    unsigned accum_ticks;
    float    speed_sum, speed_max;
    unsigned air_ticks;
    bool     have_prev;             // false until the counters have a baseline to take a delta against
    bool     printed_once;          // the first window is swallowed on purpose: "nothing in the first second"
    unsigned prev_mantle, prev_vault, prev_slide, prev_jump, prev_roll, prev_wallrun;
} TravLogState;

static TravLogState s_tlog;

bool traverse_bot_input(struct Game *g, struct Input *in) {
    if (!bot_enabled()) return false;   // untouched: the loot bot, the weapon bot, or a real player owns `in`

    TravBotState *s = &s_tbot;
    // A level reload runs the tick counter back to (near) zero; a stale target or jump cooldown
    // from the previous level must not leak into the new one, or the run stops being deterministic.
    if (g->tick < s->last_tick) {
        TravBotState fresh = { .last_block = -1 };
        *s = fresh;
    }
    s->last_tick = g->tick;

    in->attack = in->parry = in->dodge = in->interact = false;
    in->look_x = in->look_y = 0;
    in->jump = false;

    const Player *p = &PLAYER(g);

    // TARGET SELECTION: keep walking to the current target until it is reached, stale, or was never
    // set. Arrival is judged horizontally -- a target on top of a ledge is, almost by definition,
    // above the player until the mantle or the jump actually gets them up there.
    bool need_new = !s->have_target;
    if (s->have_target) {
        float dh = hypotf(s->target.x - p->c.pos.x, s->target.z - p->c.pos.z);
        if (dh < 2.0f) need_new = true;
        if (g->tick - s->target_tick >= 360) need_new = true;   // 6 s at 60 Hz: stuck or overshot, move on
    }
    if (need_new) {
        int best = -1; float best_d = 0;
        for (int i = 0; i < g->level.nblocks; i++) {
            if (i == s->last_block) continue;   // alternates between obstacles instead of bouncing on one
            const Block *b = &g->level.blocks[i];
            if (!b->solid && !b->platform) continue;
            float top = b->center.y + b->size.y * 0.5f;
            // A ledge worth mantling or vaulting sits above the knees and below head height; anything
            // lower is just floor and anything higher is a wall, not a step.
            if (top < p->c.pos.y + 0.6f || top > p->c.pos.y + 2.2f) continue;
            float dx = b->center.x - p->c.pos.x, dz = b->center.z - p->c.pos.z;
            float dist = hypotf(dx, dz);
            // Too close and there is nothing to build sprint speed into; too far and the run spends
            // most of its time crossing open ground instead of exercising the traversal moves.
            if (dist < 4.0f || dist > 30.0f) continue;
            if (best < 0 || dist < best_d) { best = i; best_d = dist; }
        }
        if (best >= 0) {
            const Block *b = &g->level.blocks[best];
            s->target = v3(b->center.x, b->center.y + b->size.y * 0.5f, b->center.z);
            s->last_block = best;
        } else {
            // Nothing to climb on this level (or from here): keep moving anyway, on a heading that
            // turns 40 degrees every time this fires, so the run does not just idle in one spot and
            // still produces sprint (and, on open ground, slide) samples for the log.
            s->wander_count++;
            float heading = (float)s->wander_count * 40.0f * DEG2RAD;
            s->target = v3(p->c.pos.x + 14.0f * sinf(heading), p->c.pos.y, p->c.pos.z + 14.0f * cosf(heading));
        }
        s->target_tick = g->tick;
        s->have_target = true;
    }

    // STEERING: identical to netgame_bot_wander's two cases -- the movement code only ever reads
    // g->cam.yaw to turn a stick back into world space, so a world heading means nothing until it is
    // expressed in the camera's basis.
    Vec3 diff = v3_sub(s->target, p->c.pos); diff.y = 0;
    float dist = v3_len(diff);
    Vec3 want = dist > 0.1f ? v3_scale(diff, 1.0f / dist) : v3(0, 0, 0);

    if (g->cam.mode == CAM_FIRST) {
        float d = wrap_pi(atan2f(want.x, want.z) - g->cam.yaw);
        in->look_x = -clampf(d, -0.05f, 0.05f) / camera_mouse_sens();   // the camera subtracts look_x * sens
        in->look_y = 0;                                                 // a stray mouse must not tilt a headless run
        in->move_x = 0; in->move_y = -1;
    } else {
        Vec3 f = v3(sinf(g->cam.yaw), 0, cosf(g->cam.yaw)), r = v3(-f.z, 0, f.x);
        in->move_x = v3_dot(want, r); in->move_y = -v3_dot(want, f);
    }

    // JUMP: either stuck against something the walk cycle cannot resolve on its own (blocked for
    // 12 straight ticks while trying to move), or closing on a target whose top is a real step up,
    // where a jump is what turns a walk into a mantle or a vault. Rate-limited so a jump held against
    // a wall for 12 ticks does not then fire every single tick once the counter is past threshold.
    bool wants_move = fabsf(in->move_x) > 0.1f || fabsf(in->move_y) > 0.1f;
    float hspeed = hypotf(p->c.hvel.x, p->c.hvel.z);
    if (wants_move && hspeed < 1.5f) s->blocked_ticks++; else s->blocked_ticks = 0;
    bool blocked = s->blocked_ticks >= 12;
    bool cresting = dist < 2.5f && (s->target.y - p->c.pos.y) > 0.6f;
    bool closing = dist > 0.1f;
    if (closing && (blocked || cresting) && g->tick - s->last_jump >= 20) {
        in->jump = true;
        s->last_jump = g->tick;
        s->blocked_ticks = 0;
    }

    // SLIDE: checked once every 4 s (240 ticks) rather than continuously, so a single burst of speed
    // opens exactly one slide instead of re-triggering every tick it stays above the threshold.
    bool sliding = g->tick < s->slide_end;
    if (!sliding && g->tick % 240 == 0 && hspeed > 5.0f && p->c.grounded) {
        s->slide_end = g->tick + 48;   // 0.8 s, long enough for combat.c's slide to actually engage
        sliding = true;
    }
    in->crouch = sliding;
    in->sprint = !sliding;   // sprint held throughout except while sliding, per the spec above

    return true;
}

void traverse_bot_log(struct Game *g, float dt) {
    TravLogState *s = &s_tlog;
    if (g->tick < s->last_tick) { TravLogState fresh = {0}; *s = fresh; }
    s->last_tick = g->tick;

    if (g->state == GS_MENU) return;   // nobody is being played; nothing to log

    const Player *p = &PLAYER(g);
    if (!s->have_prev) {
        // First sight of this player's counters: nothing to take a delta against yet, so this tick
        // only seeds the baseline. The window it falls in is the one "log nothing in the first
        // second" swallows below.
        s->prev_mantle = p->n_mantle; s->prev_vault = p->n_vault; s->prev_slide = p->n_slide;
        s->prev_jump = p->n_jump; s->prev_roll = p->n_roll; s->prev_wallrun = p->n_wallrun;
        s->have_prev = true;
    }

    float speed = hypotf(p->c.hvel.x, p->c.hvel.z);
    s->speed_sum += speed;
    if (speed > s->speed_max) s->speed_max = speed;
    if (!p->c.grounded) s->air_ticks++;
    s->accum_ticks++;
    s->accum_t += dt;

    if (s->accum_t < 1.0f) return;

    if (s->printed_once && s->accum_ticks > 0) {
        float avg = s->speed_sum / (float)s->accum_ticks;
        float air_pct = 100.0f * (float)s->air_ticks / (float)s->accum_ticks;
        unsigned d_mantle  = p->n_mantle  - s->prev_mantle;
        unsigned d_vault   = p->n_vault   - s->prev_vault;
        unsigned d_slide   = p->n_slide   - s->prev_slide;
        unsigned d_jump    = p->n_jump    - s->prev_jump;
        unsigned d_roll    = p->n_roll    - s->prev_roll;
        unsigned d_wallrun = p->n_wallrun - s->prev_wallrun;
        dbg_log("traverse: speed %.2f avg %.2f max | air %.0f%% | mantle %u vault %u slide %u jump %u roll %u wallrun %u | mode %s",
                avg, s->speed_max, air_pct, d_mantle, d_vault, d_slide, d_jump, d_roll, d_wallrun, trav_name(p->trav));
    }
    s->printed_once = true;

    s->prev_mantle = p->n_mantle; s->prev_vault = p->n_vault; s->prev_slide = p->n_slide;
    s->prev_jump = p->n_jump; s->prev_roll = p->n_roll; s->prev_wallrun = p->n_wallrun;
    s->accum_t -= 1.0f; if (s->accum_t < 0.0f) s->accum_t = 0.0f;
    s->accum_ticks = 0; s->speed_sum = 0.0f; s->speed_max = 0.0f; s->air_ticks = 0;
}
