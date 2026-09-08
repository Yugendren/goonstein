// Particle system: motes, mist, sparks and embers for a dark, glowy Ori-style world.
// Everything lives in fixed-size arrays, no allocations. Particles are drawn as camera-facing
// quads through gfx_billboard: additive types glow (bloom picks up anything bright, so additive
// colours may exceed 1.0), alpha types shade (mist, smoke, falling leaves).
#pragma once
#include "hmath.h"
#include "gfx.h"

typedef enum ParticleType { PT_SPARK, PT_EMBER, PT_FIREFLY, PT_MIST, PT_SPORE, PT_LEAF, PT_SMOKE, PT_COUNT } ParticleType;
ParticleType particle_type_from_name(const char *s);   // "spark" "ember" "firefly" "mist" "spore" "leaf" "smoke"; PT_SPORE if unknown

typedef struct Particle {
    ParticleType type; bool additive;
    Vec3 pos, vel; Vec3 color; float size, life, max_life, seed;
} Particle;

typedef struct Emitter {
    ParticleType type;
    Vec3 pos, extent;      // spawn box: pos is the centre, extent the half-size
    float rate;            // particles per second
    Vec3 color; float size; float life;
    bool active;
    float accum;           // internal
} Emitter;

#define PARTICLES_MAX 4096
#define EMITTERS_MAX  48

typedef struct Particles {
    Particle p[PARTICLES_MAX]; int count;      // dense: alive particles are [0, count)
    Emitter emitters[EMITTERS_MAX]; int nemitters;
    float time;
} Particles;

void particles_init(Particles *ps);
void particles_clear(Particles *ps);                          // kills all particles and emitters
int  particles_add_emitter(Particles *ps, const Emitter *e);  // returns index or -1
void particles_prewarm(Particles *ps, float seconds);         // run the sim so ambient emitters are already populated
// One-off burst: count particles from pos, biased along dir (dir may be zero for omni).
void particles_burst(Particles *ps, ParticleType type, Vec3 pos, Vec3 dir, int count, float speed, Vec3 color, float size, float life);
void particles_update(Particles *ps, float dt);
void particles_draw(const Particles *ps, Gfx *g);
