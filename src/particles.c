// See particles.h. Simulation (position/velocity/life) lives in particles_update; the visual
// curves (brightness, size, alpha over life) live in particles_draw so the two stay decoupled.
#include "particles.h"
#include <string.h>

#ifdef PARTICLES_TEST
#include <stdio.h>
#endif

// ---------------------------------------------------------------- deterministic rng

static Uint32 g_rng_state = 0x9e3779b9u;

static inline Uint32 xorshift32(Uint32 *state) {
    Uint32 x = *state ? *state : 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static inline float frand(void) {  // [0, 1)
    Uint32 r = xorshift32(&g_rng_state);
    return (float)((double)r / ((double)0xFFFFFFFFu + 1.0));
}
static inline float frand_range(float lo, float hi) { return lo + (hi - lo) * frand(); }
static inline float frand_signed(void) { return frand_range(-1.0f, 1.0f); }

// Cheap deterministic hash of a float into [0, 1), used to derive a stable per-particle
// constant (e.g. mist alpha) from `seed` without needing an extra struct field.
static inline float hash1(float x) {
    float y = sinf(x * 127.1f) * 43758.5453f;
    return y - floorf(y);
}

// Blend of `axis` and a random direction; spread 0 = exactly along axis, 1 = full sphere.
static Vec3 rand_dir_spread(Vec3 axis, float spread) {
    Vec3 rnd = v3(frand_signed(), frand_signed(), frand_signed());
    Vec3 a = v3_len(axis) > 1e-6f ? v3_norm(axis) : v3(0, 1, 0);
    Vec3 dir = v3_add(v3_scale(a, 1.0f - spread), v3_scale(rnd, spread));
    return v3_norm(dir);
}

static Vec3 rand_in_box(Vec3 center, Vec3 extent) {
    return v3_add(center, v3(frand_signed() * extent.x, frand_signed() * extent.y, frand_signed() * extent.z));
}

// ---------------------------------------------------------------- type names

ParticleType particle_type_from_name(const char *s) {
    if (!s) return PT_SPORE;
    if (strcmp(s, "spark") == 0) return PT_SPARK;
    if (strcmp(s, "ember") == 0) return PT_EMBER;
    if (strcmp(s, "firefly") == 0) return PT_FIREFLY;
    if (strcmp(s, "mist") == 0) return PT_MIST;
    if (strcmp(s, "spore") == 0) return PT_SPORE;
    if (strcmp(s, "leaf") == 0) return PT_LEAF;
    if (strcmp(s, "smoke") == 0) return PT_SMOKE;
    return PT_SPORE;
}

// ---------------------------------------------------------------- lifecycle

void particles_init(Particles *ps) {
    memset(ps, 0, sizeof(*ps));
}

void particles_clear(Particles *ps) {
    ps->count = 0;
    ps->nemitters = 0;
}

int particles_add_emitter(Particles *ps, const Emitter *e) {
    if (ps->nemitters >= EMITTERS_MAX) return -1;
    int i = ps->nemitters++;
    ps->emitters[i] = *e;
    ps->emitters[i].accum = 0.0f;
    return i;
}

// Adds a particle to the pool. When full, replaces the oldest (largest elapsed time) so
// long-lived ambient particles (mist, fireflies) don't get starved by a burst.
static void spawn_particle(Particles *ps, const Particle *p) {
    if (ps->count < PARTICLES_MAX) {
        ps->p[ps->count++] = *p;
        return;
    }
    int oldest = 0;
    float best_elapsed = -1.0f;
    for (int i = 0; i < ps->count; i++) {
        float elapsed = ps->p[i].max_life - ps->p[i].life;
        if (elapsed > best_elapsed) { best_elapsed = elapsed; oldest = i; }
    }
    ps->p[oldest] = *p;
}

static void init_particle_common(Particle *p, ParticleType type, Vec3 pos, Vec3 color, float size, float life) {
    p->type = type;
    p->pos = pos;
    p->vel = v3(0, 0, 0);
    p->color = color;
    p->size = size;
    p->life = life;
    p->max_life = life;
    p->seed = frand_range(0.0f, 1000.0f);
    // alpha-blended types shade; everything else glows
    p->additive = (type != PT_MIST && type != PT_LEAF && type != PT_SMOKE);
}

static void emitter_spawn(Particles *ps, const Emitter *e) {
    Particle p;
    Vec3 pos = rand_in_box(e->pos, e->extent);
    bool has_color = (e->color.x != 0.0f || e->color.y != 0.0f || e->color.z != 0.0f);

    switch (e->type) {
    case PT_SPARK: {
        float life = frand_range(0.3f, 0.6f);
        float size = e->size > 0 ? e->size : frand_range(0.05f, 0.1f);
        init_particle_common(&p, PT_SPARK, pos, e->color, size, life);
        Vec3 dir = rand_dir_spread(v3(0, 1, 0), 0.7f);
        p.vel = v3_scale(dir, frand_range(2.0f, 5.0f));
        break;
    }
    case PT_EMBER: {
        float life = frand_range(1.0f, 2.5f);
        float size = e->size > 0 ? e->size : frand_range(0.05f, 0.12f);
        init_particle_common(&p, PT_EMBER, pos, e->color, size, life);
        p.vel = v3(0, frand_range(0.3f, 0.8f), 0);
        break;
    }
    case PT_FIREFLY: {
        float life = frand_range(4.0f, 10.0f);
        float size = e->size > 0 ? e->size : frand_range(0.08f, 0.15f);
        Vec3 color = has_color ? e->color : v3(0.65f, 0.9f, 0.35f);  // warm yellow-green
        init_particle_common(&p, PT_FIREFLY, pos, color, size, life);
        break;
    }
    case PT_MIST: {
        float life = frand_range(6.0f, 12.0f);
        float size = e->size > 0 ? e->size : frand_range(1.5f, 3.0f);
        init_particle_common(&p, PT_MIST, pos, e->color, size, life);
        p.vel = v3(frand_range(-0.15f, 0.15f), 0.0f, frand_range(-0.15f, 0.15f));
        break;
    }
    case PT_SPORE: {
        float life = frand_range(3.0f, 6.0f);
        float size = e->size > 0 ? e->size : frand_range(0.02f, 0.05f);
        init_particle_common(&p, PT_SPORE, pos, e->color, size, life);
        p.vel = v3(0, frand_range(0.15f, 0.35f), 0);
        break;
    }
    case PT_LEAF: {
        float life = frand_range(4.0f, 9.0f);  // capped short once it lands, see update
        float size = e->size > 0 ? e->size : 0.1f;
        init_particle_common(&p, PT_LEAF, pos, e->color, size, life);
        p.vel = v3(0, -0.6f, 0);
        break;
    }
    case PT_SMOKE: {
        float life = frand_range(1.5f, 3.0f);
        float size = e->size > 0 ? e->size : frand_range(0.2f, 0.4f);
        Vec3 color = has_color ? e->color : v3(0.5f, 0.5f, 0.52f);
        init_particle_common(&p, PT_SMOKE, pos, color, size, life);
        p.vel = v3(0, frand_range(0.4f, 0.8f), 0);
        break;
    }
    default:
        init_particle_common(&p, e->type, pos, e->color, e->size, frand_range(1.0f, 2.0f));
        break;
    }
    spawn_particle(ps, &p);
}

void particles_burst(Particles *ps, ParticleType type, Vec3 pos, Vec3 dir, int count, float speed, Vec3 color, float size, float life) {
    bool omni = v3_len(dir) <= 1e-5f;
    for (int i = 0; i < count; i++) {
        Particle p;
        init_particle_common(&p, type, pos, color, size, life);
        Vec3 rd = rand_dir_spread(dir, omni ? 1.0f : 0.45f);
        p.vel = v3_scale(rd, speed * frand_range(0.6f, 1.0f));
        if (type == PT_MIST || type == PT_LEAF) p.vel = v3_scale(p.vel, 0.2f);  // keep those grounded-ish
        spawn_particle(ps, &p);
    }
}

// ---------------------------------------------------------------- simulation

static void update_physics(const Particles *ps, Particle *p, float dt) {
    switch (p->type) {
    case PT_SPARK: {
        p->vel.y -= 12.0f * dt;
        p->pos = v3_add(p->pos, v3_scale(p->vel, dt));
        if (p->pos.y < 0.0f && p->vel.y < 0.0f) {
            p->pos.y = 0.0f;
            p->vel.y = -p->vel.y * 0.35f;   // damped bounce
            p->vel.x *= 0.6f; p->vel.z *= 0.6f;
        }
        break;
    }
    case PT_EMBER: {
        p->vel.x = sinf(ps->time * 1.3f + p->seed * 6.2831853f) * 0.15f;
        p->vel.z = cosf(ps->time * 1.7f + p->seed * 6.2831853f) * 0.15f;
        p->pos = v3_add(p->pos, v3_scale(p->vel, dt));
        break;
    }
    case PT_FIREFLY: {
        float wx = sinf(ps->time * 0.8f + p->seed) + sinf(ps->time * 1.9f + p->seed * 3.1f);
        float wy = sinf(ps->time * 0.6f + p->seed * 2.3f) + sinf(ps->time * 1.5f + p->seed * 4.7f);
        float wz = sinf(ps->time * 1.1f + p->seed * 1.7f) + sinf(ps->time * 2.2f + p->seed * 5.3f);
        p->vel = v3(wx * 0.12f, wy * 0.06f, wz * 0.12f);
        p->pos = v3_add(p->pos, v3_scale(p->vel, dt));
        break;
    }
    case PT_MIST: {
        p->pos = v3_add(p->pos, v3_scale(p->vel, dt));
        break;
    }
    case PT_SPORE: {
        p->vel.x = sinf(ps->time * 0.9f + p->seed * 3.3f) * 0.1f;
        p->vel.z = cosf(ps->time * 1.1f + p->seed * 4.1f) * 0.1f;
        p->pos = v3_add(p->pos, v3_scale(p->vel, dt));
        break;
    }
    case PT_LEAF: {
        p->vel.x = sinf(ps->time * 2.0f + p->seed * 6.2831853f) * 0.3f;
        p->vel.z = cosf(ps->time * 1.4f + p->seed * 6.2831853f) * 0.2f;
        p->pos = v3_add(p->pos, v3_scale(p->vel, dt));
        if (p->pos.y <= 0.0f) {
            p->pos.y = 0.0f;
            p->vel = v3(0, 0, 0);
            p->life = fminf(p->life, 0.25f);  // land, then fade quickly (see draw curve)
        }
        break;
    }
    case PT_SMOKE: {
        p->vel.x = sinf(ps->time * 1.0f + p->seed * 2.6f) * 0.1f;
        p->vel.z = cosf(ps->time * 1.3f + p->seed * 3.4f) * 0.1f;
        p->pos = v3_add(p->pos, v3_scale(p->vel, dt));
        break;
    }
    default: break;
    }
}

void particles_update(Particles *ps, float dt) {
    ps->time += dt;

    for (int i = 0; i < ps->nemitters; i++) {
        Emitter *e = &ps->emitters[i];
        if (!e->active || e->rate <= 0.0f) continue;
        e->accum += e->rate * dt;
        while (e->accum >= 1.0f) {
            emitter_spawn(ps, e);
            e->accum -= 1.0f;
        }
    }

    for (int i = 0; i < ps->count; i++) {
        Particle *p = &ps->p[i];
        p->life -= dt;
        if (p->life <= 0.0f) {
            ps->p[i] = ps->p[ps->count - 1];
            ps->count--;
            i--;
            continue;
        }
        update_physics(ps, p, dt);
    }
}

void particles_prewarm(Particles *ps, float seconds) {
    const float step = 1.0f / 30.0f;
    for (float t = 0.0f; t < seconds; t += step) particles_update(ps, step);
}

// ---------------------------------------------------------------- draw

#ifdef PARTICLES_TEST
// Stub so the standalone test doesn't touch the real renderer; counts calls instead.
static int g_billboard_calls = 0;
void gfx_billboard(Gfx *g, Vec3 pos, float size, Vec4 color, bool additive) {
    (void)g; (void)pos; (void)size; (void)color; (void)additive;
    g_billboard_calls++;
}
#endif

void particles_draw(const Particles *ps, Gfx *g) {
    for (int i = 0; i < ps->count; i++) {
        const Particle *p = &ps->p[i];
        float age = clampf(1.0f - p->life / p->max_life, 0.0f, 1.0f);

        switch (p->type) {
        case PT_SPARK: {
            float flash = 1.0f - smoothstep(age * 4.0f);              // bright burst early
            Vec3 col = v3_lerp(p->color, v3(4.0f, 4.0f, 3.5f), flash * 0.7f);
            Vec3 out = v3_scale(col, (1.0f - age) * 3.0f + 0.3f);
            float size = p->size * lerpf(1.0f, 0.15f, age);
            gfx_billboard(g, p->pos, size, v4(out.x, out.y, out.z, 1.0f), true);
            break;
        }
        case PT_EMBER: {
            float flicker = 0.6f + 0.4f * sinf(ps->time * 20.0f + p->seed * 6.2831853f);
            float fade = 1.0f - age;
            Vec3 out = v3_scale(p->color, flicker * fade * 2.0f);
            gfx_billboard(g, p->pos, p->size, v4(out.x, out.y, out.z, 1.0f), true);
            break;
        }
        case PT_FIREFLY: {
            float pulse = 0.5f + 0.5f * sinf(ps->time * (2.0f * PI / 1.5f) + p->seed * 6.2831853f);
            float fade_in = smoothstep(age * 8.0f);
            float fade_out = smoothstep((1.0f - age) * 8.0f);
            float envelope = fminf(fade_in, fade_out);
            float bright = (0.3f + 0.9f * pulse) * envelope;
            Vec3 out = v3_scale(p->color, bright * 2.2f);
            gfx_billboard(g, p->pos, p->size, v4(out.x, out.y, out.z, 1.0f), true);
            break;
        }
        case PT_MIST: {
            float alpha_base = lerpf(0.05f, 0.12f, hash1(p->seed));
            float in = smoothstep(age / 0.2f);
            float out = smoothstep((1.0f - age) / 0.3f);
            float alpha = alpha_base * fminf(in, out);
            gfx_billboard(g, p->pos, p->size, v4(p->color.x, p->color.y, p->color.z, alpha), false);
            break;
        }
        case PT_SPORE: {
            float in = smoothstep(age / 0.15f);
            float out = smoothstep((1.0f - age) / 0.25f);
            Vec3 col = v3_scale(p->color, fminf(in, out) * 1.5f);
            gfx_billboard(g, p->pos, p->size, v4(col.x, col.y, col.z, 1.0f), true);
            break;
        }
        case PT_LEAF: {
            float alpha = clampf(1.0f - age, 0.0f, 1.0f) * 0.9f;
            gfx_billboard(g, p->pos, p->size, v4(p->color.x, p->color.y, p->color.z, alpha), false);
            break;
        }
        case PT_SMOKE: {
            float size = p->size * lerpf(1.0f, 3.0f, age);
            float in = smoothstep(age / 0.15f);
            float out = 1.0f - smoothstep((age - 0.15f) / 0.85f);
            float alpha = 0.35f * in * out;
            gfx_billboard(g, p->pos, size, v4(p->color.x, p->color.y, p->color.z, alpha), false);
            break;
        }
        default: break;
        }
    }
}

// ---------------------------------------------------------------- self-test

#ifdef PARTICLES_TEST
int main(void) {
    Particles ps;
    particles_init(&ps);

    Emitter firefly = {0};
    firefly.type = PT_FIREFLY;
    firefly.pos = v3(0, 1.2f, 0);
    firefly.extent = v3(5, 0.8f, 5);
    firefly.rate = 3.0f;
    firefly.color = v3(0.65f, 0.9f, 0.35f);
    firefly.size = 0.1f;
    firefly.life = 6.0f;
    firefly.active = true;
    particles_add_emitter(&ps, &firefly);

    Emitter mist = {0};
    mist.type = PT_MIST;
    mist.pos = v3(0, 0.2f, 0);
    mist.extent = v3(10, 0.1f, 10);
    mist.rate = 2.0f;
    mist.color = v3(0.5f, 0.6f, 0.7f);
    mist.size = 2.0f;
    mist.life = 8.0f;
    mist.active = true;
    particles_add_emitter(&ps, &mist);

    particles_prewarm(&ps, 5.0f);
    particles_update(&ps, 2.0f);

    float miny = 1e9f, maxy = -1e9f;
    for (int i = 0; i < ps.count; i++) {
        if (ps.p[i].pos.y < miny) miny = ps.p[i].pos.y;
        if (ps.p[i].pos.y > maxy) maxy = ps.p[i].pos.y;
    }
    printf("after prewarm(5s)+update(2s): count=%d min_y=%.3f max_y=%.3f\n", ps.count, miny, maxy);

    particles_burst(&ps, PT_SPARK, v3(0, 1, 0), v3(0, 1, 0), 40, 4.0f, v3(1.0f, 0.8f, 0.4f), 0.08f, 0.5f);
    printf("after burst: count=%d\n", ps.count);

    Gfx g;
    memset(&g, 0, sizeof(g));
    particles_draw(&ps, &g);
    printf("billboard calls=%d\n", g_billboard_calls);

    return 0;
}
#endif
