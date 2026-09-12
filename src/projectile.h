// Projectiles: the things a gun throws that take time to arrive.
//
// A hitscan shot is a decision; a projectile is an object. It leaves the muzzle at a speed, falls
// if it has any weight, loses a little to the air, bounces off the scenery if it is the bouncing
// kind, and either sticks, vanishes or goes off. That difference is the whole point of the nail
// gun and the grenade launcher: you have to lead a moving goon, and you have to think about the
// arc and the wall behind him.
//
// Everything about one kind lives in its gun's item file (`projectile SPEED GRAVITY BOUNCE LIFE
// DAMAGE RADIUS SOUND MODEL`, see items.h), so a new projectile is a text file and no code.
//
// AUTHORITY. The host owns every projectile: it spawns them, moves them, and decides what they
// hit. A client that pulls its own trigger spawns a *predicted* copy immediately -- otherwise the
// nail leaves the barrel a round trip after the click, which feels like a broken gun -- and the
// host's own copy takes it over the moment the first snapshot carrying it arrives. A predicted
// projectile never damages anything and never explodes for real; it is a picture of one.
//
// ON THE WIRE. Live projectiles ride the ordinary entity snapshot as NET_ENT_PROJ: id, kind,
// owner, position in centimetres and velocity in decimetres per second, ten bytes each. Clients
// integrate between snapshots with the same code the host uses, so twenty nails in the air cost
// 10 B * 20 * 30 Hz = 6 kB/s at the very worst and usually far less, because most of them are gone
// inside a second. A detonation is a one-off event (FE_BOOM), not state.
#pragma once
#include "hmath.h"
#include "gfx.h"
#include <stdint.h>

struct Game;

#define PROJ_MAX      128    // in the air at once, across every player
#define PROJ_STALE     0.4f  // seconds a replicated projectile survives without a snapshot
#define PROJ_MAX_LIFE 12.0f  // nothing flies forever, whatever the item file says

typedef struct Projectile {
    bool     used;
    uint16_t id;         // host-assigned network id; 0 while a client's own copy is unconfirmed
    uint8_t  owner;      // the net slot that fired it
    uint8_t  kind;       // ItemDef.proj_kind, the one byte that names it on the wire
    int      def;        // resolved index into Items.defs, or -1 if this process has no such file
    bool     predicted;  // a client's own copy, spawned on the frame the button went down
    bool     replicated; // arrived in a snapshot rather than being fired here
    Vec3     pos, vel;
    Vec3     prev;       // last tick's position: the segment that gets collision-tested and drawn
    float    age;        // seconds since it left the muzzle
    float    life;       // seconds it has left
    int      bounces;
    float    trail;      // seconds until the next puff of the smoke trail
    double   last_snap;  // client: when a snapshot last mentioned it (see PROJ_STALE)
} Projectile;

#define PROJ_BOOMS 8         // detonations still lighting the place up, local only
#define PROJ_BOOM_LIGHT 0.35f // seconds a blast keeps its own point light

typedef struct Projectiles {
    Projectile p[PROJ_MAX]; int n;      // high-water mark; iterate [0, n) and skip !used
    uint16_t next_id;
    // The flash of a detonation, local only: it is a light and a camera shake, not state.
    struct { Vec3 at; float radius, life; } boom[PROJ_BOOMS]; int nboom;
    // counters for the log line and the closing report
    unsigned spawned, hits, booms, expired, dropped;
} Projectiles;

// ---- lifetime -------------------------------------------------------------------------------
void projectiles_reset(struct Game *g);

// Put one in the air. `def` is the FIRING item's def index (the gun), `from` the muzzle and `dir`
// a unit aim vector; spread and speed come out of the def. `predicted` marks a client's own copy.
// `id` is the network id to use, or 0 to take the next one (host) / stay unconfirmed (client).
// Returns the projectile index, or -1 when the sky is full.
int  projectile_spawn(struct Game *g, int slot, int def, Vec3 from, Vec3 dir, bool predicted, uint16_t id);

// One tick of everything in the air: integrate, sweep the segment against the world, players and
// items, bounce or stop, count the fuse down. The host resolves damage and detonation; a client
// moves and draws and nothing else. Called from weapons_tick.
void projectiles_tick(struct Game *g, float dt);

// World pass: the models (or lit streaks) and their smoke trails.
void projectiles_draw(struct Game *g);

// Point lights a live fuse or a fresh detonation contributes. Returns how many were written.
int  projectiles_lights(const struct Game *g, PointLight *out, int max);

// ---- network --------------------------------------------------------------------------------
// Client: one projectile out of a snapshot. Creates it if this is the first time it has been seen,
// otherwise corrects it -- and adopts the matching predicted copy rather than drawing two.
void projectiles_net_sample(struct Game *g, uint16_t id, uint8_t kind, uint8_t owner, Vec3 pos, Vec3 vel, double t);
// Everybody: a detonation happened here. Particles, light, sound and camera shake by distance;
// no damage, because the host has already applied that to whatever it applied to.
void projectiles_boom_fx(struct Game *g, Vec3 at, float radius);
// How many are in the air right now (the debug line and the bandwidth test ask).
int  projectiles_live(const struct Game *g);
