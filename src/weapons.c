// Weapons: equipping, firing, hitting and falling over. The drawing is in weaponview.c and the
// test bot is in weaponbot.c; this file is the rules. See weapons.h for the shape of it.
//
// Authority, in one paragraph. Everything that matters happens on the host. A client presses the
// button, plays its own sound and kick immediately so the gun feels connected to the mouse, and
// sends a reliable FIRE carrying the eye and the aim it fired from. The host checks that slot has
// that weapon, that it has a round left and that its rate limit has expired, re-runs the hitscan
// against its own copy of the world, and broadcasts one small unreliable event so every client
// draws the same tracer over the same corpse-free comedy. A client's shot never decides anything.
#include "game.h"
#include "weapons.h"
#include "audio.h"
#include "debug.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------- small helpers

static Weapons *W(Game *g) { return &g->weapons; }

static const ItemDef *wdef(const Game *g, int item) {
    const Items *its = &g->items;
    if (item < 0 || item >= its->n || !its->it[item].used) return NULL;
    const ItemDef *d = &its->defs[its->it[item].def];
    return d->ok ? d : NULL;
}

WeaponKind weapon_kind_of(const Game *g, int item) {
    const ItemDef *d = wdef(g, item);
    return d ? (WeaponKind)d->weapon : WK_NONE;
}

bool weapons_holds_item(const Game *g, int slot, int item) {
    return slot >= 0 && slot < NET_MAX_PLAYERS && item >= 0 && g->weapons.w[slot].item == item;
}

bool weapons_is_down(const Game *g, int slot) {
    return slot >= 0 && slot < NET_MAX_PLAYERS && g->weapons.dn[slot].down;
}

// Down, or halfway back up: either way the controls are somebody else's problem for a moment.
bool weapons_frozen(const Game *g, int slot) {
    if (slot < 0 || slot >= NET_MAX_PLAYERS) return false;
    const Downed *d = &g->weapons.dn[slot];
    return d->down || d->getup > 0;
}

bool weapons_drawn(const Game *g, int slot) {
    if (slot < 0 || slot >= NET_MAX_PLAYERS) return false;
    const Weapon *w = &g->weapons.w[slot];
    return w->item >= 0 && w->drawn && !weapons_frozen(g, slot);
}

bool weapons_camera(const Game *g, float *roll_deg, float *eye_height) {
    const Downed *d = &g->weapons.dn[g->local];
    if (!d->down && d->getup <= 0) return false;
    // Going over takes a moment and getting up takes another; the roll follows both so the horizon
    // swings rather than snapping. Down for real is the full lie-down.
    float k = d->down ? clampf(d->t / 0.45f, 0, 1) : clampf(d->getup / 0.55f, 0, 1);
    if (roll_deg)   *roll_deg = WEAP_DOWN_ROLL * k;
    if (eye_height) *eye_height = lerpf(fmaxf(0.6f, g->players[g->local].c.height * 0.92f), WEAP_DOWN_EYE, k);
    return true;
}

// The eye and the aim a slot shoots from. The local player aims down the camera; everyone else is
// replicated by yaw alone, so they aim along their facing, level.
Vec3 weapons_eye(const Game *g, int slot) {
    const Character *c = &g->players[slot].c;
    if (slot == g->local && g->cam.mode == CAM_FIRST) return g->cam.eye;
    return v3(c->pos.x, c->pos.y + fmaxf(0.6f, c->height * 0.92f), c->pos.z);
}
Vec3 weapons_aim(const Game *g, int slot) {
    if (slot == g->local && g->level.view != VIEW_TOP) {
        Vec3 d = v3_sub(g->cam.target, g->cam.eye);
        if (v3_len(d) > 1e-4f) return v3_norm(d);
    }
    float yaw = g->players[slot].c.yaw;
    return v3_norm(v3(sinf(yaw), 0, cosf(yaw)));
}

// ---------------------------------------------------------------- equipping

bool weapons_equip(Game *g, int slot, int item) {
    if (slot < 0 || slot >= NET_MAX_PLAYERS) return false;
    Items *its = &g->items;
    if (item < 0 || item >= its->n || !its->it[item].used) return false;
    Item *it = &its->it[item];
    const ItemDef *d = wdef(g, item);
    if (!d || d->weapon == 0) return false;
    if (it->broken || it->held_by >= 0 || it->drop_lock > 0) return false;
    Weapon *w = &W(g)->w[slot];
    if (w->item >= 0) return false;                       // one hand, one weapon
    if (weapons_frozen(g, slot)) return false;
    // Reach, generously, exactly as items_grab does: the client asked from where it was a round
    // trip ago and refusing it for ten centimetres is worse than letting it have the bat.
    if (v3_len(v3_sub(it->pos, weapons_eye(g, slot))) > ITEM_REACH + 1.0f) return false;

    items_body_detach(g, item);
    it->held_by = slot; it->weapon_hand = true; it->dirty = true;
    w->item = item;
    w->ammo = d->weapon == 2 ? d->ammo : 0;
    w->cool = 0.25f; w->reload = 0; w->swing = 0; w->swing_hit = false; w->kick = 0;
    // Two hands on the painting is no hands for the gun: it comes out already holstered.
    w->drawn = !items_two_handed(g, slot);
    W(g)->swap_t = 0.25f;
    audio_play(SND_GRAB, 0.6f, 0.85f);
    dbg_log("weapon: slot %d picked up %s (%s, %d rounds)", slot, d->display, d->weapon == 2 ? "gun" : "melee", w->ammo);
    return true;
}

void weapons_drop(Game *g, int slot) {
    if (slot < 0 || slot >= NET_MAX_PLAYERS) return;
    Weapon *w = &W(g)->w[slot];
    int item = w->item;
    if (item < 0) return;
    Items *its = &g->items;
    *w = (Weapon){ .item = -1, .wind = w->wind, .wind_quiet = w->wind_quiet };
    if (item >= its->n || !its->it[item].used) return;
    Item *it = &its->it[item];
    // Put it down where the hand is, not where the last snapshot left the body, or a dropped gun
    // reappears wherever its owner picked it up.
    it->pos = weapons_hand_point(g, slot);
    it->held_by = -1; it->weapon_hand = false; it->dirty = true;
    it->drop_lock = 0.35f;
    if (items_body_attach(g, item)) {
        PhysBody *b = phys_body(&g->phys, it->body);
        if (b) { b->vel = v3_scale(weapons_aim(g, slot), 1.2f); b->vel.y += 0.6f; phys_wake(&g->phys, it->body); }
    }
    audio_play(SND_DROP, 0.5f, 1.1f);
    dbg_log("weapon: slot %d put down item %u", slot, it->id);
}

void weapons_swap(Game *g, int slot) {
    if (slot < 0 || slot >= NET_MAX_PLAYERS) return;
    Weapon *w = &W(g)->w[slot];
    if (w->item < 0 || weapons_frozen(g, slot)) return;
    // Both hands on a crate means the weapon stays on your back whatever you press.
    if (items_two_handed(g, slot)) { w->drawn = false; return; }
    w->drawn = !w->drawn;
    w->reload = 0; w->swing = 0;
    if (slot == g->local) W(g)->swap_t = 0.25f;
    audio_play(SND_CLICK, 0.35f, w->drawn ? 1.3f : 0.9f);
}

void weapons_client_hold(Game *g, int slot, int item) {
    if (slot < 0 || slot >= NET_MAX_PLAYERS || item < 0) return;
    Weapon *w = &W(g)->w[slot];
    if (w->item == item) return;
    w->item = item;
    if (w->ammo <= 0) { const ItemDef *d = wdef(g, item); w->ammo = d ? d->ammo : 0; }
}

// ---------------------------------------------------------------- where the weapon is

// The grip point of the weapon hand. Off the model's own hand_r when there is a model to ask,
// and off the body otherwise (a sprite character, a slot whose model has not loaded yet).
Vec3 weapons_hand_point(const Game *g, int slot) {
    Mat4 M;
    const CharModel *cm = &g->player_models[slot];
    if (cm->loaded && !cm->is_sprite && charmodel_bone_world(cm, &g->players[slot].c, "hand_r", &M))
        return v3(M.m[12], M.m[13], M.m[14]);
    const Character *c = &g->players[slot].c;
    Vec3 f = v3(sinf(c->yaw), 0, cosf(c->yaw)), r = v3(f.z, 0, -f.x);
    return v3_add(c->pos, v3_add(v3_scale(f, 0.20f), v3_add(v3_scale(r, 0.24f), v3(0, c->height * 0.58f, 0))));
}

// ---------------------------------------------------------------- hitscan

typedef struct RayHit { int kind, idx; Vec3 point, normal; float dist; } RayHit;

// Closest approach of the ray to a capsule standing on `base`, or -1 when it misses.
static float ray_capsule(Vec3 from, Vec3 dir, Vec3 base, float radius, float height, Vec3 *point) {
    // The hit test is a fat cylinder rather than a true capsule: a goon is a comedy silhouette,
    // not a hitbox to be shaved, and the difference is invisible at these ranges.
    float best = -1;
    for (int s = 0; s <= 8; s++) {
        float t = ((float)s / 8.0f) * height;
        Vec3 c = v3(base.x, base.y + t, base.z);
        Vec3 rel = v3_sub(c, from);
        float along = v3_dot(rel, dir);
        if (along < 0.05f) continue;
        Vec3 near_p = v3_add(from, v3_scale(dir, along));
        if (v3_len(v3_sub(near_p, c)) > radius) continue;
        if (best < 0 || along < best) { best = along; if (point) *point = near_p; }
    }
    return best;
}

// The first thing a ray meets. `shooter` is ignored (nobody shoots themselves in a comedy).
static RayHit weapons_ray(Game *g, int shooter, Vec3 from, Vec3 dir, float range) {
    RayHit h = { .kind = FH_NONE, .idx = -1, .dist = range, .point = v3_add(from, v3_scale(dir, range)), .normal = v3_scale(dir, -1) };
    Vec3 to = v3_add(from, v3_scale(dir, range));

    // The world first, so a shot through a wall hits the wall.
    float f = level_ray_solid(&g->level, from, to, 0.0f);
    if (f < 1.0f) { h.kind = FH_WORLD; h.dist = range * f; h.point = v3_add(from, v3_scale(dir, h.dist)); }
    if (g->terrain.present) {
        Vec3 tp;
        if (terrain_ray(&g->terrain, from, v3_add(from, v3_scale(dir, h.dist)), &tp)) {
            float d = v3_len(v3_sub(tp, from));
            if (d < h.dist) { h.kind = FH_WORLD; h.dist = d; h.point = tp; h.normal = terrain_normal(&g->terrain, tp.x, tp.z); }
        }
    }
    // Players.
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (i == shooter || !g->net.slots[i].active) continue;
        const Character *c = &g->players[i].c;
        // A goon already on the floor is a much smaller target, which is the only mercy on offer.
        float height = weapons_is_down(g, i) ? 0.5f : c->height;
        Vec3 p;
        float d = ray_capsule(from, dir, c->pos, c->radius * 1.15f, height, &p);
        if (d >= 0 && d < h.dist) { h.kind = FH_PLAYER; h.idx = i; h.dist = d; h.point = p; h.normal = v3_scale(dir, -1); }
    }
    // Items. A weapon in somebody's hand is part of them, not a target of its own.
    const Items *its = &g->items;
    for (int i = 0; i < its->n; i++) {
        const Item *it = &its->it[i];
        if (!it->used || it->broken || it->weapon_hand) continue;
        const ItemDef *d = &its->defs[it->def];
        float r = d->radius > 0 ? d->radius : fmaxf(d->half.x, fmaxf(d->half.y, d->half.z));
        Vec3 rel = v3_sub(it->pos, from);
        float along = v3_dot(rel, dir);
        if (along < 0.05f || along >= h.dist) continue;
        Vec3 near_p = v3_add(from, v3_scale(dir, along));
        if (v3_len(v3_sub(near_p, it->pos)) > r * 1.1f) continue;
        h.kind = FH_ITEM; h.idx = i; h.dist = along; h.point = near_p; h.normal = v3_scale(dir, -1);
    }
    return h;
}

// ---------------------------------------------------------------- taking a hit

static void knock_down(Game *g, int slot) {
    Weapons *ws = W(g);
    Downed *d = &ws->dn[slot];
    if (d->down) return;
    d->down = true; d->t = 0; d->revive = 0; d->reviver = -1; d->getup = 0;
    ws->w[slot].wind = 0; ws->w[slot].reload = 0; ws->w[slot].swing = 0; ws->w[slot].drawn = false;
    // Whatever was in the loot hand is now on the floor, which is most of the point.
    if (g->items.carry[slot].item >= 0) items_release(g, slot, false, v3(0, 0, 0));
    character_set_anim(&g->players[slot].c, ANIM_KNOCKED);
    ws->knockdowns++;
    FireEvent e = { .slot = (uint8_t)slot, .kind = FE_DOWN, .hit = FH_NONE, .pellets = 1,
                    .from = g->players[slot].c.pos, .to = g->players[slot].c.pos };
    weapons_event(g, &e);
    dbg_log("weapon: slot %d knocked down", slot);
}

static void get_up(Game *g, int slot, const char *why) {
    Weapons *ws = W(g);
    Downed *d = &ws->dn[slot];
    if (!d->down) return;
    d->down = false; d->getup = 0.9f; d->revive = 0; d->reviver = -1;
    ws->w[slot].wind = WEAP_WIND; ws->w[slot].wind_quiet = 0;
    ws->w[slot].drawn = ws->w[slot].item >= 0 && !items_two_handed(g, slot);
    character_set_anim(&g->players[slot].c, ANIM_GETUP);
    FireEvent e = { .slot = (uint8_t)slot, .kind = FE_UP, .hit = FH_NONE, .pellets = 1,
                    .from = g->players[slot].c.pos, .to = g->players[slot].c.pos };
    weapons_event(g, &e);
    dbg_log("weapon: slot %d got up after %.1f s (%s)", slot, (double)d->t, why);
}

// Host only. Wind down, shove back, and over you go when it runs out.
static void hurt_player(Game *g, int slot, float damage, Vec3 dir, float knock) {
    if (slot < 0 || slot >= NET_MAX_PLAYERS || !g->net.slots[slot].active) return;
    Weapon *w = &W(g)->w[slot];
    w->wind_quiet = 0;
    Player *p = &g->players[slot];
    Vec3 push = v3_norm(v3(dir.x, 0, dir.z));
    p->knock = v3_add(p->knock, v3_scale(push, knock));
    p->c.flash = 1.0f;
    if (W(g)->dn[slot].down) return;                  // already on the floor: the shove is enough
    w->wind -= damage;
    if (w->wind <= 0.01f) knock_down(g, slot);
    else character_set_anim(&p->c, damage >= 45.0f ? ANIM_HURT_HEAVY : ANIM_HURT);
}

// Host only. An item takes a shove, and a fragile one that took a solid shove gives up on the spot.
static void hurt_item(Game *g, int item, Vec3 dir, float knock, Vec3 at) {
    Items *its = &g->items;
    if (item < 0 || item >= its->n || !its->it[item].used || its->it[item].broken) return;
    Item *it = &its->it[item];
    const ItemDef *d = &its->defs[it->def];
    PhysBody *b = phys_body(&g->phys, it->body);
    if (b) {
        phys_wake(&g->phys, it->body);
        // Impulse, not velocity: a shot moves the gnome across the terrace and barely troubles the
        // bust, which is the correct amount of physics comedy for one trigger pull.
        Vec3 imp = v3_scale(v3_norm(dir), knock * fminf(b->mass, 20.0f));
        imp.y += knock * 0.25f * fminf(b->mass, 20.0f);
        phys_impulse(&g->phys, it->body, imp, at);
        it->dirty = true;
    }
    if (d->fragile > 0 && knock > d->fragile * 0.6f) items_break(g, item);
}

// ---------------------------------------------------------------- firing

// One shot or one swing, decided by the host (or by a single-player process, which is its own host).
// `origin`/`dir` are the shooter's reported eye and aim; the host has already validated the slot.
static void resolve_fire(Game *g, int slot, Vec3 origin, Vec3 dir) {
    Weapons *ws = W(g);
    Weapon *w = &ws->w[slot];
    const ItemDef *d = wdef(g, w->item);
    if (!d) return;
    bool gun = d->weapon == 2;
    float range = gun ? clampf(d->wrange > 0 ? d->wrange : 30.0f, 1.0f, WEAP_MAX_RANGE) : WEAP_SWING_ARC;
    int pellets = gun ? (d->pellets < 1 ? 1 : d->pellets) : 1;
    float per = d->damage / (float)pellets;

    if (gun) { w->ammo--; ws->shots++; } else { ws->swings++; }
    w->cool = 1.0f / fmaxf(d->rate, 0.05f);
    w->kick = 1.0f; w->flash = gun ? 1.0f : 0.0f;

    // Pellets spread on a fixed pattern rather than a random one: the host and every client have to
    // draw the same fan of tracers, and a shared seed is one more thing to get out of step.
    Vec3 right = v3_norm(v3_cross(dir, v3(0, 1, 0)));
    if (v3_len(right) < 0.1f) right = v3(1, 0, 0);
    Vec3 up = v3_cross(right, dir);
    RayHit first = { .kind = FH_NONE, .idx = -1, .point = v3_add(origin, v3_scale(dir, range)) };
    uint8_t any_hit = FH_NONE;
    for (int p = 0; p < pellets; p++) {
        Vec3 rd = dir;
        if (pellets > 1) {
            float a = (float)p * 2.399963f;                    // the golden angle: an even fan, no clumps
            float r = 0.055f * sqrtf(((float)p + 0.5f) / (float)pellets);
            rd = v3_norm(v3_add(dir, v3_add(v3_scale(right, cosf(a) * r), v3_scale(up, sinf(a) * r))));
        }
        RayHit h = weapons_ray(g, slot, origin, rd, range);
        if (p == 0) first = h;
        if (h.kind == FH_NONE) continue;
        if (any_hit == FH_NONE || h.kind == FH_PLAYER) any_hit = (uint8_t)h.kind;
        if (h.kind == FH_PLAYER) hurt_player(g, h.idx, per, rd, d->knock);
        else if (h.kind == FH_ITEM) hurt_item(g, h.idx, rd, d->knock, h.point);
    }
    if (any_hit != FH_NONE) ws->hits++; else ws->misses++;

    FireEvent e = { .slot = (uint8_t)slot, .kind = (uint8_t)(gun ? FE_SHOT : FE_SWING), .hit = any_hit,
                    .pellets = (uint8_t)pellets, .from = origin, .to = first.point };
    if (!gun) { e.from = origin; e.to = v3_add(origin, v3_scale(dir, range)); }
    weapons_event(g, &e);
    dbg_log("weapon: slot %d %s %s, %d left", slot, gun ? "fired" : "swung",
            any_hit == FH_PLAYER ? "and hit a goon" : any_hit == FH_ITEM ? "and hit loot" : any_hit == FH_WORLD ? "and hit the scenery" : "at nothing",
            w->ammo);
}

// Can this slot pull the trigger right now? The same test on both sides, so a client's prediction
// and the host's answer only disagree when the client is lying or lagging.
static bool can_fire(const Game *g, int slot, float tolerance) {
    const Weapon *w = &g->weapons.w[slot];
    const ItemDef *d = wdef(g, w->item);
    if (!d || d->weapon == 0) return false;
    if (!w->drawn || weapons_frozen(g, slot)) return false;
    if (w->cool > tolerance || w->reload > tolerance) return false;
    if (d->weapon == 2 && w->ammo <= 0) return false;
    if (d->weapon == 1 && w->swing > tolerance) return false;
    return true;
}

static void start_reload(Game *g, int slot) {
    Weapon *w = &W(g)->w[slot];
    const ItemDef *d = wdef(g, w->item);
    if (!d || d->weapon != 2 || w->reload > 0 || w->ammo >= d->ammo) return;
    w->reload = WEAP_RELOAD_TIME;
    W(g)->reloads++;
    FireEvent e = { .slot = (uint8_t)slot, .kind = FE_RELOAD, .hit = FH_NONE, .pellets = 1,
                    .from = weapons_eye(g, slot), .to = weapons_eye(g, slot) };
    weapons_event(g, &e);
}

// ---------------------------------------------------------------- host entry points

void weapons_net_fire(Game *g, int slot, Vec3 origin, Vec3 dir) {
    if (g->net.mode != NM_HOST || slot < 0 || slot >= NET_MAX_PLAYERS) return;
    if (!can_fire(g, slot, 0.03f)) { dbg_log("weapon: slot %d's shot refused (cooling, empty or unarmed)", slot); return; }
    // The client shoots from where it thinks its eye is. Accept that within a couple of metres of
    // where the host has it standing, and shoot from the host's eye height so nobody can fire from
    // the ceiling; the aim direction is theirs, because that is what they were looking at.
    Vec3 host_eye = weapons_eye(g, slot);
    if (v3_len(v3_sub(origin, host_eye)) > 2.5f) {
        dbg_log("weapon: slot %d fired from %.1f m off where the host has it; using the host's eye", slot, (double)v3_len(v3_sub(origin, host_eye)));
        origin = host_eye;
    }
    if (v3_len(dir) < 1e-3f) return;
    resolve_fire(g, slot, origin, v3_norm(dir));
}

void weapons_net_reload(Game *g, int slot) { if (g->net.mode == NM_HOST) start_reload(g, slot); }
void weapons_net_swap(Game *g, int slot)   { if (g->net.mode == NM_HOST) weapons_swap(g, slot); }

void weapons_net_revive(Game *g, int slot, int target, bool holding) {
    if (g->net.mode != NM_HOST) return;
    if (slot < 0 || slot >= NET_MAX_PLAYERS || target < 0 || target >= NET_MAX_PLAYERS) return;
    Downed *d = &W(g)->dn[target];
    if (!d->down || weapons_frozen(g, slot)) return;
    if (v3_len(v3_sub(g->players[slot].c.pos, g->players[target].c.pos)) > WEAP_REVIVE_REACH + 0.8f) return;
    if (!holding) { if (d->reviver == slot) { d->reviver = -1; d->revive = 0; } return; }
    d->reviver = slot;
}

// ---------------------------------------------------------------- events

void weapons_event(Game *g, const FireEvent *e) {
    Weapons *ws = W(g);
    if (g->net.mode == NM_HOST && ws->nev < WEAP_EVENTS) ws->ev[ws->nev++] = *e;
    weapons_event_apply(g, e);
}

int weapons_events_take(Game *g, FireEvent *out, int max) {
    Weapons *ws = W(g);
    int n = ws->nev < max ? ws->nev : max;
    for (int i = 0; i < n; i++) out[i] = ws->ev[i];
    ws->nev = 0;
    return n;
}

// ---------------------------------------------------------------- the local player's hands

static void local_input(Game *g, const Input *in) {
    Weapons *ws = W(g);
    int slot = g->local;
    Weapon *w = &ws->w[slot];
    bool client = g->net.mode == NM_CLIENT;
    ws->prompt[0] = 0;

    // Down: the only control left is E, and it is not yours.
    if (weapons_frozen(g, slot)) return;

    // Somebody else is on the floor within reach: hold E to haul them up. Held, not tapped, so it
    // cannot be done by accident while sprinting past.
    int downed = -1; float best = WEAP_REVIVE_REACH;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (i == slot || !g->net.slots[i].active || !ws->dn[i].down) continue;
        float dist = v3_len(v3_sub(g->players[slot].c.pos, g->players[i].c.pos));
        if (dist < best) { best = dist; downed = i; }
    }
    if (downed >= 0) {
        bool holding = in->key_held[SDL_SCANCODE_E] || in->interact;
        float k = ws->dn[downed].revive / WEAP_REVIVE_HOLD;
        snprintf(ws->prompt, sizeof ws->prompt, holding ? "PICKING UP %s   %d%%" : "HOLD E   PICK UP %s",
                 g->net.slots[downed].name, (int)(k * 100));
        for (int i = 0; i < (int)sizeof ws->prompt && ws->prompt[i]; i++)
            if (ws->prompt[i] >= 'a' && ws->prompt[i] <= 'z') ws->prompt[i] = (char)(ws->prompt[i] - 32);
        if (client) netgame_send_weapon_revive(g, downed, holding);
        else weapons_net_revive_local(g, slot, downed, holding);
        return;   // E is spoken for; no picking things up off a friend's chest
    }

    if (w->item < 0) return;
    const ItemDef *d = wdef(g, w->item);
    if (!d) return;

    // Q or the wheel swaps between the weapon and whatever the other hand is doing.
    if (in->key_down[SDL_SCANCODE_Q] || fabsf(in->wheel) > 0.3f) {
        weapons_swap(g, slot);
        if (client) netgame_send_weapon_swap(g);
    }
    if (!w->drawn) return;

    // R reloads. So does an empty gun's trigger, once it has clicked at you.
    if (in->key_down[SDL_SCANCODE_R] && d->weapon == 2 && w->ammo < d->ammo && w->reload <= 0) {
        start_reload(g, slot);
        if (client) netgame_send_weapon_reload(g);
    }

    bool pressed = d->weapon == 2 ? in->mouse_held : in->click;   // guns hold, bats tap
    if (!pressed) return;
    if (d->weapon == 2 && w->ammo <= 0 && w->reload <= 0 && w->cool <= 0) {
        w->cool = 0.35f;
        FireEvent e = { .slot = (uint8_t)slot, .kind = FE_CLICK, .hit = FH_NONE, .pellets = 1,
                        .from = weapons_eye(g, slot), .to = weapons_eye(g, slot) };
        weapons_event_apply(g, &e);
        if (!client) weapons_event(g, &e);
        return;
    }
    if (!can_fire(g, slot, 0)) return;

    Vec3 origin = weapons_eye(g, slot), dir = weapons_aim(g, slot);
    if (d->weapon == 1) { w->swing = WEAP_SWING_TIME; w->swing_hit = false; }
    if (client) {
        // Predict the feel of it -- kick, sound, flash, the round leaving the magazine -- and let
        // the host decide what it actually hit. A shot that the host refuses costs one round on the
        // client for a tenth of a second and then the snapshot puts it back.
        netgame_send_weapon_fire(g, origin, dir);
        if (d->weapon == 2) w->ammo--;
        w->cool = 1.0f / fmaxf(d->rate, 0.05f);
        w->kick = 1.0f; w->flash = d->weapon == 2 ? 1.0f : 0;
        FireEvent e = { .slot = (uint8_t)slot, .kind = (uint8_t)(d->weapon == 2 ? FE_SHOT : FE_SWING), .hit = FH_NONE,
                        .pellets = (uint8_t)(d->pellets < 1 ? 1 : d->pellets), .from = origin,
                        .to = v3_add(origin, v3_scale(dir, d->weapon == 2 ? clampf(d->wrange, 1, WEAP_MAX_RANGE) : WEAP_SWING_ARC)) };
        weapons_event_apply(g, &e);
    } else {
        resolve_fire(g, slot, origin, dir);
    }
}

// The host's own revive bookkeeping, called directly rather than over the wire.
void weapons_net_revive_local(Game *g, int slot, int target, bool holding) {
    if (g->net.mode == NM_CLIENT) return;
    Downed *d = &W(g)->dn[target];
    if (!d->down) return;
    if (!holding) { if (d->reviver == slot) { d->reviver = -1; d->revive = 0; } return; }
    d->reviver = slot;
}

// ---------------------------------------------------------------- the tick

void weapons_reset(Game *g) {
    Weapons *ws = W(g);
    memset(ws, 0, sizeof *ws);
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        ws->w[i].item = -1;
        ws->w[i].wind = WEAP_WIND;
        ws->dn[i].reviver = -1;
    }
}

// A melee swing lands on the first thing inside the arc, once, at the moment the bat is out in
// front. Guns are instant; only the swing has a contact frame worth waiting for.
static void melee_contact(Game *g, int slot) {
    Weapon *w = &W(g)->w[slot];
    if (w->swing <= 0 || w->swing_hit) return;
    if (w->swing > WEAP_SWING_TIME * 0.55f) return;    // still winding up
    w->swing_hit = true;
    if (g->net.mode == NM_CLIENT) return;              // the host owns what a swing hits
    const ItemDef *d = wdef(g, w->item);
    if (!d) return;
    Vec3 origin = weapons_eye(g, slot), dir = weapons_aim(g, slot);
    RayHit h = weapons_ray(g, slot, origin, dir, WEAP_SWING_ARC);
    if (h.kind == FH_PLAYER) hurt_player(g, h.idx, d->damage, dir, d->knock);
    else if (h.kind == FH_ITEM) hurt_item(g, h.idx, dir, d->knock, h.point);
}

void weapons_tick(Game *g, const Input *in, float dt) {
    Weapons *ws = W(g);
    bool host = g->net.mode != NM_CLIENT;

    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        Weapon *w = &ws->w[i];
        Downed *d = &ws->dn[i];
        // A weapon whose item went away (a level change, a break) leaves an empty hand behind.
        if (w->item >= 0 && (w->item >= g->items.n || !g->items.it[w->item].used || g->items.it[w->item].broken)) *w = (Weapon){ .item = -1, .wind = w->wind };
        if (w->cool > 0)   w->cool = fmaxf(0, w->cool - dt);
        if (w->kick > 0)   w->kick = fmaxf(0, w->kick - dt * 4.5f);
        if (w->flash > 0)  w->flash = fmaxf(0, w->flash - dt * 16.0f);
        if (w->swing > 0)  { melee_contact(g, i); w->swing = fmaxf(0, w->swing - dt); }
        if (w->reload > 0) {
            w->reload -= dt;
            if (w->reload <= 0) {
                w->reload = 0;
                const ItemDef *def = wdef(g, w->item);
                if (def && def->weapon == 2) w->ammo = def->ammo;
            }
        }
        // Wind comes back after a quiet moment, so a run of small hits stacks but a bad afternoon
        // half an hour ago does not.
        w->wind_quiet += dt;
        if (host && !d->down && w->wind_quiet > WEAP_WIND_DELAY && w->wind < WEAP_WIND)
            w->wind = fminf(WEAP_WIND, w->wind + WEAP_WIND_REGEN * dt);
        // Two hands on the loot: the weapon goes on your back and stays there.
        if (w->item >= 0 && w->drawn && items_two_handed(g, i)) w->drawn = false;

        if (d->getup > 0) { d->getup = fmaxf(0, d->getup - dt); }
        if (!d->down) continue;
        d->t += dt;
        if (d->reviver >= 0) {
            // The mate has to keep standing there. One tick of them wandering off resets it.
            bool near = g->net.slots[d->reviver].active &&
                        v3_len(v3_sub(g->players[d->reviver].c.pos, g->players[i].c.pos)) <= WEAP_REVIVE_REACH + 0.5f;
            if (near) d->revive += dt; else { d->reviver = -1; d->revive = 0; }
        }
        if (!host) continue;
        if (d->revive >= WEAP_REVIVE_HOLD) { ws->revives++; get_up(g, i, "hauled up by a mate"); }
        else if (d->t >= WEAP_DOWN_TIME) get_up(g, i, "six seconds of lying there");
    }

    // Animation: the host drives everyone's, a client only predicts its own; the rest arrive in
    // the snapshot. Nothing here is ever called death, in code or on screen.
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (!g->net.slots[i].active) continue;
        if (g->net.mode == NM_CLIENT && i != g->local) continue;
        Character *c = &g->players[i].c;
        const Downed *d = &ws->dn[i];
        const Weapon *w = &ws->w[i];
        if (d->down) { if (d->t > 0.75f && c->anim != ANIM_DOWN) character_set_anim(c, ANIM_DOWN); }
        else if (d->getup > 0) { if (c->anim != ANIM_GETUP) character_set_anim(c, ANIM_GETUP); }
        else if (w->item >= 0 && w->drawn && c->speed < 0.12f && g->players[i].state == PS_FREE) {
            const ItemDef *def = wdef(g, w->item);
            Anim want = w->reload > 0 ? ANIM_GUN_RELOAD
                      : w->swing > 0 ? ANIM_MELEE_SWING
                      : w->kick > 0.7f && def && def->weapon == 2 ? ANIM_GUN_FIRE
                      : def && def->weapon == 2 ? ANIM_GUN_IDLE : ANIM_MELEE_IDLE;
            if (c->anim != want) character_set_anim(c, want);
        }
    }

    if (g->net.slots[g->local].active) local_input(g, in);
    weapons_fx_tick(g, dt);
}

// ---------------------------------------------------------------- snapshot bytes

uint8_t weapons_pack_flags(const Game *g, int slot) {
    const Weapon *w = &g->weapons.w[slot];
    const Downed *d = &g->weapons.dn[slot];
    const ItemDef *def = wdef(g, w->item);
    unsigned kind = def ? (unsigned)def->weapon : 0u;
    return (uint8_t)((d->down ? 1u : 0u) | (d->getup > 0 ? 2u : 0u) | (w->drawn ? 4u : 0u) |
                     ((kind & 3u) << 3) | (w->reload > 0 ? 32u : 0u) | (w->swing > 0 ? 64u : 0u));
}

void weapons_apply_flags(Game *g, int slot, uint8_t flags, uint8_t ammo, uint8_t wind) {
    if (slot < 0 || slot >= NET_MAX_PLAYERS) return;
    Weapon *w = &g->weapons.w[slot];
    Downed *d = &g->weapons.dn[slot];
    bool down = (flags & 1u) != 0;
    if (down && !d->down) { d->down = true; d->t = 0; d->getup = 0; }
    else if (!down && d->down) { d->down = false; d->getup = (flags & 2u) ? 0.9f : 0; }
    w->drawn  = (flags & 4u) != 0;
    if (slot == g->local) {
        // Our own ammo and wind are the host's to say; everything else we predicted ourselves and
        // a snapshot older than the round trip must not walk it back.
        w->ammo = ammo;
        w->wind = (float)wind;
        if ((flags & 32u) && w->reload <= 0) w->reload = WEAP_RELOAD_TIME * 0.5f;
        if (!(flags & 32u)) w->reload = 0;
    } else {
        w->ammo = ammo; w->wind = (float)wind;
        w->reload = (flags & 32u) ? WEAP_RELOAD_TIME * 0.5f : 0;
        if ((flags & 64u) && w->swing <= 0) { w->swing = WEAP_SWING_TIME; w->swing_hit = true; }
    }
}
