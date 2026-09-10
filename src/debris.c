// See items.h. Debris is confetti, not physics: it bounces once off the ground and fades, but
// never collides with blocks or anything else. Its own tiny deterministic RNG (not shared with
// particles.c or audio.c) keeps a fixed tick reproducible run to run.
#include <stddef.h>
#include "items.h"
#include "game.h"
#include <math.h>

#define DEBRIS_GRAVITY 18.0f   // matches GAME_GRAVITY in game.c

// ---------------------------------------------------------------- deterministic rng
static Uint32 g_debris_rng = 0x2545f491u;   // static counter: seeded once, never from time or rand()

static inline Uint32 debris_xorshift32(Uint32 *state) {
    Uint32 x = *state ? *state : 0x2545f491u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static inline float debris_frand(void) {  // [0, 1)
    Uint32 r = debris_xorshift32(&g_debris_rng);
    return (float)((double)r / ((double)0xFFFFFFFFu + 1.0));
}
static inline float debris_frand_range(float lo, float hi) { return lo + (hi - lo) * debris_frand(); }
static inline float debris_frand_signed(void) { return debris_frand_range(-1.0f, 1.0f); }

void debris_burst(Items *its, Vec3 at, Vec4 tint, float size, int count, float speed) {
    if (count < 1) count = 1;
    if (count > 24) count = 24;

    for (int i = 0; i < count; i++) {
        // Find a free slot, or the one with the least life left (overwrite the oldest-looking).
        int slot = -1;
        float worst_life = 1e30f;
        int worst_slot = 0;
        for (int j = 0; j < DEBRIS_MAX; j++) {
            Debris *d = &its->debris[j];
            if (!d->used) { slot = j; break; }
            if (d->life < worst_life) { worst_life = d->life; worst_slot = j; }
        }
        if (slot < 0) slot = worst_slot;

        Debris *d = &its->debris[slot];
        d->used = true;
        d->pos = v3(at.x + debris_frand_signed() * size, at.y + debris_frand_signed() * size,
                    at.z + debris_frand_signed() * size);

        Vec3 dir = v3(debris_frand_signed(), debris_frand_range(0.4f, 1.0f), debris_frand_signed());
        dir = v3_norm(dir);
        float spd = speed * debris_frand_range(0.6f, 1.4f);
        d->vel = v3_scale(dir, spd);

        d->rot = quat_identity();
        Vec3 spin_axis = v3_norm(v3(debris_frand_signed(), debris_frand_signed(), debris_frand_signed()));
        d->spin = v3_scale(spin_axis, debris_frand_range(4.0f, 12.0f));

        d->tint = tint;
        d->size = size * debris_frand_range(0.5f, 1.0f);
        d->life = d->max_life = 3.0f;
    }
}

void debris_update(struct Game *g, float dt) {
    Items *its = &g->items;
    for (int i = 0; i < DEBRIS_MAX; i++) {
        Debris *d = &its->debris[i];
        if (!d->used) continue;

        d->life -= dt;
        if (d->life <= 0) { d->used = false; continue; }

        d->vel.y -= DEBRIS_GRAVITY * dt;
        d->pos = v3_add(d->pos, v3_scale(d->vel, dt));

        float spin_len = v3_len(d->spin);
        if (spin_len > 1e-6f) {
            Quat dq = quat_from_axis_angle(v3_scale(d->spin, 1.0f / spin_len), spin_len * dt);
            d->rot = quat_norm(quat_mul(dq, d->rot));
        }

        float ground = g->terrain.present ? terrain_height(&g->terrain, d->pos.x, d->pos.z) : 0.0f;
        if (d->pos.y <= ground) {
            d->pos.y = ground;
            d->vel.y = -d->vel.y * 0.35f;
            d->vel.x *= 0.7f;
            d->vel.z *= 0.7f;
            d->spin = v3_scale(d->spin, 0.6f);
        }
    }
}

void debris_draw(struct Game *g) {
    Items *its = &g->items;
    for (int i = 0; i < DEBRIS_MAX; i++) {
        const Debris *d = &its->debris[i];
        if (!d->used) continue;

        float t = clampf(d->life / d->max_life, 0.0f, 1.0f);
        float shrink = t * t;
        float s = d->size * shrink;
        if (s < 0.005f) continue;

        Mat4 world = m4_from_trs(d->pos, d->rot, v3(s, s, s));
        props_draw_matrix(&g->gfx, &g->props, "models/shapes/box.obj", world, d->tint, v3(0, 0, 0), NULL, 0, 0);
    }
}
