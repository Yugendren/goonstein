// Explore bot: the M2 loot loop, R.E.P.O. style. A free-running state machine that a headless
// test (or an empty seat) can drive: find an item nobody is holding, walk to it, grab it, walk it
// to the boat's hold, drop it, repeat. The movement half of this is lifted straight from
// netgame.c's netgame_bot_wander and game.c's bot_input, which is why the steering block below
// looks like theirs: the movement code is camera-relative, so a world heading has to be expressed
// in the camera's basis (third person) or turned into a head-yaw nudge plus "walk forward"
// (first person). See items.h for the two entry points this file implements.
// game.h pulls in items.h (and, through platform.h's SDL.h, stddef.h for NULL that phys.h needs);
// include it first rather than items.h directly so that ordering holds regardless of build unit.
#include "game.h"
#include "debug.h"
#include <math.h>

// A local copy of netgame.c's wrap_pi: that one is static to netgame.c, and a three-line angle
// helper is not worth reaching across translation units for.
static float wrap_pi(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }

#define ITEMBOT_SKIP_MAX 8   // items abandoned as unreachable; small ring so the search moves on

// Everything the bot remembers between ticks. Only the local player ever drives this (it is called
// from the same place bot_input is), so one static instance is enough: no per-slot table needed.
typedef struct ItemBotState {
    int      target;                          // item index being fetched, -1 = none chosen yet
    unsigned target_since;                    // tick the walk to `target` began; for the 15 s give-up
    int      skip[ITEMBOT_SKIP_MAX];          // items given up on, so the next search skips them
    int      skip_n, skip_next;               // ring bookkeeping
    Vec3     goal;                             // world point currently being walked to
    bool     active;                           // an item run is on; what items_bot_target reports
    // logged_* hold the item identity we last printed a line for, so a log line fires once per
    // state change instead of once per tick (or once per 10 ticks, while the interact button spams).
    int      logged_walk, logged_grab, logged_carry, logged_drop;
    // Stuck detection, identical in shape to game.c's bot_input: an item wedged behind a wall or
    // through a wall of the level geometry must not pin the bot in place forever.
    Vec3     last_pos; unsigned last_tick, stuck_until; int side; bool walked;
} ItemBotState;

static ItemBotState s_bot = { .target = -1, .logged_walk = -1, .logged_grab = -1,
                               .logged_carry = -1, .logged_drop = -1, .side = 1 };

Vec3 items_bot_target(const struct Game *g) {
    (void)g;   // the run state lives in s_bot, not per-Game; g is only here for a stable signature
    return s_bot.active ? s_bot.goal : v3(0, 0, 0);
}

bool items_bot_input(struct Game *g, struct Input *in) {
    in->interact = false; in->attack = false; in->sprint = false; in->move_x = in->move_y = 0;

    Items *its = &g->items;
    const Player *p = &PLAYER(g);
    const Carry *carry = &its->carry[g->local];
    ItemBotState *s = &s_bot;

    Vec3 goal;   // world point to walk toward this tick; also what items_bot_target hands back

    if (carry->item < 0) {
        // Not carrying: confirm the chosen target is still worth walking to, else pick a new one.
        bool valid = s->target >= 0 && s->target < its->n;
        if (valid) {
            const Item *it = &its->it[s->target];
            valid = it->used && !it->broken && it->held_by < 0 && !it->in_hold && !it->sunk && it->drop_lock <= 0;
        }
        if (valid && g->tick - s->target_since > 900) {
            // Wedged behind a wall or otherwise unreachable for 15 seconds: give up on it and
            // remember it so the next search does not just walk straight back to the same item.
            dbg_log("bot: giving up on item %d, stuck", s->target);
            s->skip[s->skip_next] = s->target;
            s->skip_next = (s->skip_next + 1) % ITEMBOT_SKIP_MAX;
            if (s->skip_n < ITEMBOT_SKIP_MAX) s->skip_n++;
            valid = false;
        }
        if (!valid) {
            int best = -1; float best_d = 0;
            for (int i = 0; i < its->n; i++) {
                const Item *it = &its->it[i];
                if (!it->used || it->broken || it->held_by >= 0 || it->in_hold || it->sunk || it->drop_lock > 0) continue;   // sunk: not worth drowning for
                bool skipped = false;
                for (int k = 0; k < s->skip_n; k++) if (s->skip[k] == i) { skipped = true; break; }
                if (skipped) continue;
                Vec3 d = v3_sub(it->pos, p->c.pos); d.y = 0;
                float dist = v3_len(d);
                if (best < 0 || dist < best_d) { best = i; best_d = dist; }
            }
            if (best < 0) {
                // Nothing left to fetch: hand back to whatever fallback wander the caller uses.
                s->target = -1; s->active = false; s->goal = v3(0, 0, 0);
                return false;
            }
            s->target = best;
            s->target_since = g->tick;
        }
        const Item *it = &its->it[s->target];
        if (s->logged_walk != s->target) {
            const ItemDef *def = item_def(its, it);
            dbg_log("bot: walking to item %d (%s) at %.1f %.1f", s->target, def->display, it->pos.x, it->pos.z);
            s->logged_walk = s->target;
        }
        goal = it->pos;
        Vec3 d = v3_sub(it->pos, p->c.pos); d.y = 0;
        if (v3_len(d) < ITEM_REACH * 0.7f) {
            if (g->tick % 10 == 0) in->interact = true;   // one press per tick, spamming toggles grab/drop
            if (s->logged_grab != s->target) { dbg_log("bot: grabbing item %d", s->target); s->logged_grab = s->target; }
        }
    } else {
        // Carrying something: head for the hold, or back to spawn if this level has no hold volume.
        Vec3 hold_c;
        goal = items_hold_center(g, &hold_c) ? hold_c : g->level.spawn;
        if (s->logged_carry != carry->item) {
            dbg_log("bot: carrying item %d to the hold", carry->item);
            s->logged_carry = carry->item;
        }
        Vec3 d = v3_sub(goal, p->c.pos); d.y = 0;
        // Let go when the thing in its hands is over the hold, not when its feet are: the hold
        // point sits 1.2 m in front of the eye, so "standing on the boat" drops loot over the bow.
        bool over = items_in_hold_volume(g, items_hold_point(g, g->local));
        if (over || v3_len(d) < 0.5f) {
            if (g->tick % 10 == 0) in->interact = true;
            if (s->logged_drop != carry->item) { dbg_log("bot: dropping item %d at the hold", carry->item); s->logged_drop = carry->item; }
        }
        s->target = -1;   // not walking toward a specific item while one is in hand
    }
    s->active = true;
    s->goal = goal;

    // Steering: identical to netgame_bot_wander's two cases. `want` is a unit heading in world XZ.
    Vec3 diff = v3_sub(goal, p->c.pos); diff.y = 0;
    float dist = v3_len(diff);
    Vec3 want = dist > 0.1f ? v3_scale(diff, 1.0f / dist) : v3(0, 0, 0);

    // The head always aims at the goal; sidestepping is done with the feet. An earlier version
    // steered by rewriting `want`, which made the bot chase a heading that moved every half second
    // and spin on the spot forever.
    if (g->cam.mode == CAM_FIRST) {
        float wrap = wrap_pi(atan2f(want.x, want.z) - g->cam.yaw);
        in->look_x = -clampf(wrap, -0.05f, 0.05f) / camera_mouse_sens();   // the camera subtracts look_x * sens
        in->look_y = 0;
        // Only walk once roughly facing the goal, or the bot orbits it.
        in->move_x = 0;
        in->move_y = (v3_len(want) > 0.1f && fabsf(wrap) < 60.0f * DEG2RAD) ? -1.0f : 0.0f;
        if (g->tick < s->stuck_until) { in->move_x = (float)s->side; in->move_y = -0.6f; }
    } else {
        // Third person: the movement code is camera-relative, so express the world heading in the
        // camera's basis rather than the world's.
        Vec3 f = v3(sinf(g->cam.yaw), 0, cosf(g->cam.yaw)), r = v3(-f.z, 0, f.x);
        Vec3 go = want;
        if (g->tick < s->stuck_until) go = v3_norm(v3_add(want, v3_scale(r, (float)s->side * 1.5f)));
        in->move_x = v3_dot(go, r); in->move_y = -v3_dot(go, f);
    }
    if (fabsf(in->move_x) > 0.1f || fabsf(in->move_y) > 0.1f) s->walked = true;

    // Stuck handling, in the shape of game.c's bot_input: barely moved in half a second *while
    // actually walking*, so sidestep for a moment, alternating sides. The "while walking" part
    // matters: a bot turning on the spot toward a new target is not stuck, and diagnosing it as
    // stuck is how it ends up rocking between two sidestep headings and never arriving.
    if (g->tick - s->last_tick >= 30) {
        if (s->walked && v3_len(v3_sub(p->c.pos, s->last_pos)) < 0.25f) { s->stuck_until = g->tick + 60; s->side = -s->side; }
        s->last_pos = p->c.pos; s->last_tick = g->tick; s->walked = false;
    }
    // HOLLOW_BOT_TRACE: a slow trace of what the loot bot thinks it is doing, for headless runs.
    if (SDL_getenv("HOLLOW_BOT_TRACE") && g->tick % 30 == 0)
        dbg_log("bot trace: pos %.1f %.1f %.1f goal %.1f %.1f dist %.1f yaw %.0f move %.2f %.2f%s carry %d",
                p->c.pos.x, p->c.pos.y, p->c.pos.z, goal.x, goal.z, (double)dist, g->cam.yaw / DEG2RAD,
                (double)in->move_x, (double)in->move_y, g->tick < s->stuck_until ? " STUCK" : "", carry->item);
    return true;
}
