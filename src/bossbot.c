// --- boss --- HOLLOW_BOT=boss: a bot that goes down the culvert and fights the thing at the
// bottom, so the whole feature can be exercised with nobody at the keyboard.
//
// It is a verification harness, not an opponent. It plays the fight the way the fight is meant to
// be played -- keep your distance, keep moving sideways, jump the shockwave, step out of the
// charge lane, get up on a platform now and then, reload when the gun is empty -- and it logs what
// happened so a headless run produces a number instead of a vibe. The bar it is written against:
// pistol plus rifle, 600 hp, under three minutes.
//
// It is deliberately built out of the same pieces weaponbot.c is (aim_at / walk_to / a tri-state
// env gate) rather than sharing them, because those are twenty lines and a shared "bot library"
// would immediately grow a parameter for every bot that used it.
#include "game.h"
#include "boss.h"
#include "debug.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

static float wrap_pi(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }

#define BB_HOLD_MIN     7.0f    // metres: closer than this and the sweep reaches
#define BB_HOLD_MAX    15.0f    // metres: further than this and the pistol is wasting rounds
#define BB_FIRE_DEG     5.0f    // degrees of aim error close enough to pull the trigger
#define BB_STRAFE_TIME 110u     // ticks before it swaps which way it is circling (~1.8 s)
#define BB_PLATFORM_EVERY 600u  // ticks between deciding to go and stand somewhere higher (10 s)
#define BB_STUCK_TICKS   20u    // ticks of wanting to move and not moving before it tries a jump
#define BB_LEG_ARRIVE    4.0f   // metres: close enough to a route point to call it reached. The
                                 // island's roads are six to seven metres wide, so this keeps the
                                 // walk on the tarmac without demanding the bot hit a pixel.
#define BB_LEG_TIMEOUT  600u    // ticks (10 s) before a leg is abandoned and the next one tried

typedef struct BossBotState {
    int      phase;             // 0 heading for the door, 1 fighting
    float    strafe;            // -1 / +1
    unsigned strafe_until, next_platform, stuck, last_jump;
    Vec3     platform;  bool have_platform;
    unsigned shots, tick0;
    bool     logged_door, logged_fight, logged_win, logged_route_done;
    float    last_hp;
    // --- the walk to the door --- the level's own line to it, and which leg of it we are on
    const Route *route; int leg; unsigned leg_tick;
    unsigned dodge_until; float dodge_side;
} BossBotState;

static BossBotState s_bb = { .strafe = 1.0f, .last_hp = -1, .dodge_side = 1.0f };

static bool bot_enabled(void) {
    static int on = -1;
    if (on < 0) { const char *e = SDL_getenv("HOLLOW_BOT"); on = (e && !strcmp(e, "boss")) ? 1 : 0; }
    return on == 1;
}

// HOLLOW_BOSSBOT_RANGE=MIN,MAX overrides the distance the bot tries to hold. Its whole reason to
// exist is capture: the sweep only happens inside six and a half metres and the bot's job is to
// stay outside seven, so without this there is no way to photograph a sweep telegraph without a
// hand on the keyboard. It is a harness knob, not a difficulty setting.
static void hold_band(float *lo, float *hi) {
    static float l = BB_HOLD_MIN, h = BB_HOLD_MAX; static bool read = false;
    if (!read) {
        read = true;
        const char *e = SDL_getenv("HOLLOW_BOSSBOT_RANGE");
        if (e) { float a = 0, b = 0; if (sscanf(e, "%f,%f", &a, &b) == 2 && b > a && a > 0) { l = a; h = b; } }
    }
    *lo = l; *hi = h;
}

// Mouse deltas, not an absolute heading: camera.c integrates look_x/look_y, and its yaw and pitch
// take opposite signs. Lifted from weaponbot.c's aim_at, which explains both.
static void aim_at(Game *g, Input *in, Vec3 eye, Vec3 at) {
    Vec3 d = v3_sub(at, eye);
    float horiz = sqrtf(d.x * d.x + d.z * d.z);
    float wrap = wrap_pi(atan2f(d.x, d.z) - g->cam.yaw);
    in->look_x = -clampf(wrap, -0.08f, 0.08f) / camera_mouse_sens();
    float want_pitch = atan2f(-d.y, horiz);
    in->look_y = clampf(want_pitch - g->cam.pitch, -0.06f, 0.06f) / camera_mouse_sens();
}

static float aim_error_deg(const Game *g, Vec3 eye, Vec3 at) {
    Vec3 want = v3_norm(v3_sub(at, eye));
    Vec3 have = weapons_aim(g, g->local);
    return acosf(clampf(v3_dot(want, have), -1, 1)) / DEG2RAD;
}

// A world heading only means anything once it is in the camera's basis: camera_move_dir turns the
// stick back into world space using g->cam.yaw and nothing else.
static void push(Game *g, Input *in, Vec3 want) {
    float len = v3_len(want);
    if (len < 1e-4f) { in->move_x = in->move_y = 0; return; }
    want = v3_scale(want, 1.0f / len);
    Vec3 f = v3(sinf(g->cam.yaw), 0, cosf(g->cam.yaw)), r = v3(-f.z, 0, f.x);
    in->move_x = v3_dot(want, r); in->move_y = -v3_dot(want, f);
}

static void walk_to(Game *g, Input *in, Vec3 goal, float stop) {
    Vec3 d = v3_sub(goal, PLAYER(g).c.pos); d.y = 0;
    if (v3_len(d) < stop) { in->move_x = in->move_y = 0; return; }
    push(g, in, d);
}

// The centre of the first trigger whose name starts with `prefix`, and its full name. That is how
// the bot finds the culvert door without any coordinates of its own: the level owns where the door
// is, and the level also owns the way to it (see `route` in assets/levels/README.md).
static bool trigger_centre(const Level *lv, const char *prefix, Vec3 *out, const char **name) {
    size_t n = strlen(prefix);
    for (int i = 0; i < lv->ntriggers; i++) {
        if (strncmp(lv->triggers[i].name, prefix, n) != 0) continue;
        *out = v3_scale(v3_add(lv->triggers[i].vmin, lv->triggers[i].vmax), 0.5f);
        if (name) *name = lv->triggers[i].name;
        return true;
    }
    return false;
}

// The nearest block top between a step and a mantle above the bot's feet, within `range`. The bot
// does not know what a "platform" is; it knows what it can climb, which is the same thing.
static bool nearest_platform(const Game *g, float range, Vec3 *out) {
    const Level *lv = &g->level;
    Vec3 me = PLAYER(g).c.pos;
    int best = -1; float best_d = range;
    for (int i = 0; i < lv->nblocks; i++) {
        const Block *b = &lv->blocks[i];
        float top = b->center.y + b->size.y * 0.5f;
        float up = top - me.y;
        if (up < 0.8f || up > 3.2f) continue;                    // not worth climbing / cannot be climbed
        if (b->size.x < 2.0f || b->size.z < 2.0f) continue;      // too small to fight on
        Vec3 d = v3_sub(b->center, me); d.y = 0;
        float dist = v3_len(d);
        if (dist > best_d || dist < 1.0f) continue;
        best = i; best_d = dist;
    }
    if (best < 0) return false;
    *out = v3(lv->blocks[best].center.x, lv->blocks[best].center.y + lv->blocks[best].size.y * 0.5f + 0.1f, lv->blocks[best].center.z);
    return true;
}

bool boss_bot_input(Game *g, Input *in) {
    if (!bot_enabled()) return false;

    BossBotState *s = &s_bb;
    in->move_x = in->move_y = 0; in->look_x = in->look_y = 0;
    in->click = in->mouse_held = in->attack = in->interact = in->sprint = in->jump = false;
    in->key_down[SDL_SCANCODE_R] = false; in->key_held[SDL_SCANCODE_E] = false;
    if (!s->tick0) s->tick0 = g->tick;

    // Flat on your back is flat on your back. In the cave a lone goon is put back at the door by
    // boss.c after three seconds; there is nothing for the bot to do about it in the meantime.
    if (weapons_is_down(g, g->local)) { s->stuck = 0; return true; }

    Vec3 eye = weapons_eye(g, g->local);
    Vec3 me = PLAYER(g).c.pos;
    Weapon *w = &g->weapons.w[g->local];

    // ---- getting there -------------------------------------------------------------------
    //
    // NOT by walking at the door. The island's Culvert is cut into the north-east hill, and the
    // straight line to it from the boat climbs that hill and arrives on TOP of the cutting's roof
    // slab, two metres above the doorway and a metre outside the trigger -- which is exactly where
    // this bot used to stand, at a dead halt, for the rest of the run. The way in is the golf-cart
    // roads, and the level writes them down: a `route` named after the door (`door:cave`) is the
    // line a person walks, leg by leg. With no such route the bot falls back to the old beeline,
    // which is right on a level where the door is in the same room you start in.
    if (!boss_present(g)) {
        Vec3 door; const char *door_name = NULL;
        if (!trigger_centre(&g->level, "door:", &door, &door_name)) return true;   // no door here
        if (!s->logged_door) {
            s->route = level_route(&g->level, door_name);
            s->leg_tick = g->tick;
            // Join the route at the nearest point, not at the start of it. The route is written
            // from the boat because that is where a run begins, but a capture started with
            // --spawn halfway up the island should not walk back to the pier first.
            if (s->route) {
                float best = 1e18f;
                for (int i = 0; i < s->route->n; i++) {
                    float dx = s->route->x[i] - me.x, dz = s->route->z[i] - me.z;
                    if (dx * dx + dz * dz < best) { best = dx * dx + dz * dz; s->leg = i; }
                }
                if (s->leg > 0) dbg_log("bossbot: joining the route at leg %d/%d, %.0f m from its start",
                                        s->leg + 1, s->route->n, (double)sqrtf(best));
            }
            dbg_log("bossbot: heading for %s at %.1f %.1f %.1f, %s",
                    door_name, (double)door.x, (double)door.y, (double)door.z,
                    s->route ? "following the level's route" : "no route on this level, walking straight at it");
            SDL_Log("bossbot: heading for %s%s", door_name, s->route ? " along the level's route" : "");
            s->logged_door = true;
        }
        Vec3 goal = door;
        const Route *r = s->route;
        if (r) {
            // Advance a leg when we are near enough to its point, and advance it anyway when a leg
            // has taken too long: a harness that can deadlock on one bad waypoint is not a harness.
            while (s->leg < r->n) {
                float dx = r->x[s->leg] - me.x, dz = r->z[s->leg] - me.z;
                if (dx * dx + dz * dz > BB_LEG_ARRIVE * BB_LEG_ARRIVE) break;
                s->leg++; s->leg_tick = g->tick;
                if (s->leg < r->n) dbg_log("bossbot: route leg %d/%d -> %.0f %.0f", s->leg + 1, r->n, (double)r->x[s->leg], (double)r->z[s->leg]);
            }
            if (s->leg < r->n && g->tick - s->leg_tick > BB_LEG_TIMEOUT) {
                dbg_log("bossbot: leg %d/%d (%.0f %.0f) took too long, skipping it", s->leg + 1, r->n, (double)r->x[s->leg], (double)r->z[s->leg]);
                s->leg++; s->leg_tick = g->tick;
            }
            if (s->leg < r->n) goal = v3(r->x[s->leg], me.y, r->z[s->leg]);
            else if (!s->logged_route_done) { s->logged_route_done = true; dbg_log("bossbot: route walked, the door is %.1f m away", (double)v3_len(v3_sub(v3(door.x, me.y, door.z), me))); }
        }
        aim_at(g, in, eye, v3(goal.x, eye.y, goal.z));
        // Against something, and still wanting to go through it: jump first (most of what this
        // island puts in the way is a step, a kerb or a low wall), and if that has not worked for a
        // second, walk sideways for half a second instead, alternating which way each time. That is
        // the same escape the overworld bot in game.c has had all along, and without it one palm
        // trunk on the verge is the end of the run.
        bool wants = true;
        if (g->tick < s->dodge_until) {
            Vec3 camf = v3(sinf(g->cam.yaw), 0, cosf(g->cam.yaw)), camr = v3(-camf.z, 0, camf.x);
            push(g, in, v3_add(v3_scale(camr, s->dodge_side), v3_scale(camf, 0.35f)));
        } else {
            walk_to(g, in, goal, 0.4f);
            wants = in->move_x != 0 || in->move_y != 0;
        }
        in->sprint = true;
        if (wants && v3_len(PLAYER(g).c.hvel) < 1.0f) {
            s->stuck++;
            if (s->stuck > BB_STUCK_TICKS && g->tick - s->last_jump > 25) { in->jump = true; s->last_jump = g->tick; }
            if (s->stuck > BB_STUCK_TICKS * 3 && g->tick >= s->dodge_until) {
                s->dodge_side = -s->dodge_side; s->dodge_until = g->tick + 30; s->stuck = 0;
                dbg_log("bossbot: stuck at %.0f %.0f, sidestepping %s", (double)me.x, (double)me.z, s->dodge_side > 0 ? "right" : "left");
            }
        } else s->stuck = 0;
        return true;
    }

    // ---- the fight -----------------------------------------------------------------------
    const CaveBoss *cb = &g->cave;
    const Character *bc = &cb->b.c;
    Vec3 chest = v3(bc->pos.x, bc->pos.y + bc->height * 0.55f, bc->pos.z);
    Vec3 to_boss = v3_sub(bc->pos, me); to_boss.y = 0;
    float dist = v3_len(to_boss);
    Vec3 away = dist > 0.01f ? v3_scale(to_boss, -1.0f / dist) : v3(0, 0, 1);
    Vec3 right = v3(-away.z, 0, away.x);

    if (!s->logged_fight) {
        dbg_log("bossbot: the fight starts, %s at %.0f hp", cb->b.def.name, (double)bc->hp);
        SDL_Log("bossbot: fighting %s (%.0f hp)", cb->b.def.name, (double)bc->hp);
        s->logged_fight = true; s->tick0 = g->tick; s->last_hp = bc->hp;
    }
    if (cb->b.state == BS_DEAD) {
        if (!s->logged_win) {
            s->logged_win = true;
            float secs = (float)(g->tick - s->tick0) / 60.0f;
            SDL_Log("bossbot: it is down after %.1f s and %u shots", (double)secs, s->shots);
            dbg_log("bossbot: win in %.1f s, %u shots, wind %.0f left", (double)secs, s->shots, (double)w->wind);
            boss_report(g);
        }
        walk_to(g, in, bc->pos, 3.0f);   // wander over to the loot
        return true;
    }

    // Aim first: everything else is movement, and the gun must never stop pointing at it.
    aim_at(g, in, eye, chest);

    // --- dodging ---------------------------------------------------------------------------
    // A live shockwave that is about to reach the feet: jump. The ring is half a metre high, so
    // being in the air at the moment it passes is the entire answer.
    bool jumping = false;
    if (cb->shock.live) {
        float rd = v3_len(v3_sub(v3(me.x, cb->shock.at.y, me.z), cb->shock.at));
        float lead = (rd - cb->shock.r) / fmaxf(cb->shock.speed, 0.1f);   // seconds until it arrives
        if (lead > -0.05f && lead < 0.18f && PLAYER(g).c.grounded) { in->jump = true; jumping = true; }
    }
    // A charge winding up with us in the lane: get out of the lane sideways, not backwards. The
    // bot reads the same state the telegraph is drawn from, which is the point of putting that
    // state in the snapshot rather than in an event.
    bool evading = false;
    if (cb->b.state == BS_WINDUP && cb->b.move < cb->b.def.nmoves) {
        const BossMove *m = &cb->b.def.moves[cb->b.move];
        Vec3 fwd = v3(sinf(bc->yaw), 0, cosf(bc->yaw));
        Vec3 rel = v3_sub(me, bc->pos); rel.y = 0;
        float along = v3_dot(rel, fwd), across = v3_dot(rel, v3(-fwd.z, 0, fwd.x));
        if (m->kind == BMK_CHARGE && along > 0 && along < m->p2 && fabsf(across) < m->p3 + 1.6f) {
            push(g, in, v3_scale(v3(-fwd.z, 0, fwd.x), across >= 0 ? 1.0f : -1.0f));
            in->sprint = true;
            evading = true;
        } else if (m->kind == BMK_SLAM && dist < m->p1 + 1.0f && cb->tell_k > 0.86f && PLAYER(g).c.grounded) {
            in->jump = true; jumping = true;   // the ring is about to come out of the ground
        }
    }

    // --- spacing and circling --------------------------------------------------------------
    if (!evading) {
        if (g->tick >= s->strafe_until) { s->strafe = -s->strafe; s->strafe_until = g->tick + BB_STRAFE_TIME; }
        float hold_min, hold_max; hold_band(&hold_min, &hold_max);
        Vec3 want = v3_scale(right, s->strafe);
        // Too close to a thing four metres tall is not a place to fight from: nothing on the floor
        // is visible from under it. Getting out is worth a sprint; getting back in is not.
        if (dist < hold_min) { want = v3_add(want, v3_scale(away, 2.6f)); in->sprint = true; }
        else if (dist > hold_max) want = v3_sub(want, v3_scale(away, 1.2f));
        // Every ten seconds, go and stand on something. Height is not just cosmetic here: the
        // shockwave cannot climb and the charge runs underneath.
        if (g->tick >= s->next_platform) {
            s->have_platform = nearest_platform(g, 13.0f, &s->platform);
            s->next_platform = g->tick + BB_PLATFORM_EVERY;
        }
        if (s->have_platform) {
            Vec3 d = v3_sub(s->platform, me); d.y = 0;
            if (v3_len(d) < 1.6f || me.y > s->platform.y - 0.4f) s->have_platform = false;
            else want = v3_add(v3_scale(v3_norm(d), 1.6f), v3_scale(want, 0.4f));
        }
        push(g, in, want);
    }

    // Wanting to move and not moving is a ledge, and a jump is how you get over a ledge: the same
    // press opens a mantle when there is one, which is how the bot ends up on the platforms.
    if (!jumping && (in->move_x != 0 || in->move_y != 0) && v3_len(PLAYER(g).c.hvel) < 1.2f) {
        if (++s->stuck > BB_STUCK_TICKS && g->tick - s->last_jump > 20) { in->jump = true; s->last_jump = g->tick; s->stuck = 0; }
    } else s->stuck = 0;

    // --- shooting ---------------------------------------------------------------------------
    if (w->item < 0) return true;                 // no gun: nothing else to do but keep moving
    if (w->ammo <= 0 && w->reload <= 0) { in->key_down[SDL_SCANCODE_R] = true; return true; }
    if (w->reload > 0) return true;
    if (aim_error_deg(g, eye, chest) < BB_FIRE_DEG) {
        in->mouse_held = true; in->click = true;
        s->shots++;
    }
    // A line a second while the health is moving, so a headless log shows the fight happening
    // rather than only its ending.
    if (g->tick % 60 == 0) {
        dbg_log("bossbot: boss %.0f hp (%+.0f), wind %.0f, %.1f m, %s%s%s",
                (double)bc->hp, (double)(bc->hp - s->last_hp), (double)w->wind, (double)dist,
                cb->b.phase2 ? "phase2 " : "", cb->stag_t > 0 ? "STAGGER " : "",
                cb->b.state == BS_WINDUP ? cb->b.def.moves[cb->b.move].name : "");
        s->last_hp = bc->hp;
    }
    return true;
}
