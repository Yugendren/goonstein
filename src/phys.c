// Rigid bodies. See phys.h for the shape of the thing; this file is the whole solver.
//
// The method is deliberately old-fashioned: sample each body at a few points (a box's eight
// corners, a sphere's centre), push every sample that has ended up inside the world back out, and
// apply an impulse at that point with the standard rigid-body formula. Because the impulse is
// applied at the corner and not the centre, a crate that lands on one edge rolls onto its face,
// and a thrown vase spins. There is no constraint solver, no islands and no warm starting, which
// costs some stacking accuracy and buys a solver that cannot explode and that fits in one file.
//
// Everything is in world space, iterated in index order, with no allocation and no clock reads, so
// the same inputs give the same result on the host and on a client predicting one body.
#include "phys.h"
#include <string.h>
#include <SDL3/SDL.h>
#include <stdlib.h>

// ---------------------------------------------------------------- quaternion helpers
Quat quat_mul(Quat a, Quat b) {   // a * b: rotate by b first, then a
    return (Quat){
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}
Quat quat_from_axis_angle(Vec3 axis, float angle) {
    float l = v3_len(axis);
    if (l < 1e-8f) return quat_identity();
    float s = sinf(angle * 0.5f) / l;
    return (Quat){axis.x * s, axis.y * s, axis.z * s, cosf(angle * 0.5f)};
}
Vec3 quat_rotate(Quat q, Vec3 v) {
    Vec3 u = v3(q.x, q.y, q.z);
    Vec3 t = v3_scale(v3_cross(u, v), 2.0f);
    return v3_add(v3_add(v, v3_scale(t, q.w)), v3_cross(u, t));
}
// Spin a quaternion by an angular velocity for dt: q += 0.5 * (w, 0) * q, renormalised.
static Quat quat_integrate(Quat q, Vec3 w, float dt) {
    Quat wq = {w.x, w.y, w.z, 0};
    Quat d = quat_mul(wq, q);
    return quat_norm((Quat){q.x + d.x * 0.5f * dt, q.y + d.y * 0.5f * dt,
                            q.z + d.z * 0.5f * dt, q.w + d.w * 0.5f * dt});
}

// ---------------------------------------------------------------- tuning
#define PH_LINEAR_DAMP   0.12f   // per second: air, and a little help settling
#define PH_ANGULAR_DAMP  0.90f
#define PH_SLEEP_VEL     0.14f   // m/s below which a resting body counts as still
#define PH_SLEEP_AVEL    0.60f   // rad/s ditto
#define PH_SLEEP_TIME    0.55f   // seconds of stillness before it sleeps
#define PH_CORRECT       0.75f   // fraction of the penetration removed per step
#define PH_SLOP          0.004f  // penetration allowed before we bother pushing
#define PH_MAX_VEL       60.0f
#define PH_MAX_AVEL      24.0f
#define PH_REST_CUTOFF   1.2f    // below this closing speed a contact does not bounce at all
#define PH_MAX_CANDS     64      // blocks kept per body by the broad phase (a busy deck is crowded)

void phys_init(PhysWorld *w) {
    memset(w, 0, sizeof *w);
    // Characters fall at 18 m/s^2 because that is how a game falls; loot falls at 12, because a
    // fragile item's threshold is written in metres per second and at 18 everything set down from
    // chest height arrives at 7.7 m/s and shatters. 12 keeps the weight without the massacre.
    w->gravity = 12.0f;
}
void phys_clear(PhysWorld *w) {
    const Level *lv = w->lv; PhysGroundFn gf = w->ground; void *ud = w->ground_ud; float g = w->gravity;
    memset(w, 0, sizeof *w);
    w->lv = lv; w->ground = gf; w->ground_ud = ud; w->gravity = g;
}

static int alloc_body(PhysWorld *w) {
    for (int i = 0; i < w->n; i++) if (!w->b[i].used) return i;
    if (w->n >= PHYS_MAX_BODIES) return -1;
    return w->n++;
}
static void finish_body(PhysBody *b, float mass) {
    b->used = true;
    b->mass = fmaxf(mass, 0.01f);
    b->inv_mass = 1.0f / b->mass;
    // Scalar inertia: a solid box about its centre, averaged over the three axes. Exact enough for
    // something the size of a vase and it keeps the impulse maths to one divide.
    float hx = b->half.x, hy = b->half.y, hz = b->half.z;
    float I = b->mass * (hx * hx + hy * hy + hz * hz) * (2.0f / 3.0f);
    b->inv_inertia = I > 1e-6f ? 1.0f / I : 0.0f;
    b->radius = v3_len(b->half);
    b->restitution = 0.08f;   // loot thuds, it does not bounce: keeps a delivery in the boat
    b->friction = 0.72f;
    b->sleeping = false; b->sleep_t = 0;
    b->user = -1;
}
int phys_add_box(PhysWorld *w, Vec3 pos, Quat rot, Vec3 half, float mass) {
    int i = alloc_body(w); if (i < 0) return -1;
    PhysBody *b = &w->b[i]; memset(b, 0, sizeof *b);
    b->shape = PHS_BOX; b->half = v3(fmaxf(half.x, 0.02f), fmaxf(half.y, 0.02f), fmaxf(half.z, 0.02f));
    b->pos = pos; b->rot = quat_norm(rot);
    finish_body(b, mass);
    return i;
}
int phys_add_sphere(PhysWorld *w, Vec3 pos, Quat rot, float radius, float mass) {
    int i = alloc_body(w); if (i < 0) return -1;
    PhysBody *b = &w->b[i]; memset(b, 0, sizeof *b);
    float r = fmaxf(radius, 0.02f);
    b->shape = PHS_SPHERE; b->half = v3(r, r, r);
    b->pos = pos; b->rot = quat_norm(rot);
    finish_body(b, mass);
    b->radius = r;                      // a sphere's bound is its radius, not the box diagonal
    float I = b->mass * r * r * 0.4f;   // solid sphere
    b->inv_inertia = I > 1e-6f ? 1.0f / I : 0.0f;
    return i;
}
void phys_remove(PhysWorld *w, int i) {
    PhysBody *b = phys_body(w, i); if (!b) return;
    memset(b, 0, sizeof *b);
    while (w->n > 0 && !w->b[w->n - 1].used) w->n--;
}
void phys_wake(PhysWorld *w, int i) {
    PhysBody *b = phys_body(w, i); if (!b) return;
    b->sleeping = false; b->sleep_t = 0;
}
void phys_place(PhysWorld *w, int i, Vec3 pos, Quat rot, bool still) {
    PhysBody *b = phys_body(w, i); if (!b) return;
    b->pos = pos; b->rot = quat_norm(rot);
    if (still) { b->vel = v3(0, 0, 0); b->avel = v3(0, 0, 0); }
}
void phys_impulse(PhysWorld *w, int i, Vec3 impulse, Vec3 at) {
    PhysBody *b = phys_body(w, i); if (!b || b->kinematic) return;
    b->sleeping = false; b->sleep_t = 0;
    b->vel = v3_add(b->vel, v3_scale(impulse, b->inv_mass));
    Vec3 r = v3_sub(at, b->pos);
    b->avel = v3_add(b->avel, v3_scale(v3_cross(r, impulse), b->inv_inertia));
}
void phys_accelerate(PhysWorld *w, int i, Vec3 accel, float dt) {
    PhysBody *b = phys_body(w, i); if (!b || b->kinematic) return;
    b->sleeping = false; b->sleep_t = 0;
    b->vel = v3_add(b->vel, v3_scale(accel, dt));
}
void phys_torque(PhysWorld *w, int i, Vec3 ang_accel, float dt) {
    PhysBody *b = phys_body(w, i); if (!b || b->kinematic) return;
    b->sleeping = false; b->sleep_t = 0;
    b->avel = v3_add(b->avel, v3_scale(ang_accel, dt));
}

// ---------------------------------------------------------------- contacts
// One resolved contact: a world point, the outward normal and how deep it is.
typedef struct Contact { Vec3 p, n; float d; } Contact;

// Deepest overlap of a point with an axis-aligned box, resolved along the shallowest axis, which
// is what stops a corner that has slipped a millimetre inside a wall from being flung upward.
static bool point_in_block(const Block *bl, Vec3 p, Vec3 *n, float *d) {
    Vec3 h = v3_scale(bl->size, 0.5f);
    Vec3 q = v3_sub(p, bl->center);
    float ox = h.x - fabsf(q.x), oy = h.y - fabsf(q.y), oz = h.z - fabsf(q.z);
    if (ox <= 0 || oy <= 0 || oz <= 0) return false;
    if (oy <= ox && oy <= oz)      { *n = v3(0, q.y >= 0 ? 1.0f : -1.0f, 0); *d = oy; }
    else if (ox <= oz)             { *n = v3(q.x >= 0 ? 1.0f : -1.0f, 0, 0); *d = ox; }
    else                           { *n = v3(0, 0, q.z >= 0 ? 1.0f : -1.0f); *d = oz; }
    return true;
}
// Closest point on a block to `p`, for the sphere case.
static Vec3 block_closest(const Block *bl, Vec3 p) {
    Vec3 h = v3_scale(bl->size, 0.5f);
    return v3(clampf(p.x, bl->center.x - h.x, bl->center.x + h.x),
              clampf(p.y, bl->center.y - h.y, bl->center.y + h.y),
              clampf(p.z, bl->center.z - h.z, bl->center.z + h.z));
}

// Apply one contact to one body: push it out of the world and take the energy out of the point
// that hit. Returns the closing speed, so the caller can call it an impact.
static float resolve_contact(PhysBody *b, const Contact *c, Vec3 *pos_fix, int *nfix) {
    Vec3 r = v3_sub(c->p, b->pos);
    Vec3 vp = v3_add(b->vel, v3_cross(b->avel, r));
    float vn = v3_dot(vp, c->n);
    // What the caller is told about the impact is the centre of mass closing on the surface, not
    // the contact point: a tumbling vase's corner can be doing 8 m/s while the vase itself drifts,
    // and an item file's `fragile` figure is meant to read as "how fast did it hit the wall".
    float com_close = -v3_dot(b->vel, c->n);
    if (c->d > PH_SLOP) { *pos_fix = v3_add(*pos_fix, v3_scale(c->n, (c->d - PH_SLOP) * PH_CORRECT)); (*nfix)++; }
    if (vn >= 0) return 0;   // already separating: the push alone is enough
    Vec3 rn = v3_cross(r, c->n);
    float denom = b->inv_mass + b->inv_inertia * v3_dot(rn, rn);
    if (denom < 1e-8f) return 0;
    // A resting body must not tick upward forever, so slow contacts are perfectly inelastic.
    float e = (-vn > PH_REST_CUTOFF) ? b->restitution : 0.0f;
    float j = -(1.0f + e) * vn / denom;
    Vec3 imp = v3_scale(c->n, j);
    b->vel = v3_add(b->vel, v3_scale(imp, b->inv_mass));
    b->avel = v3_add(b->avel, v3_scale(v3_cross(r, imp), b->inv_inertia));
    // Coulomb friction along the tangent, capped by the normal impulse. This is what makes a
    // dropped crate stop sliding instead of skating off down the hill.
    Vec3 vt = v3_sub(vp, v3_scale(c->n, vn));
    float lt = v3_len(vt);
    if (lt > 1e-4f) {
        Vec3 t = v3_scale(vt, 1.0f / lt);
        Vec3 rt = v3_cross(r, t);
        float dt_denom = b->inv_mass + b->inv_inertia * v3_dot(rt, rt);
        if (dt_denom > 1e-8f) {
            float jt = -lt / dt_denom;
            float cap = b->friction * j;
            jt = clampf(jt, -cap, cap);
            Vec3 fi = v3_scale(t, jt);
            b->vel = v3_add(b->vel, v3_scale(fi, b->inv_mass));
            b->avel = v3_add(b->avel, v3_scale(v3_cross(r, fi), b->inv_inertia));
        }
    }
    return com_close > 0 ? com_close : 0.0f;
}

static void note_impact(PhysBody *b, float speed, Vec3 at, Vec3 n) {
    if (speed > b->impact) { b->impact = speed; b->impact_at = at; b->impact_n = n; }
}

// HOLLOW_PHYS_TRACE=USER: dump every contact of the body owned by that user index, once a tick.
// Diagnosing "why is this thing hovering" from positions alone is guesswork; this makes it a fact.
static int phys_trace_user(void) {
    static int cached = -2;
    if (cached == -2) { const char *e = SDL_getenv("HOLLOW_PHYS_TRACE"); cached = e ? atoi(e) : -1; }
    return cached;
}

// Collide one body against the terrain and its candidate blocks.
static void collide_static(PhysWorld *w, PhysBody *b, const int *cand, int ncand) {
    bool trace = b->user >= 0 && b->user == phys_trace_user();
    if (trace) SDL_Log("phys %d: pos %.2f %.2f %.2f vel %.2f %.2f %.2f cands %d", b->user,
                       (double)b->pos.x, (double)b->pos.y, (double)b->pos.z,
                       (double)b->vel.x, (double)b->vel.y, (double)b->vel.z, ncand);
    Vec3 fix = v3(0, 0, 0); int nfix = 0;
    Vec3 pts[8]; int npts = 0;
    if (b->shape == PHS_BOX) {
        for (int c = 0; c < 8; c++) {
            Vec3 local = v3((c & 1) ? b->half.x : -b->half.x, (c & 2) ? b->half.y : -b->half.y, (c & 4) ? b->half.z : -b->half.z);
            pts[npts++] = v3_add(b->pos, quat_rotate(b->rot, local));
        }
    } else pts[npts++] = b->pos;
    float sphere_r = b->shape == PHS_SPHERE ? b->half.x : 0.0f;

    for (int k = 0; k < npts; k++) {
        Vec3 p = pts[k];
        if (w->ground) {
            float h = 0; Vec3 n = v3(0, 1, 0);
            w->ground(w->ground_ud, p.x, p.z, &h, &n);
            float depth = h - (p.y - sphere_r);
            if (depth > 0) {
                Contact c = { v3(p.x, p.y - sphere_r, p.z), v3_norm(n), depth };
                float s = resolve_contact(b, &c, &fix, &nfix);
                if (s > 0) note_impact(b, s, c.p, c.n);
                w->contacts++;
            }
        }
        for (int ci = 0; ci < ncand; ci++) {
            const Block *bl = &w->lv->blocks[cand[ci]];
            Contact c;
            if (b->shape == PHS_SPHERE) {
                Vec3 q = block_closest(bl, p);
                Vec3 diff = v3_sub(p, q);
                float dist = v3_len(diff);
                if (dist > 1e-5f) {
                    if (dist >= sphere_r) continue;
                    c.n = v3_scale(diff, 1.0f / dist); c.d = sphere_r - dist; c.p = q;
                } else {   // centre inside the block: push out along the shallowest face
                    Vec3 n; float d;
                    if (!point_in_block(bl, p, &n, &d)) continue;
                    c.n = n; c.d = d + sphere_r; c.p = p;
                }
            } else {
                Vec3 n; float d;
                if (!point_in_block(bl, p, &n, &d)) continue;
                c.n = n; c.d = d; c.p = p;
            }
            float s = resolve_contact(b, &c, &fix, &nfix);
            if (trace) SDL_Log("phys %d:   block %d centre %.2f %.2f %.2f size %.2f %.2f %.2f n %.2f %.2f %.2f d %.3f",
                               b->user, cand[ci], (double)bl->center.x, (double)bl->center.y, (double)bl->center.z,
                               (double)bl->size.x, (double)bl->size.y, (double)bl->size.z,
                               (double)c.n.x, (double)c.n.y, (double)c.n.z, (double)c.d);
            if (s > 0) note_impact(b, s, c.p, c.n);
            b->grounded = true;
            w->contacts++;
        }
    }
    if (nfix > 0) b->pos = v3_add(b->pos, v3_scale(fix, 1.0f / (float)nfix));
    if (nfix > 0) b->grounded = true;
}

// Bodies against each other, as bounding spheres. Two crates never interlock properly this way,
// but they do stack, shove and knock each other over, which is the whole comic requirement.
static void collide_pair(PhysWorld *w, PhysBody *a, PhysBody *b) {
    Vec3 diff = v3_sub(a->pos, b->pos);
    float dist = v3_len(diff);
    float sum = a->radius + b->radius;
    if (dist >= sum || dist < 1e-5f) return;
    Vec3 n = v3_scale(diff, 1.0f / dist);
    float depth = sum - dist;
    Vec3 cp = v3_add(b->pos, v3_scale(n, b->radius - depth * 0.5f));
    float ia = a->kinematic ? 0.0f : a->inv_mass, ib = b->kinematic ? 0.0f : b->inv_mass;
    float isum = ia + ib;
    if (isum < 1e-8f) return;
    if (depth > PH_SLOP) {
        float push = (depth - PH_SLOP) * PH_CORRECT;
        a->pos = v3_add(a->pos, v3_scale(n, push * (ia / isum)));
        b->pos = v3_sub(b->pos, v3_scale(n, push * (ib / isum)));
    }
    Vec3 ra = v3_sub(cp, a->pos), rb = v3_sub(cp, b->pos);
    Vec3 va = v3_add(a->vel, v3_cross(a->avel, ra)), vb = v3_add(b->vel, v3_cross(b->avel, rb));
    float vn = v3_dot(v3_sub(va, vb), n);
    if (vn >= 0) return;
    Vec3 rna = v3_cross(ra, n), rnb = v3_cross(rb, n);
    float denom = ia + ib + (a->kinematic ? 0 : a->inv_inertia * v3_dot(rna, rna))
                          + (b->kinematic ? 0 : b->inv_inertia * v3_dot(rnb, rnb));
    if (denom < 1e-8f) return;
    float e = (-vn > PH_REST_CUTOFF) ? fminf(a->restitution, b->restitution) : 0.0f;
    float j = -(1.0f + e) * vn / denom;
    Vec3 imp = v3_scale(n, j);
    if (!a->kinematic) {
        a->vel = v3_add(a->vel, v3_scale(imp, a->inv_mass));
        a->avel = v3_add(a->avel, v3_scale(v3_cross(ra, imp), a->inv_inertia));
    }
    if (!b->kinematic) {
        b->vel = v3_sub(b->vel, v3_scale(imp, b->inv_mass));
        b->avel = v3_sub(b->avel, v3_scale(v3_cross(rb, imp), b->inv_inertia));
    }
    float close = v3_dot(v3_sub(a->vel, b->vel), n);   // centre-of-mass closing speed, as above
    if (close < 0) { note_impact(a, -close, cp, n); note_impact(b, -close, cp, v3_scale(n, -1.0f)); }
    w->contacts++;
}

// ---------------------------------------------------------------- the step
void phys_step(PhysWorld *w, float dt) {
    Uint64 t0 = SDL_GetPerformanceCounter();
    w->awake = 0; w->contacts = 0;
    if (dt <= 0) { w->last_ms = 0; return; }

    // Broad phase, once per step rather than per substep: every block whose box is within the
    // sphere this body could possibly reach before the step is over.
    static int cands[PHYS_MAX_BODIES][PH_MAX_CANDS];
    static int ncands[PHYS_MAX_BODIES];
    float fastest = 0;
    for (int i = 0; i < w->n; i++) {
        PhysBody *b = &w->b[i];
        ncands[i] = 0;
        if (!b->used || b->sleeping || b->kinematic) continue;
        w->awake++;
        float speed = v3_len(b->vel) + v3_len(b->avel) * b->radius;
        if (speed > fastest) fastest = speed;
        if (!w->lv) continue;
        float reach = b->radius + speed * dt + 0.25f;
        for (int k = 0; k < w->lv->nblocks && ncands[i] < PH_MAX_CANDS; k++) {
            const Block *bl = &w->lv->blocks[k];
            if (!bl->solid && !bl->platform) continue;
            Vec3 h = v3_scale(bl->size, 0.5f);
            float dx = fmaxf(fabsf(b->pos.x - bl->center.x) - h.x, 0.0f);
            float dy = fmaxf(fabsf(b->pos.y - bl->center.y) - h.y, 0.0f);
            float dz = fmaxf(fabsf(b->pos.z - bl->center.z) - h.z, 0.0f);
            if (dx * dx + dy * dy + dz * dz <= reach * reach) cands[i][ncands[i]++] = k;
        }
    }
    if (w->awake == 0) { w->last_ms = 0; return; }

    // Substep only as much as the fastest body needs: a resting pile costs one pass, a thrown
    // crate costs six, and nothing tunnels through a wall in between.
    int steps = (int)ceilf(fastest * dt / 0.10f);
    if (steps < 1) steps = 1;
    if (steps > 6) steps = 6;
    float h = dt / (float)steps;

    for (int s = 0; s < steps; s++) {
        for (int i = 0; i < w->n; i++) {
            PhysBody *b = &w->b[i];
            if (!b->used || b->sleeping || b->kinematic) continue;
            b->vel.y -= w->gravity * h;
            b->vel = v3_scale(b->vel, expf(-PH_LINEAR_DAMP * h));
            b->avel = v3_scale(b->avel, expf(-PH_ANGULAR_DAMP * h));
            float sv = v3_len(b->vel); if (sv > PH_MAX_VEL) b->vel = v3_scale(b->vel, PH_MAX_VEL / sv);
            float sa = v3_len(b->avel); if (sa > PH_MAX_AVEL) b->avel = v3_scale(b->avel, PH_MAX_AVEL / sa);
            b->pos = v3_add(b->pos, v3_scale(b->vel, h));
            b->rot = quat_integrate(b->rot, b->avel, h);
            b->grounded = false;
        }
        for (int i = 0; i < w->n; i++) {
            PhysBody *b = &w->b[i];
            if (!b->used || b->sleeping || b->kinematic) continue;
            collide_static(w, b, cands[i], ncands[i]);
        }
        for (int i = 0; i < w->n; i++) {
            PhysBody *a = &w->b[i];
            if (!a->used || a->kinematic) continue;
            for (int j = i + 1; j < w->n; j++) {
                PhysBody *b = &w->b[j];
                if (!b->used) continue;
                if (a->sleeping && b->sleeping) continue;
                if (a->kinematic && b->kinematic) continue;
                // Something awake has run into something asleep: wake it before resolving, or the
                // thrown crate would bounce off a statue that never notices.
                Vec3 d = v3_sub(a->pos, b->pos);
                if (v3_dot(d, d) >= (a->radius + b->radius) * (a->radius + b->radius)) continue;
                if (a->sleeping) { a->sleeping = false; a->sleep_t = 0; }
                if (b->sleeping) { b->sleeping = false; b->sleep_t = 0; }
                collide_pair(w, a, b);
            }
        }
    }

    // Sleeping: a body that has been slow and in contact with something for half a second stops
    // costing anything at all. Anything in the air keeps its timer at zero, so a slow arc through
    // the sky is never mistaken for a rest.
    for (int i = 0; i < w->n; i++) {
        PhysBody *b = &w->b[i];
        if (!b->used || b->kinematic || b->sleeping) continue;
        bool still = v3_len(b->vel) < PH_SLEEP_VEL && v3_len(b->avel) < PH_SLEEP_AVEL && b->grounded;
        b->sleep_t = still ? b->sleep_t + dt : 0.0f;
        if (b->sleep_t >= PH_SLEEP_TIME) { b->sleeping = true; b->vel = v3(0, 0, 0); b->avel = v3(0, 0, 0); }
    }
    Uint64 t1 = SDL_GetPerformanceCounter();
    w->last_ms = (float)((double)(t1 - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency());
}
