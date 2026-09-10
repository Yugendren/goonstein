// Rigid bodies for the things you carry, throw and drop.
//
// Not a solver. Every body is a box or a sphere sampled at a few points (a box's eight corners, a
// sphere's centre); each sample that ends up inside the world gets a positional push and an impulse
// applied at that point, which is what makes a thrown crate tumble and a dropped one settle flat.
// Bodies collide with the terrain (a height callback), with the level's solid blocks and decks, and
// with each other as bounding spheres. Everything is fixed-step and allocation-free, cheap enough
// for a hundred objects at 60 Hz, and it goes to sleep the moment it stops moving.
//
// Determinism: same inputs, same order, same result. Bodies are iterated by index, contacts are
// resolved in a fixed order, and nothing reads the wall clock.
#pragma once
#include "hmath.h"
#include "level.h"
#include <stddef.h>

#define PHYS_MAX_BODIES 160

typedef enum PhysShape { PHS_BOX = 0, PHS_SPHERE = 1 } PhysShape;

typedef struct PhysBody {
    bool      used;
    PhysShape shape;
    Vec3      half;          // box half-extents; sphere: radius in every component
    float     radius;        // bounding radius, cached from half
    float     mass, inv_mass;
    float     inv_inertia;   // scalar approximation: a box's is close enough for a crate
    float     restitution;   // 0 = dead, 0.5 = lively
    float     friction;      // 0..1 tangential impulse cap
    Vec3      pos, vel;
    Quat      rot; Vec3 avel;
    bool      sleeping; float sleep_t;
    bool      kinematic;     // never integrated, infinite mass to everyone else (a client's replicas)
    bool      grounded;      // touched anything last step
    // The hardest contact since the caller last cleared it: what a fragile item breaks on.
    float     impact; Vec3 impact_at, impact_n;
    int       user;          // the item that owns this body, or -1
} PhysBody;

// Terrain under a point: height and surface normal. May be NULL on a level with no terrain.
typedef void (*PhysGroundFn)(void *ud, float x, float z, float *h, Vec3 *n);

typedef struct PhysWorld {
    PhysBody     b[PHYS_MAX_BODIES]; int n;   // n is a high-water mark; iterate [0, n) and skip !used
    float        gravity;                     // m/s^2, positive
    const Level *lv;                          // blocks to collide with (may be NULL)
    PhysGroundFn ground; void *ground_ud;
    // stats, for the debug line and the milestone report
    unsigned awake, contacts; float last_ms;
} PhysWorld;

void phys_init(PhysWorld *w);
void phys_clear(PhysWorld *w);
// Both return a body index or -1 when the world is full. Bodies start awake.
int  phys_add_box(PhysWorld *w, Vec3 pos, Quat rot, Vec3 half, float mass);
int  phys_add_sphere(PhysWorld *w, Vec3 pos, Quat rot, float radius, float mass);
void phys_remove(PhysWorld *w, int i);
void phys_wake(PhysWorld *w, int i);
// Teleport (a client applying a snapshot, an item being placed). Clears velocity when `still`.
void phys_place(PhysWorld *w, int i, Vec3 pos, Quat rot, bool still);
// Impulse in kg m/s applied at a world point (`at`), which is what makes a throw spin.
void phys_impulse(PhysWorld *w, int i, Vec3 impulse, Vec3 at);
// Velocity-level force for the carry spring: acceleration this step, applied to the centre.
void phys_accelerate(PhysWorld *w, int i, Vec3 accel, float dt);
// Angular version: an angular acceleration about the world axes.
void phys_torque(PhysWorld *w, int i, Vec3 ang_accel, float dt);
// One fixed step. Substeps internally when something is moving fast, so a thrown item cannot
// tunnel through a wall.
void phys_step(PhysWorld *w, float dt);

static inline PhysBody *phys_body(PhysWorld *w, int i) {
    return (i >= 0 && i < w->n && w->b[i].used) ? &w->b[i] : NULL;
}
static inline const PhysBody *phys_body_c(const PhysWorld *w, int i) {
    return (i >= 0 && i < w->n && w->b[i].used) ? &w->b[i] : NULL;
}
// Quaternion helpers the item layer wants too.
Quat quat_from_axis_angle(Vec3 axis, float angle);
Quat quat_mul(Quat a, Quat b);
Vec3 quat_rotate(Quat q, Vec3 v);
