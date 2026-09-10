// Items: grabbing, carrying, throwing, dropping and breaking. The physics is in phys.c; this file
// is the game on top of it.
//
// Carrying is a spring, not a socket. A held item is an ordinary rigid body pulled toward a point
// 1.2 m in front of the carrier's eye, so it lags when you turn, swings when you stop, drags on the
// floor when it is heavy, and can be knocked out of your hands by a doorframe: stretch the spring
// past ITEM_BREAK_LEASH and you have dropped it. That is the entire comedy engine of the game.
//
// Authority: the host owns every item. A client predicts only the one in its own hands and pins the
// rest where the interpolation puts them (kinematic bodies, so the predicted item still collides
// with them). Grab, throw and drop travel as reliable messages; the host validates the distance.
#include "game.h"
#include "items.h"
#include "audio.h"
#include "debug.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------- ground under the physics world
static void items_ground(void *ud, float x, float z, float *h, Vec3 *n) {
    Game *g = (Game *)ud;
    if (!g->terrain.present) { *h = 0; *n = v3(0, 1, 0); return; }
    *h = terrain_height(&g->terrain, x, z);
    *n = terrain_normal(&g->terrain, x, z);
}

static float eye_h(const Character *c) { return fmaxf(0.6f, c->height * 0.92f); }
static Vec3 eye_of(const Game *g, int slot) {
    const Character *c = &g->players[slot].c;
    if (slot == g->local && g->cam.mode == CAM_FIRST) return g->cam.eye;
    return v3(c->pos.x, c->pos.y + eye_h(c), c->pos.z);
}

Vec3 items_look_dir(const Game *g, int slot) {
    // The local player looks where the camera looks (both first and third person aim down the same
    // ray). Everyone else is replicated by yaw only, so they aim along their facing, tilted down a
    // little because loot is generally on the floor.
    if (slot == g->local && g->level.view != VIEW_TOP) {
        Vec3 d = v3_sub(g->cam.target, g->cam.eye);
        if (v3_len(d) > 1e-4f) return v3_norm(d);
    }
    float yaw = g->players[slot].c.yaw;
    return v3_norm(v3(sinf(yaw), -0.20f, cosf(yaw)));
}
Vec3 items_hold_point(const Game *g, int slot) {
    return v3_add(eye_of(g, slot), v3_scale(items_look_dir(g, slot), ITEM_HOLD_DIST));
}

// ---------------------------------------------------------------- lifetime
void items_reset(Game *g) {
    Items *its = &g->items;
    ItemDef defs[ITEMDEF_MAX]; int ndefs = its->ndefs;
    memcpy(defs, its->defs, sizeof defs);          // the parsed files survive a level change
    memset(its, 0, sizeof *its);
    memcpy(its->defs, defs, sizeof defs); its->ndefs = ndefs;
    its->next_id = 1; its->look_at = -1;
    for (int i = 0; i < 4; i++) its->carry[i].item = -1;
    // phys_init, not phys_clear: this is the only place the physics world is ever set up, and
    // phys_clear deliberately keeps the gravity it was given, which is zero on a world nobody has
    // initialised. Items floated gently out to sea for an afternoon over that.
    phys_init(&g->phys);
    g->phys.lv = &g->level;
    g->phys.ground = items_ground; g->phys.ground_ud = g;
}

static void find_hold_volume(Game *g) {
    Items *its = &g->items;
    const Trigger *t = level_trigger_named(&g->level, "hold");
    its->hold_valid = t != NULL;
    if (t) { its->hold_min = t->vmin; its->hold_max = t->vmax; }
}

void items_load_level(Game *g) {
    Items *its = &g->items;
    items_reset(g);
    find_hold_volume(g);
    for (int i = 0; i < g->level.nitems && its->n < ITEMS_MAX; i++) {
        const LevelItem *li = &g->level.items[i];
        int d = itemdef_get(its, li->name);
        if (d < 0) continue;
        const ItemDef *def = &its->defs[d];
        Item *it = &its->it[its->n];
        memset(it, 0, sizeof *it);
        Quat rot = quat_from_axis_angle(v3(0, 1, 0), li->yaw);
        int body = def->radius > 0 ? phys_add_sphere(&g->phys, li->pos, rot, def->radius, def->mass)
                                   : phys_add_box(&g->phys, li->pos, rot, def->half, def->mass);
        if (body < 0) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "items: out of physics bodies at '%s'", li->name); break; }
        g->phys.b[body].user = its->n;
        // Placement check: an item authored above thin air (on a prop that carries no collider, say
        // a decorative deck) falls to whatever is really under it, which on this island is usually
        // the sea floor. Say so at load time rather than leaving it to be discovered by a bot.
        float base = g->terrain.present ? terrain_height(&g->terrain, li->pos.x, li->pos.z) : 0.0f;
        float ground = level_ground(&g->level, li->pos, base, NULL);
        if (li->pos.y - ground > 1.0f)
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "item '%s' at %.1f %.1f %.1f sits %.1f m above the nearest solid surface (%.2f) and will fall",
                        li->name, (double)li->pos.x, (double)li->pos.y, (double)li->pos.z, (double)(li->pos.y - ground), (double)ground);
        if (g->terrain.present) {   // and a slope steeper than the friction can hold is a slide into the sea
            Vec3 nrm = terrain_normal(&g->terrain, li->pos.x, li->pos.z);
            float slope = acosf(clampf(nrm.y, -1, 1)) / DEG2RAD;
            if (slope > 25.0f)
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "item '%s' at %.1f %.1f %.1f is on a %.0f degree slope and will slide off it",
                            li->name, (double)li->pos.x, (double)li->pos.y, (double)li->pos.z, (double)slope);
        }
        it->used = true; it->def = d; it->body = body; it->id = its->next_id++;
        it->held_by = -1; it->pos = li->pos; it->rot = rot; it->dirty = true;
        its->n++;
    }
    // HOLLOW_ITEM_STRESS=N: keep cloning what the level placed, in a grid over the spawn, until
    // there are N items. The milestone asks for a physics budget at 64 objects and no level has
    // 64 pieces of loot in it yet.
    const char *stress = SDL_getenv("HOLLOW_ITEM_STRESS");
    if (stress && its->n > 0) {
        int want = atoi(stress); if (want > ITEMS_MAX) want = ITEMS_MAX;
        int seed_n = its->n;
        for (int k = 0; its->n < want; k++) {
            const Item *src = &its->it[k % seed_n];
            const ItemDef *def = &its->defs[src->def];
            Vec3 pos = v3(g->level.spawn.x + (float)(its->n % 8) * 1.1f - 4.0f,
                          g->level.spawn.y + 2.0f + (float)(its->n / 8) * 0.9f,
                          g->level.spawn.z + (float)((its->n / 8) % 8) * 1.1f - 4.0f);
            int body = def->radius > 0 ? phys_add_sphere(&g->phys, pos, quat_identity(), def->radius, def->mass)
                                       : phys_add_box(&g->phys, pos, quat_identity(), def->half, def->mass);
            if (body < 0) break;
            Item *it = &its->it[its->n]; memset(it, 0, sizeof *it);
            g->phys.b[body].user = its->n;
            it->used = true; it->def = src->def; it->body = body; it->id = its->next_id++;
            it->held_by = -1; it->pos = pos; it->rot = quat_identity(); it->dirty = true;
            its->n++;
        }
        SDL_Log("item stress: %d items", its->n);
    }
    if (its->n) dbg_log("items: %d placed, hold volume %s", its->n, its->hold_valid ? "found" : "missing");
}

int items_find_id(const Items *its, uint16_t id) {
    for (int i = 0; i < its->n; i++) if (its->it[i].used && its->it[i].id == id) return i;
    return -1;
}

// ---------------------------------------------------------------- looking at things
int items_look_target(const Game *g, int slot) {
    const Items *its = &g->items;
    Vec3 eye = eye_of(g, slot), dir = items_look_dir(g, slot);
    int best = -1; float best_d = 1e9f;
    for (int i = 0; i < its->n; i++) {
        const Item *it = &its->it[i];
        if (!it->used || it->broken || it->held_by >= 0 || it->drop_lock > 0) continue;
        Vec3 d = v3_sub(it->pos, eye);
        float dist = v3_len(d);
        if (dist > ITEM_REACH || dist < 1e-3f) continue;
        if (v3_dot(v3_scale(d, 1.0f / dist), dir) < 0.55f) continue;   // a ~57 degree cone, not a laser
        if (dist < best_d) { best_d = dist; best = i; }
    }
    return best;
}

float items_speed_scale(const Game *g, int slot) { return items_two_handed(g, slot) ? ITEM_SLOW_SPEED : 1.0f; }
bool items_two_handed(const Game *g, int slot) {
    if (slot < 0 || slot >= 4) return false;
    int i = g->items.carry[slot].item;
    if (i < 0 || i >= g->items.n || !g->items.it[i].used) return false;
    return g->items.defs[g->items.it[i].def].two_handed;
}
bool items_hold_center(const Game *g, Vec3 *out) {
    if (!g->items.hold_valid) return false;
    *out = v3_scale(v3_add(g->items.hold_min, g->items.hold_max), 0.5f);
    return true;
}
bool items_in_hold_volume(const Game *g, Vec3 p) {
    const Items *its = &g->items;
    return its->hold_valid && p.x >= its->hold_min.x && p.x <= its->hold_max.x
        && p.y >= its->hold_min.y && p.y <= its->hold_max.y
        && p.z >= its->hold_min.z && p.z <= its->hold_max.z;
}
void items_hold_totals(const Game *g, int *count, int *value) {
    if (count) *count = g->items.hold_count;
    if (value) *value = g->items.hold_value;
}
void items_set_hold_totals(Game *g, int count, int value) { g->items.hold_count = count; g->items.hold_value = value; }

// ---------------------------------------------------------------- grab, release, break
bool items_grab(Game *g, int slot, int idx) {
    Items *its = &g->items;
    if (slot < 0 || slot >= 4 || idx < 0 || idx >= its->n) return false;
    Item *it = &its->it[idx];
    if (!it->used || it->broken || it->held_by >= 0 || it->drop_lock > 0) return false;
    if (its->carry[slot].item >= 0) return false;
    // Reach, generously: the client asked from where it was a round trip ago.
    if (v3_len(v3_sub(it->pos, eye_of(g, slot))) > ITEM_REACH + 1.0f) return false;
    it->held_by = slot; it->dirty = true; it->leash_armed = false; it->leash_t = 0;
    its->carry[slot].item = idx; its->carry[slot].charge = 0; its->carry[slot].charging = false;
    PhysBody *b = phys_body(&g->phys, it->body);
    if (b) { b->kinematic = false; phys_wake(&g->phys, it->body); }
    audio_play(SND_GRAB, 0.6f, 1.0f);
    dbg_log("item %u (%s) grabbed by slot %d", it->id, its->defs[it->def].display, slot);
    return true;
}

void items_release(Game *g, int slot, bool thrown, Vec3 vel) {
    Items *its = &g->items;
    if (slot < 0 || slot >= 4) return;
    int idx = its->carry[slot].item;
    if (idx < 0 || idx >= its->n) return;
    Item *it = &its->it[idx];
    its->carry[slot].item = -1; its->carry[slot].charge = 0; its->carry[slot].charging = false;
    if (!it->used) return;
    it->held_by = -1; it->dirty = true;
    it->drop_lock = 0.35f;   // long enough that the same E press cannot pick it straight back up
    PhysBody *b = phys_body(&g->phys, it->body);
    if (b) {
        phys_wake(&g->phys, it->body);
        if (thrown) {
            // Off-centre, so a thrown crate tumbles rather than sailing like a dart.
            Vec3 side = v3_norm(v3_cross(v3_len(vel) > 1e-4f ? vel : v3(0, 0, 1), v3(0, 1, 0)));
            b->vel = v3(0, 0, 0);
            phys_impulse(&g->phys, it->body, v3_scale(vel, b->mass), v3_add(b->pos, v3_scale(side, b->radius * 0.4f)));
            dbg_log("item %u thrown by slot %d at %.1f m/s", it->id, slot, (double)v3_len(vel));
        } else {
            b->vel = v3_scale(b->vel, 0.4f);
            audio_play(SND_DROP, 0.5f, 1.0f);
            dbg_log("item %u dropped by slot %d", it->id, slot);
        }
    }
}

void items_break(Game *g, int idx) {
    Items *its = &g->items;
    if (idx < 0 || idx >= its->n) return;
    Item *it = &its->it[idx];
    if (!it->used || it->broken) return;
    const ItemDef *d = &its->defs[it->def];
    Vec3 at = it->pos;
    if (it->held_by >= 0 && it->held_by < 4) its->carry[it->held_by].item = -1;
    it->held_by = -1; it->broken = true; it->dirty = true; it->seen_broken = true;
    if (it->body >= 0) { phys_remove(&g->phys, it->body); it->body = -1; }
    float size = d->radius > 0 ? d->radius : fmaxf(d->half.x, fmaxf(d->half.y, d->half.z));
    debris_burst(its, at, d->tint, size * 0.55f, 6 + (int)(it->id % 5), 3.6f);
    particles_burst(&g->particles, PT_SPARK, at, v3(0, 0.6f, 0), 22, 5.0f,
                    v3(d->tint.x * 2.2f, d->tint.y * 2.0f, d->tint.z * 1.8f), 0.06f, 0.55f);
    audio_play(d->sound >= 0 ? (SoundId)d->sound : SND_SMASH, 0.9f, 0.95f + 0.1f * (float)(it->id % 3));
    its->lost_value = d->value; its->lost_t = 2.5f; its->breaks++;
    int pieces = 0; for (int k = 0; k < DEBRIS_MAX; k++) if (its->debris[k].used) pieces++;
    dbg_log("item %u (%s) broke, -$%d, %d debris pieces in flight", it->id, d->display, d->value, pieces);
}

// ---------------------------------------------------------------- network entry points
bool items_net_grab(Game *g, int slot, uint16_t id) {
    int idx = items_find_id(&g->items, id);
    if (idx < 0) return false;
    return items_grab(g, slot, idx);
}
void items_net_release(Game *g, int slot, uint16_t id, bool thrown, Vec3 vel) {
    int idx = items_find_id(&g->items, id);
    if (idx < 0 || g->items.carry[slot].item != idx) return;
    float speed = v3_len(vel);
    if (speed > 25.0f) vel = v3_scale(vel, 25.0f / speed);   // a client does not get to invent a railgun
    items_release(g, slot, thrown, vel);
}
void items_net_sample(Game *g, uint16_t id, Vec3 pos, Quat rot, int held_by, bool broken, bool in_hold, double t) {
    Items *its = &g->items;
    int idx = items_find_id(its, id);
    if (idx < 0) return;
    Item *it = &its->it[idx];
    it->held_by = held_by;
    it->in_hold = in_hold;   // the host decides what counts; a client bot must not fish it back out
    // Reconcile our own hands with the host's answer. A grab someone else won has to come out of
    // them, and one the host granted has to go into them -- but not while our own prediction is
    // still in flight, or every snapshot older than the round trip would undo the grab we just made.
    Carry *lc = &its->carry[g->local];
    if (lc->pending <= 0) {
        if (held_by == g->local && lc->item != idx && lc->item < 0) { lc->item = idx; lc->charge = 0; lc->charging = false; it->leash_armed = false; }
        else if (held_by != g->local && lc->item == idx) { lc->item = -1; lc->charge = 0; lc->charging = false; }
    }
    if (broken && !it->seen_broken) { items_break(g, idx); return; }   // debris is local; the host only says "gone"
    if (it->broken) return;
    if (it->nhist > 0 && t <= it->hist[it->nhist - 1].t) return;
    if (it->nhist == ITEM_HIST) { memmove(it->hist, it->hist + 1, sizeof it->hist[0] * (ITEM_HIST - 1)); it->nhist--; }
    it->hist[it->nhist].t = t; it->hist[it->nhist].pos = pos; it->hist[it->nhist].rot = rot; it->nhist++;
}

// A client draws every item it does not hold NET_INTERP_DELAY in the past, exactly like a remote
// player, and pins its body there so the item it does hold still bumps into them.
static void interpolate_items(Game *g) {
    Items *its = &g->items;
    double rt = g->net.now - NET_INTERP_DELAY;
    for (int i = 0; i < its->n; i++) {
        Item *it = &its->it[i];
        if (!it->used || it->broken || it->nhist == 0) continue;
        if (it->held_by == g->local) continue;   // predicted locally, not replayed
        Vec3 pos; Quat rot;
        if (rt <= it->hist[0].t) { pos = it->hist[0].pos; rot = it->hist[0].rot; }
        else if (rt >= it->hist[it->nhist - 1].t) { pos = it->hist[it->nhist - 1].pos; rot = it->hist[it->nhist - 1].rot; }
        else {
            int k = it->nhist - 1;
            while (k > 0 && it->hist[k - 1].t > rt) k--;
            double span = it->hist[k].t - it->hist[k - 1].t;
            float u = span > 1e-6 ? (float)((rt - it->hist[k - 1].t) / span) : 1.0f;
            pos = v3_lerp(it->hist[k - 1].pos, it->hist[k].pos, u);
            rot = quat_slerp(it->hist[k - 1].rot, it->hist[k].rot, u);
        }
        it->pos = pos; it->rot = rot;
        PhysBody *b = phys_body(&g->phys, it->body);
        if (b) { b->kinematic = true; phys_place(&g->phys, it->body, pos, rot, true); }
    }
}

// ---------------------------------------------------------------- carrying
static float throw_speed(const ItemDef *d, float charge01) {
    // Mass is the whole joke: the tape reel goes across the courtyard, the bust goes over the rail.
    float heft = clampf(6.0f / fmaxf(d->mass, 0.1f), 0.30f, 1.7f);
    return (3.0f + 10.0f * charge01) * heft;
}

static void hold_spring(Game *g, int slot, float dt) {
    Items *its = &g->items;
    int idx = its->carry[slot].item;
    if (idx < 0) return;
    Item *it = &its->it[idx];
    PhysBody *b = phys_body(&g->phys, it->body);
    if (!b) { its->carry[slot].item = -1; return; }
    b->kinematic = false;
    const ItemDef *d = &its->defs[it->def];
    Vec3 target = items_hold_point(g, slot);
    Vec3 err = v3_sub(target, b->pos);
    float stretch = v3_len(err);
    // The leash only bites once the spring has actually reeled the thing in: you grab from up to
    // 2.5 m away, which is further than the leash, and dropping it on the frame you picked it up
    // would be a very short game.
    if (!it->leash_armed && stretch < ITEM_BREAK_LEASH * 0.75f) it->leash_armed = true;
    // A swing past the leash is not a drop; a quarter second hung up on a doorframe is.
    it->leash_t = (it->leash_armed && stretch > ITEM_BREAK_LEASH) ? it->leash_t + dt : 0.0f;
    if (it->leash_t > 0.22f) {
        dbg_log("item %u pulled out of slot %d's hands (%.2f m)", it->id, slot, (double)stretch);
        items_release(g, slot, false, v3(0, 0, 0));
        return;
    }
    // Critically damped, softened by mass: a heavy thing is always a moment behind where you meant
    // it to be. Gravity is cancelled here, so what is left is lag, swing and whatever it hits.
    float omega = fmaxf(6.0f, 16.0f / (1.0f + d->mass * 0.030f));
    Vec3 a = v3_sub(v3_scale(err, omega * omega), v3_scale(b->vel, 2.0f * omega));
    a.y += g->phys.gravity;
    float amag = v3_len(a);
    if (amag > 120.0f) a = v3_scale(a, 120.0f / amag);   // hands are strong, not hydraulic
    phys_accelerate(&g->phys, it->body, a, dt);
    // A held item never travels faster than a person can swing one. Without this the spring can
    // catapult something that was briefly snagged, and a vase would shatter on the next doorframe
    // at a speed no player caused.
    float vs = v3_len(b->vel);
    if (vs > 6.0f) b->vel = v3_scale(b->vel, 6.0f / vs);
    // Weakly upright, free in yaw: the item rights itself over a second or so and still spins.
    Vec3 up = quat_rotate(b->rot, v3(0, 1, 0));
    Vec3 axis = v3_cross(up, v3(0, 1, 0));
    Vec3 ang = v3_sub(v3_scale(axis, 16.0f), v3_scale(b->avel, 5.0f));
    float amax = v3_len(ang);
    if (amax > 60.0f) ang = v3_scale(ang, 60.0f / amax);
    phys_torque(&g->phys, it->body, ang, dt);
}

// The local player's hands. On a client this also fires the reliable message the host acts on, and
// predicts the result immediately so the item is in your hands on the frame you pressed the key.
static void local_carry_input(Game *g, const Input *in, float dt) {
    Items *its = &g->items;
    int slot = g->local;
    Carry *c = &its->carry[slot];
    bool client = g->net.mode == NM_CLIENT;
    its->look_at = -1;

    if (c->item < 0) {
        its->look_at = items_look_target(g, slot);
        c->charge = 0; c->charging = false;
        if (in->interact && its->look_at >= 0) {
            int idx = its->look_at;
            if (client) { netgame_send_item_grab(g, g->items.it[idx].id); c->pending = 0.6f; }
            items_grab(g, slot, idx);
        }
        return;
    }
    Item *it = &its->it[c->item];
    uint16_t id = it->id;
    const ItemDef *d = &its->defs[it->def];
    // Left mouse charges; letting go throws. Everything else is a drop.
    if (in->mouse_held) { c->charging = true; c->charge = fminf(c->charge + dt, ITEM_CHARGE_MAX); }
    else if (c->charging) {
        float k = c->charge / ITEM_CHARGE_MAX;
        Vec3 vel = v3_scale(items_look_dir(g, slot), throw_speed(d, k));
        vel.y += 1.2f;   // a throw arcs; nobody bowls loot along the floor on purpose
        if (client) { netgame_send_item_release(g, id, true, vel); c->pending = 0.6f; }
        items_release(g, slot, true, vel);
        return;
    }
    if (in->interact || in->rclick) {
        if (client) { netgame_send_item_release(g, id, false, v3(0, 0, 0)); c->pending = 0.6f; }
        items_release(g, slot, false, v3(0, 0, 0));
    }
}

// ---------------------------------------------------------------- the tick
static void update_hold_totals(Game *g) {
    Items *its = &g->items;
    int count = 0, value = 0;
    for (int i = 0; i < its->n; i++) {
        Item *it = &its->it[i];
        it->in_hold = false;
        if (!it->used || it->broken || it->held_by >= 0 || !its->hold_valid) continue;
        Vec3 p = it->pos;
        if (p.x < its->hold_min.x || p.x > its->hold_max.x) continue;
        if (p.y < its->hold_min.y || p.y > its->hold_max.y) continue;
        if (p.z < its->hold_min.z || p.z > its->hold_max.z) continue;
        it->in_hold = true; count++; value += its->defs[it->def].value;
    }
    if (count != its->hold_count || value != its->hold_value)
        dbg_log("hold: %d items, $%d", count, value);
    its->hold_count = count; its->hold_value = value;
}

void items_tick(Game *g, const Input *in, float dt) {
    Items *its = &g->items;
    Uint64 t0 = SDL_GetPerformanceCounter();
    if (its->n == 0 && g->level.nitems == 0) { its->last_ms = 0; return; }
    if (!its->hold_valid) find_hold_volume(g);
    g->phys.lv = &g->level; g->phys.ground = items_ground; g->phys.ground_ud = g;
    bool client = g->net.mode == NM_CLIENT;

    for (int i = 0; i < its->n; i++) if (its->it[i].drop_lock > 0) its->it[i].drop_lock -= dt;
    for (int i = 0; i < 4; i++) if (its->carry[i].pending > 0) its->carry[i].pending -= dt;
    if (its->lost_t > 0) its->lost_t -= dt;

    if (client) interpolate_items(g);
    local_carry_input(g, in, dt);
    // The host pulls every carried item along; a client only its own (the rest are pinned replicas).
    if (client) hold_spring(g, g->local, dt);
    else for (int s = 0; s < 4; s++) if (g->net.slots[s].active) hold_spring(g, s, dt);

    phys_step(&g->phys, dt);

    for (int i = 0; i < its->n; i++) {
        Item *it = &its->it[i];
        if (!it->used || it->broken) continue;
        PhysBody *b = phys_body(&g->phys, it->body);
        if (!b) continue;
        if (!b->kinematic) {
            if (v3_len(v3_sub(b->pos, it->pos)) > 0.002f) it->dirty = true;
            it->pos = b->pos; it->rot = b->rot;
        }
        // Fragile: the hardest thing it hit this tick decides. The host is the only one that
        // breaks things on impact; a client waits to be told, so both sides agree on the money.
        const ItemDef *d = &its->defs[it->def];
        if (!client && d->fragile > 0 && b->impact > d->fragile) {
            dbg_log("item %u (%s) hit at %.1f m/s, threshold %.1f", it->id, d->display, (double)b->impact, (double)d->fragile);
            items_break(g, i);
            continue;
        }
        b->impact = 0;
        it->sunk = g->terrain.present && g->terrain.water > -900.0f && it->pos.y < g->terrain.water - 0.5f;
    }
    if (!client) update_hold_totals(g);
    // HOLLOW_ITEM_TRACE: once a second, where every item is and what it is doing. The only way to
    // tell "settled in the hold" from "quietly sliding into the sea" in a headless run.
    if (SDL_getenv("HOLLOW_ITEM_TRACE") && g->tick % 60 == 0)
        dbg_log("phys: %d items, %u awake, %u contacts, %.3f ms physics + %.3f ms items",
                its->n, g->phys.awake, g->phys.contacts, (double)g->phys.last_ms, (double)its->last_ms);
    if (SDL_getenv("HOLLOW_ITEM_TRACE") && SDL_getenv("HOLLOW_ITEM_VERBOSE") && g->tick % 60 == 0)
        for (int i = 0; i < its->n; i++) {
            const Item *it = &its->it[i];
            if (!it->used) continue;
            const PhysBody *pb = phys_body_c(&g->phys, it->body);
            float gh = 0; Vec3 gn = v3(0, 1, 0); items_ground(g, it->pos.x, it->pos.z, &gh, &gn);
            dbg_log("item trace: %u %-28s pos %6.1f %5.2f %7.1f vel %5.2f %5.2f %5.2f ground %6.2f held %d %s%s%s",
                    it->id, its->defs[it->def].display,
                    (double)it->pos.x, (double)it->pos.y, (double)it->pos.z,
                    pb ? (double)pb->vel.x : 0.0, pb ? (double)pb->vel.y : 0.0, pb ? (double)pb->vel.z : 0.0, (double)gh,
                    it->held_by, it->broken ? "BROKEN " : "", it->in_hold ? "IN-HOLD " : "", pb && pb->sleeping ? "asleep" : "awake");
        }
    debris_update(g, dt);
    its->last_ms = (float)((double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency());
}

// ---------------------------------------------------------------- drawing
void items_draw(Game *g) {
    Items *its = &g->items;
    for (int i = 0; i < its->n; i++) {
        const Item *it = &its->it[i];
        if (!it->used || it->broken) continue;
        const ItemDef *d = &its->defs[it->def];
        if (!d->ok || !d->model[0]) continue;
        float s = d->scale;
        props_draw_matrix(&g->gfx, &g->props, d->model, m4_from_trs(it->pos, it->rot, v3(s, s, s)), d->tint, v3(0, 0, 0), NULL, 0, 0);
    }
    gfx_set_material(&g->gfx, NULL);
    if (g->gfx.in_shadow) return;   // the sun pass wants the loot's silhouette, not its confetti
    debris_draw(g);
    if (!SDL_getenv("HOLLOW_NOBLOB"))
        for (int i = 0; i < its->n; i++) {
            const Item *it = &its->it[i];
            if (!it->used || it->broken) continue;
            const ItemDef *d = &its->defs[it->def];
            float r = d->radius > 0 ? d->radius : fmaxf(d->half.x, d->half.z);
            draw_blob_shadow(&g->gfx, it->pos, r * 2.0f, 0.4f);
        }
}
