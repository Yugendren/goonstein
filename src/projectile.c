// Projectiles: the physics, the damage and the pixels for everything currently in the air. See
// projectile.h for the wire format and the authority model in one paragraph; the short version is
// that the host owns every projectile and a client's own shot is a predicted picture of one until
// the first snapshot mentioning it arrives.
//
// No blood, no fire-orange gore, nothing called death anywhere near this file: a direct hit is a
// dull thud and a couple of grey puffs, and a blast is warm dust and a flash, exactly like the rest
// of weapons.c. See DESIGN.md.
#include "game.h"
#include "audio.h"
#include "debug.h"
#include <math.h>
#include <string.h>

// ---------------------------------------------------------------- tuning

// A bullet at 420 m/s covers seven metres a tick at 60 Hz, which is more than enough to skip through a
// thin wall between two samples. 0.35 m is comfortably thinner than the level's thinnest solid, and
// 8 substeps caps the cost of a fast, heavy throw (a grenade launched hard downhill) at eight traces
// a tick rather than an unbounded number.
#define PROJ_SUBSTEP_MAX_MOVE 0.35f
#define PROJ_SUBSTEP_CAP      8

#define PROJ_TRAIL_INTERVAL   0.04f  // seconds between smoke puffs while airborne and moving
#define PROJ_TRAIL_MIN_SPEED  4.0f   // below this the trail stops; a dying throw doesn't smoke
// A flat, short-fused projectile reads as a bullet: its whole flight is a fraction of a
// second in a straight line, and a smoke trail on it would be a grey streak nobody asked for rather
// than the lazy arc a lobbed grenade actually leaves. A def this file cannot resolve (kind unknown on
// this process) is treated the same way, since there is nothing to say otherwise.
#define PROJ_TRAIL_SKIP_LIFE  2.0f

#define PROJ_BOUNCE_DAMP      0.75f  // fraction of tangential speed kept on a bounce: a grenade slides on
#define PROJ_BOUNCE_MAX       8      // bounces before it is parked to wait out its fuse
#define PROJ_REST_SPEED       1.2f   // m/s; below this on a mostly-flat surface it stops rolling
#define PROJ_BOUNCE_SOUND_GAP 0.1f   // seconds between bounce thuds, so a long slide isn't a drumroll

// Bounce-sound cooldown, kept out of Projectile itself (projectile.h is not this file's to change):
// indexed the same as Projectiles.p[], reset in projectiles_reset. Safe against exactly one Game per
// process (main.c's `static Game game`), which is all this build ever makes.
static float s_bounce_cool[PROJ_MAX];

// ---------------------------------------------------------------- small helpers

static const ItemDef *proj_def(const Game *g, const Projectile *p) {
    return (p->def >= 0 && p->def < g->items.ndefs) ? &g->items.defs[p->def] : NULL;
}

// Copied from weaponview.c's static atten() (that copy is local to this file for the same reason
// weaponview.c keeps its own limb_matrix): distance-based gain falloff, same numbers, so a
// projectile's thud and a gunshot's crack fade over the same range.
static float atten(const Game *g, Vec3 at, float gain) {
    float dist = v3_len(v3_sub(at, g->cam.eye));
    return gain * clampf(1.0f - dist / 25.0f, 0.08f, 1.0f);
}

// A world matrix built directly from three axes. Copied from weaponview.c's static basis_matrix.
static Mat4 basis_matrix(Vec3 pos, Vec3 x, Vec3 y, Vec3 z, Vec3 scale) {
    Mat4 m = m4_identity();
    m.m[0] = x.x * scale.x; m.m[1] = x.y * scale.x; m.m[2] = x.z * scale.x;
    m.m[4] = y.x * scale.y; m.m[5] = y.y * scale.y; m.m[6] = y.z * scale.y;
    m.m[8] = z.x * scale.z; m.m[9] = z.y * scale.z; m.m[10] = z.z * scale.z;
    m.m[12] = pos.x; m.m[13] = pos.y; m.m[14] = pos.z;
    return m;
}

// A unit cube stretched and turned to run from `from` to `to`. Copied from weaponview.c's static
// limb_matrix, which is where the tracer and the viewmodel arms come from -- a flying round with no
// model is drawn exactly the same way a tracer is.
static Mat4 limb_matrix(Vec3 from, Vec3 to, float thick) {
    Vec3 d = v3_sub(to, from);
    float len = v3_len(d);
    Vec3 z = len > 1e-5f ? v3_scale(d, 1.0f / len) : v3(0, 0, 1);
    Vec3 upref = fabsf(z.y) > 0.98f ? v3(1, 0, 0) : v3(0, 1, 0);
    Vec3 x = v3_norm(v3_cross(upref, z));
    Vec3 y = v3_cross(z, x);
    Vec3 mid = v3_scale(v3_add(from, to), 0.5f);
    return basis_matrix(mid, x, y, z, v3(thick, thick, fmaxf(len, 0.001f)));
}

static int alloc_slot(Projectiles *ps) {
    for (int i = 0; i < PROJ_MAX; i++)
        if (!ps->p[i].used) { if (i >= ps->n) ps->n = i + 1; return i; }
    return -1;
}

// ---------------------------------------------------------------- ending a flight

// A non-explosive stop: a round biting a wall, a goon or a crate. Runs on any machine that ticks
// this projectile -- host or client, predicted or replicated -- because it is pure cosmetic feedback
// with no gameplay weight and nothing in the wire format carries it as an event of its own.
static void quiet_impact(Game *g, Vec3 at, Vec3 normal, const ItemDef *d) {
    particles_burst(&g->particles, PT_SMOKE, at, normal, 4, 0.5f, v3(0.5f, 0.45f, 0.4f), 0.09f, 0.35f);
    // A small bright fleck for anything that just hit something solid, whatever it was: one
    // unconditional burst reads as well for a round biting a wall as for one biting a crate
    // hitting wood too, so there is no case analysis on what was hit.
    particles_burst(&g->particles, PT_SPARK, at, normal, 3, 1.8f, v3(1.0f, 0.85f, 0.5f), 0.04f, 0.18f);
    SoundId snd = (d && d->proj_sound >= 0) ? (SoundId)d->proj_sound : SND_HIT;
    audio_play(snd, atten(g, at, 0.5f), 1.0f);
}

// Host only: the real bang. Damage and knockback fall off with a squared falloff (k = (1 -
// dist/radius)^2) rather than linearly, because a linear falloff makes the rim of a blast feel as
// dangerous as its centre; squaring it keeps the outer ring a shove and the middle a knockdown,
// which is the shape an explosion is supposed to have.
static void explode_host(Game *g, Vec3 at, int owner, const ItemDef *d) {
    Projectiles *ps = &g->projectiles;
    float radius = d ? d->proj_radius : 0.0f;
    if (radius <= 0.0f) return;
    float damage = d ? d->proj_damage : 0.0f;
    float knock = d ? d->knock : 0.0f;

    int hit_players = 0, hit_items = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        if (!g->net.slots[i].active) continue;
        const Character *c = &g->players[i].c;
        Vec3 center = v3(c->pos.x, c->pos.y + c->height * 0.5f, c->pos.z);
        Vec3 delta = v3_sub(center, at);
        float dist = v3_len(delta);
        if (dist >= radius) continue;
        float k = 1.0f - dist / radius; k *= k;
        Vec3 dir = dist > 1e-4f ? v3_scale(delta, 1.0f / dist) : v3(0, 1, 0);
        // Friendly fire is always on, the owner included: standing on your own grenade is the joke.
        weapons_hurt_player(g, i, damage * k, dir, knock * k);
        hit_players++;
    }
    for (int i = 0; i < g->items.n; i++) {
        Item *it = &g->items.it[i];
        if (!it->used || it->broken || it->weapon_hand) continue;
        Vec3 delta = v3_sub(it->pos, at);
        float dist = v3_len(delta);
        if (dist >= radius) continue;
        float k = 1.0f - dist / radius; k *= k;
        Vec3 dir = dist > 1e-4f ? v3_scale(delta, 1.0f / dist) : v3(0, 1, 0);
        weapons_hurt_item(g, i, dir, knock * k, it->pos);
        hit_items++;
    }

    // One event carries this to everyone: weapons_event both queues it for the network and applies
    // it locally, and that local apply -- FE_BOOM wired into weaponview.c's weapons_event_apply, to
    // call projectiles_boom_fx -- is what shows the host its own picture of the bang. Calling
    // projectiles_boom_fx here as well would double every blast on the machine that set it off.
    FireEvent e = { .slot = (uint8_t)owner, .kind = FE_BOOM, .hit = FH_NONE,
                     .pellets = (uint8_t)clampf(radius * 10.0f, 1, 255), .from = at, .to = at };
    weapons_event(g, &e);
    ps->booms++;
    dbg_log("projectile: slot %d's %s went off at (%.1f %.1f %.1f), caught %d goon(s) and %d item(s)",
            owner, d ? d->display : "?", (double)at.x, (double)at.y, (double)at.z, hit_players, hit_items);
}

// One projectile's end, however it arrived here. Anything with a blast radius goes off (for real,
// with damage, only on the host); anything without one just stops. `normal` only matters for the
// quiet case, to aim the spark and smoke away from the surface -- pass v3(0,1,0) when there is no
// surface to speak of (a fuse running out in mid-air).
static void proj_die(Game *g, int idx, Vec3 at, Vec3 normal) {
    Projectiles *ps = &g->projectiles;
    Projectile *p = &ps->p[idx];
    const ItemDef *d = proj_def(g, p);
    if (d && d->proj_radius > 0.0f) {
        // A client never deals damage and never queues the event, so its own copy of an explosive
        // projectile (predicted or replicated) simply vanishes here without a picture: the real one,
        // in the right place, arrives a moment later as the host's FE_BOOM. Faking an earlier one
        // from a client's own guess at where and when it went off risks a visibly wrong position and
        // a second, redundant bang once the authoritative event lands -- the same "predicted, but
        // never real" rule the header states for firing applies just as well to going off.
        if (g->net.mode != NM_CLIENT) explode_host(g, at, p->owner, d);
    } else {
        quiet_impact(g, at, normal, d);
    }
    p->used = false;
}

// ---------------------------------------------------------------- lifetime

void projectiles_reset(Game *g) {
    memset(&g->projectiles, 0, sizeof g->projectiles);
    g->projectiles.next_id = 1;    // 0 is reserved for "unconfirmed"
    memset(s_bounce_cool, 0, sizeof s_bounce_cool);
}

int projectile_spawn(Game *g, int slot, int def, Vec3 from, Vec3 dir, bool predicted, uint16_t id) {
    Projectiles *ps = &g->projectiles;
    // --- boss --- WEAP_BOSS_SLOT is not a player and never will be; it is the owner a fireball
    // the boss threw carries, so that nothing which loops over the four seats ever finds it and
    // weapons_trace knows not to let it hit whatever threw it.
    if ((slot < 0 || slot >= NET_MAX_PLAYERS) && slot != WEAP_BOSS_SLOT) return -1;
    if (def < 0 || def >= g->items.ndefs) return -1;
    const ItemDef *d = &g->items.defs[def];
    if (!d->proj) return -1;

    int idx = alloc_slot(ps);
    if (idx < 0) { ps->dropped++; return -1; }

    Projectile *p = &ps->p[idx];
    memset(p, 0, sizeof *p);
    p->used = true;
    p->def = def;
    p->kind = d->proj_kind;
    p->owner = (uint8_t)slot;
    Vec3 dirn = v3_norm(dir);
    p->pos = from; p->prev = from;
    p->vel = v3_scale(dirn, d->proj_speed);
    // --- projectiles --- proj_spread is deliberately not applied here. A single call spawns one
    // projectile down one direction; a shotgun-style volley of several is the caller's job, fanned
    // the same golden-angle way resolve_fire fans hitscan pellets in weapons.c, so the host and every
    // client draw the same pattern without agreeing on a random seed. Applying a further jitter of
    // our own, keyed off the id, would just be a second uncoordinated spread on top of the caller's
    // first one -- so this function throws exactly where it is told to.
    p->life = fminf(d->proj_life, PROJ_MAX_LIFE);
    p->trail = PROJ_TRAIL_INTERVAL;

    bool host = g->net.mode != NM_CLIENT;
    if (host) {
        p->predicted = false;
        p->id = ps->next_id++;
        // --- boss --- The wire carries thirteen bits of id (the other three are the owner), so the
        // counter wraps at 8192 rather than at 65536. It has to wrap where the wire wraps or two
        // live projectiles would share an id on the clients while looking distinct here.
        if (ps->next_id >= 0x2000u) ps->next_id = 1;   // and 0 stays reserved for "unconfirmed"

    } else {
        p->predicted = predicted;
        p->id = predicted ? 0 : id;
    }
    ps->spawned++;
    dbg_log("projectile: slot %d threw a %s (id %u%s)", slot, d->display, p->id, p->predicted ? ", predicted" : "");
    return idx;
}

void projectiles_tick(Game *g, float dt) {
    Projectiles *ps = &g->projectiles;
    bool host = g->net.mode != NM_CLIENT;

    // The blast ring: local-only light-and-shake bookkeeping, ticked down and compacted here since
    // there is no separate fx-tick entry point in the header.
    int bj = 0;
    for (int i = 0; i < ps->nboom; i++) {
        ps->boom[i].life -= dt;
        if (ps->boom[i].life > 0) { if (bj != i) ps->boom[bj] = ps->boom[i]; bj++; }
    }
    ps->nboom = bj;

    for (int i = 0; i < ps->n; i++) {
        Projectile *p = &ps->p[i];
        if (!p->used) continue;
        if (s_bounce_cool[i] > 0) s_bounce_cool[i] -= dt;

        // A replicated projectile the snapshot stream has gone quiet on: the owner's connection
        // stalled, or a run of unreliable packets went missing. Only a client does this -- the host
        // has no snapshots to go stale, it is the thing snapshots are made from.
        if (!host && p->replicated && g->net.now - p->last_snap > PROJ_STALE) { p->used = false; continue; }

        const ItemDef *d = proj_def(g, p);

        // Substep so nothing crosses a wall between two samples: see PROJ_SUBSTEP_MAX_MOVE above.
        // The count is worked out from the speed at the START of the tick, which slightly
        // over-estimates once gravity has added to it over the tick and so always errs toward more
        // safety, never less.
        float speed0 = v3_len(p->vel);
        int nsub = (speed0 * dt > PROJ_SUBSTEP_MAX_MOVE) ? (int)ceilf(speed0 * dt / PROJ_SUBSTEP_MAX_MOVE) : 1;
        if (nsub < 1) nsub = 1;
        if (nsub > PROJ_SUBSTEP_CAP) nsub = PROJ_SUBSTEP_CAP;
        float h = dt / (float)nsub;

        bool died = false;
        for (int s = 0; s < nsub && !died; s++) {
            p->prev = p->pos;
            if (d) p->vel.y -= d->proj_gravity * h;
            if (d && d->proj_drag > 0.0f) p->vel = v3_scale(p->vel, expf(-d->proj_drag * h));
            p->pos = v3_add(p->pos, v3_scale(p->vel, h));

            Vec3 seg = v3_sub(p->pos, p->prev);
            float seglen = v3_len(seg);
            if (seglen < 1e-5f) continue;
            Vec3 dirn = v3_scale(seg, 1.0f / seglen);
            WeapHit wh = weapons_trace(g, p->owner, p->prev, dirn, seglen);
            if (wh.kind == FH_NONE) continue;

            if (wh.kind == FH_PLAYER || wh.kind == FH_ITEM || wh.kind == FH_BOSS) {
                if (host && !p->predicted && d) {
                    if (wh.kind == FH_PLAYER) weapons_hurt_player(g, wh.idx, d->proj_damage, dirn, d->knock);
                    else if (wh.kind == FH_BOSS) boss_hurt(g, p->owner < NET_MAX_PLAYERS ? p->owner : -1, d->proj_damage, dirn, wh.point);   // --- boss ---
                    else weapons_hurt_item(g, wh.idx, dirn, d->knock, wh.point);
                    // Tell whoever fired it that it arrived. A bullet with travel time cannot put a
                    // hit marker on the screen at the moment the trigger went down, because at that
                    // moment nothing had happened yet; this is that answer, sent late on purpose.
                    FireEvent ev = { .slot = p->owner, .kind = FE_IMPACT, .hit = (uint8_t)wh.kind,
                                     .pellets = 1, .from = wh.point, .to = wh.point };
                    weapons_event(g, &ev);
                }
                ps->hits++;
                proj_die(g, i, wh.point, wh.normal);
                died = true;
                continue;
            }

            // FH_WORLD. weapons_trace's normal is the true surface normal off the terrain but only
            // the reverse of the incoming ray off a level block (level_ray_solid does not return
            // one) -- the same approximation the hitscan lives with, so a projectile bouncing off a block
            // wall mirrors through the ray's own axis rather than the wall's. Good enough for a
            // bouncing projectile, and the two guns that exist today do not bounce at all.
            if (d && d->proj_bounce > 0.0f) {
                Vec3 n = wh.normal;
                float vn = v3_dot(p->vel, n);
                Vec3 vel_t = v3_sub(p->vel, v3_scale(n, vn));
                p->pos = v3_add(wh.point, v3_scale(n, 0.04f));
                p->vel = v3_sub(v3_scale(vel_t, PROJ_BOUNCE_DAMP), v3_scale(n, vn * (1.0f + d->proj_bounce)));
                p->bounces++;
                if (s_bounce_cool[i] <= 0.0f) {
                    SoundId snd = d->proj_sound >= 0 ? (SoundId)d->proj_sound : SND_THUD;
                    audio_play(snd, atten(g, p->pos, 0.35f), 1.15f);
                    s_bounce_cool[i] = PROJ_BOUNCE_SOUND_GAP;
                }
                bool mostly_flat = n.y > 0.7f;
                if (p->bounces > PROJ_BOUNCE_MAX || (v3_len(p->vel) < PROJ_REST_SPEED && mostly_flat))
                    p->vel = v3(0, 0, 0);   // parked; the fuse decides what happens next
                continue;
            }

            proj_die(g, i, wh.point, wh.normal);
            died = true;
        }
        if (died) continue;

        p->age += dt;
        p->life -= dt;
        if (p->life <= 0.0f) {
            ps->expired++;
            if (d && d->proj_radius > 0.0f) {
                proj_die(g, i, p->pos, v3(0, 1, 0));
            } else {
                // A round that never hit anything just stops existing: no puff, no sound. Its whole
                // flight was a fraction of a second nobody was meant to notice, and conjuring a burst
                // of smoke out of thin air where it happened to run out of road would read as a bug,
                // not an ending -- the quiet option, chosen on purpose.
                p->used = false;
            }
            continue;
        }

        bool trail_ok = d && d->proj_gravity > 0.0f && d->proj_life > PROJ_TRAIL_SKIP_LIFE;
        if (trail_ok && v3_len(p->vel) > PROJ_TRAIL_MIN_SPEED) {
            p->trail -= dt;
            if (p->trail <= 0.0f) {
                particles_burst(&g->particles, PT_SMOKE, p->pos, v3(0, 0, 0), 1, 0.15f, v3(0.55f, 0.53f, 0.5f), 0.06f, 0.35f);
                p->trail = PROJ_TRAIL_INTERVAL;
            }
        }
    }
}

// ---------------------------------------------------------------- drawing

void projectiles_draw(Game *g) {
    // A flying round casting a sun shadow is the same bug weaponview.c's viewmodel avoids with this
    // same early return: a shadow is a silhouette baked once for the whole scene, not a per-frame toy.
    if (g->gfx.in_shadow) return;
    Gfx *x = &g->gfx;
    const Projectiles *ps = &g->projectiles;

    for (int i = 0; i < ps->n; i++) {
        const Projectile *p = &ps->p[i];
        if (!p->used) continue;
        const ItemDef *d = proj_def(g, p);
        Vec3 seg = v3_sub(p->pos, p->prev);
        float seglen = v3_len(seg);

        if (d && d->proj_model[0]) {
            // Business end down +Z, the same convention grip_matrix uses for a held weapon, oriented
            // by the projectile's own velocity (falling back to last tick's segment, then to +Z, for
            // the rare frame where it is sitting dead still).
            Vec3 fwd = v3_len(p->vel) > 1e-4f ? v3_norm(p->vel) : (seglen > 1e-5f ? v3_scale(seg, 1.0f / seglen) : v3(0, 0, 1));
            Vec3 upref = fabsf(fwd.y) > 0.98f ? v3(1, 0, 0) : v3(0, 1, 0);
            Vec3 right = v3_norm(v3_cross(upref, fwd));
            Vec3 up = v3_cross(fwd, right);
            Mat4 m = basis_matrix(p->pos, right, up, fwd, v3(d->proj_scale, d->proj_scale, d->proj_scale));
            if (d->proj_gravity > 0.0f) m = m4_mul(m, m4_rotate_z(p->age * 6.0f));   // a grenade tumbles, ~1 rev/s
            props_draw_matrix(&g->gfx, &g->props, d->proj_model, m, d->tint, v3(0, 0, 0), NULL, 0, 0);
            // The fuse spark: only for something that both falls and can go off, so a heavy but inert
            // thrown object (should one ever exist) doesn't carry a spark it has no fuse to justify.
            if (d->proj_gravity > 0.0f && d->proj_radius > 0.0f)
                gfx_billboard(x, p->pos, 0.05f, v4(1.0f, 0.8f, 0.4f, 0.9f), true);
        } else {
            Mat4 m = limb_matrix(p->prev, p->pos, 0.03f);
            Material mat = material_default();
            mat.unlit = 1.0f;
            mat.emissive = v3(0.9f, 0.5f, 0.2f);
            gfx_set_material(x, &mat);
            gfx_draw(x, &x->cube, &x->white, m, v4(1, 1, 1, 1), v4(1, 1, 0, 0));
            gfx_set_material(x, NULL);
        }
    }
}

int projectiles_lights(const Game *g, PointLight *out, int max) {
    const Projectiles *ps = &g->projectiles;
    int n = 0;
    for (int i = 0; i < ps->nboom && n < max; i++) {
        if (ps->boom[i].life <= 0) continue;
        out[n].pos = ps->boom[i].at;
        out[n].radius = ps->boom[i].radius * 2.0f;
        out[n].color = v3(1.0f, 0.82f, 0.55f);
        out[n].intensity = (ps->boom[i].life / PROJ_BOOM_LIGHT) * 6.0f;
        n++;
    }
    // A lit fuse: only a projectile that both falls and can go off gets one, the same test the fuse
    // spark in projectiles_draw uses, so a light never appears on something with no spark to match it.
    for (int i = 0; i < ps->n && n < max; i++) {
        const Projectile *p = &ps->p[i];
        if (!p->used) continue;
        const ItemDef *d = proj_def(g, p);
        if (!d || d->proj_gravity <= 0.0f || d->proj_radius <= 0.0f) continue;
        out[n].pos = p->pos;
        out[n].radius = 2.5f;
        out[n].color = v3(1.0f, 0.7f, 0.35f);
        out[n].intensity = 1.0f;
        n++;
    }
    return n;
}

// ---------------------------------------------------------------- network

void projectiles_net_sample(Game *g, uint16_t id, uint8_t kind, uint8_t owner, Vec3 pos, Vec3 vel, double t) {
    Projectiles *ps = &g->projectiles;

    if (id != 0) {
        for (int i = 0; i < ps->n; i++) {
            Projectile *p = &ps->p[i];
            if (!p->used || p->id != id) continue;
            p->pos = pos; p->vel = vel; p->last_snap = t;
            p->replicated = true;
            // prev is left alone: projectiles_draw wants the segment the LAST tick actually swept,
            // not a fresh zero-length one starting at this snapshot's point.
            return;
        }
    }

    // Reconciliation: an unconfirmed predicted copy of ours, close enough to be the one this
    // snapshot is describing, is adopted rather than drawn twice next to it.
    int best = -1; float best_d = 3.0f;
    for (int i = 0; i < ps->n; i++) {
        Projectile *p = &ps->p[i];
        if (!p->used || !p->predicted || p->id != 0 || p->owner != owner || p->kind != kind) continue;
        float d = v3_len(v3_sub(p->pos, pos));
        if (d < best_d) { best_d = d; best = i; }
    }
    if (best >= 0) {
        Projectile *p = &ps->p[best];
        p->id = id; p->predicted = false; p->replicated = true;
        p->pos = pos; p->vel = vel; p->last_snap = t;
        return;
    }

    // Never seen before: someone else's shot, or ours after our own predicted copy already died
    // locally before the snapshot caught up with it. def may resolve to -1 if this process has never
    // loaded that item file; it still flies and draws as a plain streak, per the header's own promise.
    int idx = alloc_slot(ps);
    if (idx < 0) { ps->dropped++; return; }
    Projectile *p = &ps->p[idx];
    memset(p, 0, sizeof *p);
    p->used = true; p->id = id; p->kind = kind; p->owner = owner;
    p->def = itemdef_by_kind(&g->items, kind);
    p->pos = pos; p->prev = pos; p->vel = vel;
    p->replicated = true;
    p->last_snap = t;
    const ItemDef *d = proj_def(g, p);
    p->life = d ? fminf(d->proj_life, PROJ_MAX_LIFE) : PROJ_MAX_LIFE;
    p->trail = PROJ_TRAIL_INTERVAL;
}

void projectiles_boom_fx(Game *g, Vec3 at, float radius) {
    Projectiles *ps = &g->projectiles;
    float r = fmaxf(radius, 0.5f);

    // Dust and a warm flash, scaled by the blast; no red, no fire-orange gore, per DESIGN.md.
    particles_burst(&g->particles, PT_SMOKE, at, v3(0, 1, 0), (int)clampf(10.0f + r * 6.0f, 10, 40),
                     r * 1.8f, v3(0.5f, 0.46f, 0.42f), 0.35f * r, 1.1f + r * 0.15f);
    particles_burst(&g->particles, PT_SPARK, at, v3(0, 1, 0), 10, r * 3.0f, v3(1.0f, 0.85f, 0.5f), 0.05f, 0.3f);
    particles_burst(&g->particles, PT_EMBER, at, v3(0, 1, 0), 2, r * 1.2f, v3(1.0f, 0.7f, 0.35f), 0.08f, 0.6f);

    // SND_BOOM is the pump shotgun's own sound (SND_SHOT, an octave lower, twice as long, with a
    // rattling tail) -- already the shape of an explosion rather than a gunshot. Pitching it down
    // further from a shotgun's 1.0 is what tells the two apart.
    audio_play(SND_BOOM, atten(g, at, 1.0f), 0.75f);

    // Shake falls off from the CAMERA's eye, not from the blast centre to itself: a grenade on the
    // far side of the level should not visibly shake a view that cannot even see it.
    float dist = v3_len(v3_sub(at, g->cam.eye));
    float k = clampf(1.0f - dist / (r * 4.0f), 0.0f, 1.0f);
    if (k > 0.0f) camera_add_shake(&g->cam, 0.9f * k);

    int idx;
    if (ps->nboom < PROJ_BOOMS) idx = ps->nboom++;
    else {
        // Full: replace the oldest light rather than drop the newest -- the freshest blast is the
        // one still worth a light on screen a moment from now.
        idx = 0; float life = ps->boom[0].life;
        for (int i = 1; i < PROJ_BOOMS; i++) if (ps->boom[i].life < life) { life = ps->boom[i].life; idx = i; }
    }
    ps->boom[idx].at = at; ps->boom[idx].radius = r; ps->boom[idx].life = PROJ_BOOM_LIGHT;
}

int projectiles_live(const Game *g) {
    int n = 0;
    for (int i = 0; i < g->projectiles.n; i++) if (g->projectiles.p[i].used) n++;
    return n;
}
