// Weapon bot: a headless verification harness for the weapon system, the way itembot.c is one for
// the loot loop. Off by default -- the ordinary loot bot must not notice this file exists -- and on
// only when the environment says HOLLOW_BOT=shoot, which is how a four-process test asks one of its
// processes to go pick a fight instead of running the loot circuit.
//
// The state machine: ARM (find a weapon, walk to it, grab it) -> HUNT (pick a target, close in,
// aim) -> FIRE (pull the trigger on a cadence once the aim is good) -> RELOAD (an empty gun) and,
// cutting across all of it, DOWN (lie there) and a detour to REVIVE a downed mate. See weapons.h for
// the one function this file implements.
//
// The steering is lifted from itembot.c, which is lifted from netgame.c's netgame_bot_wander: a
// world heading only means something once it is expressed in the camera's basis, because that is
// what the movement code (camera_move_dir, called from game.c) turns back into world space. Aiming
// a gun asks for more than itembot.c ever needed, though: weapons_aim reads the camera's own facing
// (g->cam.target - g->cam.eye), in both first and third person, so this file also has to turn the
// camera itself -- yaw and now pitch too -- rather than just walk toward a goal and let the camera
// sit wherever it was left.
#include "game.h"
#include "debug.h"
#include <math.h>
#include <string.h>

// A local copy of netgame.c's wrap_pi, same as itembot.c's: three lines, not worth an include.
static float wrap_pi(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }

#define BOT_HUNT_RANGE   25.0f   // metres: how far off the bot will notice something worth shooting
#define BOT_FIRE_DEG     4.0f    // degrees of aim error close enough to pull the trigger
#define BOT_FIRE_PERIOD  150u    // ticks between bursts, 2.5 s at the fixed 60 Hz tick rate
#define BOT_MIN_RANGE     4.0f   // metres: standing on top of a thing on the floor means looking
                                 // straight down at it, and the camera's pitch clamp will not go
                                 // that far, so the bot keeps its distance and can actually aim
#define BOT_REVIVE_RANGE 8.0f    // metres: how far off a downed mate the bot will detour for them

// Everything the bot remembers between ticks. One static instance, same reasoning as itembot.c's
// ItemBotState: only the local player ever drives this, so there is nothing to key by slot.
typedef struct WeaponBotState {
    int      arm_target;         // item index of the weapon being fetched, -1 = none chosen yet
    unsigned next_fire;          // tick the next burst is allowed
    bool     reloading;          // R has been pressed for the reload in progress, so it is not spammed
    // logged_* hold the identity last printed a line for, so a line fires once per state change.
    int      logged_arm, logged_revive;
} WeaponBotState;

static WeaponBotState s_wbot = { .arm_target = -1, .logged_arm = -1, .logged_revive = -1 };

// Turns the camera toward a world point, driving both look_x (yaw) and look_y (pitch) the way
// itembot.c's first-person branch drives yaw: a small proportional nudge every tick, because
// look_x/look_y are mouse deltas the camera integrates, not an absolute heading to snap to.
// camera_orbit and camera_first both fold pitch in with `pitch += look_y * sens`, unlike yaw which
// is subtracted, so the two signs below are not a typo -- they match camera.c's own conventions.
static void aim_at(Game *g, Input *in, Vec3 eye, Vec3 at) {
    Vec3 d = v3_sub(at, eye);
    float horiz = sqrtf(d.x * d.x + d.z * d.z);
    float wrap = wrap_pi(atan2f(d.x, d.z) - g->cam.yaw);
    in->look_x = -clampf(wrap, -0.05f, 0.05f) / camera_mouse_sens();
    float want_pitch = atan2f(-d.y, horiz);
    float perr = clampf(want_pitch - g->cam.pitch, -0.05f, 0.05f);
    in->look_y = perr / camera_mouse_sens();
}

// Walks toward `goal`, holding still inside `stop_dist`. Camera-relative exactly as itembot.c's
// third-person branch and game.c's boss-fight bot do it: express the world heading in the camera's
// basis, because camera_move_dir only ever looks at g->cam.yaw to turn the stick back into world
// space. Get this backwards and the bot strafes past everything it is trying to walk up to.
// Walk directly away from a point. The bot only needs this because the things worth shooting on
// this island are mostly lying on the floor, and from half a metre away they are underneath the
// camera's pitch limit rather than in front of it.
static void back_away(Game *g, Input *in, Vec3 from) {
    Vec3 diff = v3_sub(PLAYER(g).c.pos, from); diff.y = 0;
    float dist = v3_len(diff);
    if (dist < 0.05f) { in->move_x = 0; in->move_y = 1; return; }
    Vec3 want = v3_scale(diff, 1.0f / dist);
    Vec3 f = v3(sinf(g->cam.yaw), 0, cosf(g->cam.yaw)), r = v3(-f.z, 0, f.x);
    in->move_x = v3_dot(want, r); in->move_y = -v3_dot(want, f);
}

static void walk_to(Game *g, Input *in, Vec3 goal, float stop_dist) {
    Vec3 diff = v3_sub(goal, PLAYER(g).c.pos); diff.y = 0;
    float dist = v3_len(diff);
    if (dist < stop_dist) { in->move_x = in->move_y = 0; return; }
    Vec3 want = v3_scale(diff, 1.0f / dist);
    Vec3 f = v3(sinf(g->cam.yaw), 0, cosf(g->cam.yaw)), r = v3(-f.z, 0, f.x);
    in->move_x = v3_dot(want, r); in->move_y = -v3_dot(want, f);
}

// HOLLOW_BOT=shoot gates the whole file. Cached the same tri-state way phys.c caches
// HOLLOW_PHYS_TRACE: -1 not looked up yet, then 0 or 1 for the life of the process.
static bool bot_enabled(void) {
    static int on = -1;
    if (on < 0) { const char *e = SDL_getenv("HOLLOW_BOT"); on = (e && !strcmp(e, "shoot")) ? 1 : 0; }
    return on == 1;
}

bool weapons_bot_input(Game *g, Input *in) {
    if (!bot_enabled()) return false;   // untouched: the loot bot (or a real player) owns `in`

    in->interact = false; in->click = false; in->mouse_held = false; in->attack = false;
    in->sprint = false; in->move_x = in->move_y = 0; in->look_x = in->look_y = 0;
    in->key_down[SDL_SCANCODE_R] = false; in->key_held[SDL_SCANCODE_E] = false;

    WeaponBotState *s = &s_wbot;
    Items *its = &g->items;
    Weapon *w = &g->weapons.w[g->local];
    Vec3 eye = weapons_eye(g, g->local);

    // DOWN: the controls belong to nobody, same as a real downed player. Lie there.
    if (weapons_is_down(g, g->local)) return true;

    // ARM: nothing in the weapon hand. Find the nearest free weapon on the level and go get it.
    if (w->item < 0) {
        bool valid = s->arm_target >= 0 && s->arm_target < its->n;
        if (valid) {
            const Item *it = &its->it[s->arm_target];
            valid = it->used && !it->broken && it->held_by < 0 && !it->sunk
                    && weapon_kind_of(g, s->arm_target) != WK_NONE;
        }
        if (!valid) {
            int best = -1; float best_d = 0;
            for (int i = 0; i < its->n; i++) {
                const Item *it = &its->it[i];
                if (!it->used || it->broken || it->held_by >= 0 || it->sunk) continue;
                if (weapon_kind_of(g, i) == WK_NONE) continue;
                Vec3 d = v3_sub(it->pos, PLAYER(g).c.pos); d.y = 0;
                float dist = v3_len(d);
                if (best < 0 || dist < best_d) { best = i; best_d = dist; }
            }
            s->arm_target = best;
        }
        if (s->arm_target < 0) return true;   // no weapon anywhere on the level: nothing to do
        const Item *it = &its->it[s->arm_target];
        if (s->logged_arm != s->arm_target) {
            const ItemDef *def = item_def(its, it);
            dbg_log("bot: going for the %s at %.1f %.1f", def->display, it->pos.x, it->pos.z);
            s->logged_arm = s->arm_target;
        }
        aim_at(g, in, eye, it->pos);
        // Close in until the game itself says the thing is grabbable. ITEM_REACH is measured from
        // the eye, which is most of two metres above the feet, so a bot that stops at 2 m of floor
        // is still out of reach of anything lying on the ground -- as this one was.
        walk_to(g, in, it->pos, ITEM_REACH * 0.35f);
        if (items_look_target(g, g->local) == s->arm_target) in->interact = true;
        return true;
    }
    s->arm_target = -1; s->logged_arm = -1;   // armed: nothing left to walk toward a weapon for

    const ItemDef *wd = item_def(its, &its->it[w->item]);
    WeaponKind kind = weapon_kind_of(g, w->item);

    // RELOAD: an empty gun. Tap R once and hold still until weapons.c's own reload timer clears it;
    // a melee weapon's ammo is always 0, so the WK_GUN guard keeps a bat from spamming R forever.
    if (kind == WK_GUN && w->ammo <= 0) {
        if (w->reload <= 0 && !s->reloading) {
            in->key_down[SDL_SCANCODE_R] = true;
            s->reloading = true;
            dbg_log("bot: reloading %s, out of ammo", wd->display);
        } else if (w->reload <= 0) {
            s->reloading = false;   // the timer already cleared it and refilled the mag
        }
        return true;
    }
    s->reloading = false;

    // HUNT: pick something to point the gun at. Another goon first, since that is what exercises
    // hurt_player and knockdown; otherwise the nearest ordinary item, so the hitscan-on-loot path
    // gets a workout too. Both are capped at BOT_HUNT_RANGE, or there is nothing to do out here.
    int target_slot = -1, target_item = -1; float best_d = BOT_HUNT_RANGE;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (i == g->local || !g->net.slots[i].active) continue;
        float dist = v3_len(v3_sub(g->players[i].c.pos, PLAYER(g).c.pos));
        if (dist < best_d) { best_d = dist; target_slot = i; }
    }
    if (target_slot < 0) {
        best_d = BOT_HUNT_RANGE;
        for (int i = 0; i < its->n; i++) {
            const Item *it = &its->it[i];
            if (!it->used || it->broken || it->held_by >= 0 || it->sunk) continue;
            if (weapon_kind_of(g, i) != WK_NONE) continue;   // not another gun to go pick up
            float dist = v3_len(v3_sub(it->pos, PLAYER(g).c.pos));
            if (dist < best_d) { best_d = dist; target_item = i; }
        }
    }
    // Melee: right up against it. WEAP_SWING_ARC is measured from the chest, so a bot idling at
    // 1.3 m of floor is 1.7 m of actual reach away from a gnome and swings at nothing all day.
    float engage = kind == WK_GUN ? fminf(wd->wrange * 0.6f, 12.0f) : 0.9f;
    float standoff = kind == WK_GUN ? BOT_MIN_RANGE : 0.7f;
    if (engage < standoff) engage = standoff;
    bool busy = (target_slot >= 0 || target_item >= 0) && best_d <= engage;

    // REVIVE: a mate on the floor nearby, and nothing already in range worth finishing off first.
    int downed = -1; float down_d = BOT_REVIVE_RANGE;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (i == g->local || !g->net.slots[i].active || !weapons_is_down(g, i)) continue;
        float dist = v3_len(v3_sub(g->players[i].c.pos, PLAYER(g).c.pos));
        if (dist < down_d) { down_d = dist; downed = i; }
    }
    // A mate on the floor outranks whatever the bot was shooting at. That is both the right
    // instinct and the only way a headless run ever exercises the revive path, since there is
    // almost always something in range worth pointing a gun at instead.
    (void)busy;
    if (downed >= 0) {
        Vec3 tp = g->players[downed].c.pos;
        if (s->logged_revive != downed) {
            dbg_log("bot: slot %d is down, going to haul them up", downed);
            s->logged_revive = downed;
        }
        aim_at(g, in, eye, v3(tp.x, tp.y + 1.0f, tp.z));
        walk_to(g, in, tp, WEAP_REVIVE_REACH * 0.7f);
        in->key_held[SDL_SCANCODE_E] = true;
        in->interact = true;
        return true;
    }
    s->logged_revive = -1;

    if (target_slot < 0 && target_item < 0) return true;   // nothing to shoot: hold position, armed

    Vec3 target_pos = target_slot >= 0
        ? v3(g->players[target_slot].c.pos.x,
             g->players[target_slot].c.pos.y + g->players[target_slot].c.height * 0.55f,
             g->players[target_slot].c.pos.z)
        : v3(its->it[target_item].pos.x,
             its->it[target_item].pos.y + item_def(its, &its->it[target_item])->half.y,
             its->it[target_item].pos.z);
    Vec3 walk_goal = target_slot >= 0 ? g->players[target_slot].c.pos : its->it[target_item].pos;

    aim_at(g, in, eye, target_pos);
    if (best_d < standoff) back_away(g, in, walk_goal);
    else walk_to(g, in, walk_goal, engage);

    // FIRE: close enough in angle, on a cadence, so the log reads as one deliberate burst every
    // couple of seconds rather than a wall of misses. weapons_aim is the same direction the host
    // will actually shoot along, so the angle test here is the real one, not an approximation.
    Vec3 aim = weapons_aim(g, g->local);
    Vec3 to_target = v3_norm(v3_sub(target_pos, eye));
    bool aimed = v3_dot(aim, to_target) > cosf(BOT_FIRE_DEG * DEG2RAD);
    if (aimed && g->tick >= s->next_fire) {
        if (kind == WK_GUN) in->mouse_held = true; else in->click = true;
        // Deterministic jitter off the tick, the same hash shape as netgame_bot_wander's: no rand(),
        // so two runs of the same seed fire on the same ticks.
        uint32_t h = g->tick * 2654435761u + (uint32_t)(g->local + 1) * 40503u;
        h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
        s->next_fire = g->tick + BOT_FIRE_PERIOD + (h % 30);
        dbg_log("bot: shooting %s at %.1f m", wd->display, (double)best_d);
    }
    return true;
}
