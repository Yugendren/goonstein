// Audio: procedural sound effects and an ambient drone, mixed in an SDL3 audio stream.
// No asset files -- everything below is synthesised sample-by-sample.
#include "audio.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

#define SAMPLE_RATE 48000
#define NUM_VOICES 16
#define TWO_PI 6.28318530717958647692f
#define TINY_DC 1e-20f // keeps one-pole filters out of denormal land

// ---------------------------------------------------------------- voices

typedef struct Voice {
    SoundId id;
    bool active;
    float t;        // seconds since the voice started, already scaled by pitch
    float pitch;
    float gain;
    Uint32 seed;    // per-voice xorshift state, used for the noise components
    Uint64 age;     // monotonically increasing, used to steal the oldest voice
} Voice;

typedef struct AudioState {
    SDL_AudioStream *stream;
    SDL_Mutex *mutex;

    Voice voices[NUM_VOICES];
    Uint64 next_age;
    Uint32 rng; // drives the per-voice seed assignment

    float drone_target, drone_level;
    float fight_target, fight_level;
    float master;

    // Filter/phase state for the ambient layers, only ever touched under the lock.
    float drone_lp_state;
    float fight_shimmer_lp;
    Uint32 drone_noise_seed;
    Uint32 fight_noise_seed;
    double drone_phase; // seconds, advances with real time so the drone stays continuous
} AudioState;

static AudioState g_audio;
static bool g_audio_ok = false;

// ---------------------------------------------------------------- small dsp helpers

static inline Uint32 xorshift32(Uint32 *state) {
    Uint32 x = *state ? *state : 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

// white noise in [-1, 1]
static inline float noise1(Uint32 *state) {
    Uint32 r = xorshift32(state);
    return (float)((double)r / (double)0xFFFFFFFFu) * 2.0f - 1.0f;
}

// one-pole low-pass; `a` in (0,1], smaller = darker. Adds a tiny DC offset to
// avoid denormal stalls when the input decays to (near) silence.
static inline float lowpass1(float *state, float in, float a) {
    *state += a * ((in + TINY_DC) - *state);
    return *state;
}

static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float expdecay(float t, float tau) {
    return expf(-t / tau);
}

// linear envelope: ramps 0->1 over `attack` seconds, then 1->0 over the rest of `dur`.
static inline float linenv(float t, float dur, float attack) {
    if (t < 0.0f) return 0.0f;
    if (t < attack) return t / attack;
    float rel = dur - attack;
    if (rel <= 0.0f) return 0.0f;
    float v = 1.0f - (t - attack) / rel;
    return clampf(v, 0.0f, 1.0f);
}

// ---------------------------------------------------------------- per-sound synthesis
//
// Each function returns a mono sample for time t (seconds, already scaled by
// the voice's pitch) and reports whether the sound is still playing via
// *done. `seed` is the voice's private noise state, mutated in place so
// repeated calls advance the noise sequence.

static float synth_footstep(float t, Uint32 *seed, bool *done) {
    const float dur = 0.12f;
    *done = t >= dur;
    if (*done) return 0.0f;
    float env = expdecay(t, 0.035f);
    float thud = sinf(TWO_PI * 90.0f * t) * env;
    float n = noise1(seed) * expdecay(t, 0.015f) * 0.5f;
    return (thud * 0.8f + n) * 0.8f;
}

static float synth_swing(float t, Uint32 *seed, bool *done) {
    const float dur = 0.25f;
    *done = t >= dur;
    if (*done) return 0.0f;
    float frac = t / dur;
    // noise sweep: shape a burst of noise with a rising-then-falling envelope
    // and a cutoff-like amplitude taper so it reads as air being cut.
    float cutoff = 0.08f + 0.5f * sinf((float)M_PI * frac);
    float n = noise1(seed);
    float env = sinf((float)M_PI * frac); // rises then falls across the whole swing
    return n * env * cutoff * 1.6f;
}

static float synth_hit(float t, Uint32 *seed, bool *done) {
    const float dur = 0.2f;
    *done = t >= dur;
    if (*done) return 0.0f;
    float env = expdecay(t, 0.05f);
    float body = sinf(TWO_PI * 70.0f * t) * env;
    float crack = noise1(seed) * expdecay(t, 0.02f) * 0.9f;
    float sub = sinf(TWO_PI * 45.0f * t) * expdecay(t, 0.09f) * 0.6f;
    return ((body + sub) * 0.7f + crack) * 0.55f;
}

static float synth_parry(float t, Uint32 *seed, bool *done) {
    const float dur = 0.45f;
    *done = t >= dur;
    if (*done) return 0.0f;
    float env = expdecay(t, 0.09f);
    float ping = sinf(TWO_PI * 2400.0f * t) * 0.6f + sinf(TWO_PI * 3600.0f * t) * 0.4f;
    float shimmer = sinf(TWO_PI * 5100.0f * t) * 0.15f * expdecay(t, 0.05f);
    float transient = noise1(seed) * expdecay(t, 0.006f) * 0.7f;
    return (ping * env * 0.6f + shimmer + transient) * 0.9f;
}

static float synth_hurt(float t, Uint32 *seed, bool *done) {
    const float dur = 0.35f;
    *done = t >= dur;
    if (*done) return 0.0f;
    float crackEnv = expdecay(t, 0.03f);
    float crack = noise1(seed) * crackEnv * 0.8f;
    float toneEnv = expdecay(t, 0.18f);
    float tone = sinf(TWO_PI * 100.0f * t + 3.0f * sinf(TWO_PI * 20.0f * t)) * toneEnv;
    return (crack * 0.6f + tone * 0.6f) * 0.85f;
}

static float synth_stagger(float t, Uint32 *seed, bool *done) {
    const float dur = 0.8f;
    *done = t >= dur;
    if (*done) return 0.0f;
    float crunchEnv = expdecay(t, 0.12f);
    float crunch = noise1(seed) * crunchEnv;
    float ringEnv = expdecay(t, 0.5f) * (1.0f - expdecay(t, 0.01f));
    float ring = sinf(TWO_PI * 130.0f * t) * 0.6f + sinf(TWO_PI * 195.0f * t) * 0.3f;
    float sub = sinf(TWO_PI * 55.0f * t) * expdecay(t, 0.3f);
    return (crunch * 0.7f + ring * ringEnv * 0.6f + sub * 0.5f) * 0.62f;
}

static float synth_roar(float t, Uint32 *seed, bool *done) {
    const float dur = 1.1f;
    *done = t >= dur;
    if (*done) return 0.0f;
    float env = linenv(t, dur, 0.12f);
    float growl = 1.0f + 0.5f * sinf(TWO_PI * 7.0f * t) + 0.2f * sinf(TWO_PI * 13.0f * t);
    float pitchWobble = 1.0f + 0.08f * sinf(TWO_PI * 5.0f * t);
    float n = noise1(seed);
    float tone = sinf(TWO_PI * 85.0f * pitchWobble * t) * 0.5f;
    return (n * 0.5f + tone) * env * growl * 0.5f;
}

static float synth_death(float t, Uint32 *seed, bool *done) {
    const float dur = 1.5f;
    *done = t >= dur;
    if (*done) return 0.0f;
    float frac = t / dur;
    float freq = 220.0f * powf(0.09f, frac); // descends from 220Hz down two+ octaves
    float env = expdecay(t, 0.9f) * (1.0f - expdecay(t, 0.02f));
    float tone = sinf(TWO_PI * freq * t);
    float n = noise1(seed) * 0.06f * env;
    return tone * env * 0.8f + n;
}

static float synth_blip(float t, Uint32 *seed, bool *done) {
    const float dur = 0.05f;
    *done = t >= dur;
    if (*done) return 0.0f;
    (void)seed;
    float env = expdecay(t, 0.015f);
    return sinf(TWO_PI * 1200.0f * t) * env * 0.7f;
}

static float synth_heart(float t, Uint32 *seed, bool *done) {
    const float dur = 0.25f;
    *done = t >= dur;
    if (*done) return 0.0f;
    (void)seed;
    float out = 0.0f;
    float t1 = t;
    if (t1 >= 0.0f && t1 < 0.15f) {
        float env = expdecay(t1, 0.045f);
        out += sinf(TWO_PI * 55.0f * t1) * env;
    }
    float t2 = t - 0.13f;
    if (t2 >= 0.0f && t2 < 0.15f) {
        float env = expdecay(t2, 0.045f) * 0.55f;
        out += sinf(TWO_PI * 52.0f * t2) * env;
    }
    return out * 0.9f;
}

static float synth_door(float t, Uint32 *seed, bool *done) {
    const float dur = 1.0f;
    *done = t >= dur;
    if (*done) return 0.0f;
    float creakEnv = linenv(t, dur, 0.35f);
    float creak = sinf(TWO_PI * (60.0f + 8.0f * sinf(TWO_PI * 1.5f * t)) * t) * creakEnv;
    float thudEnv = (t > 0.75f) ? expdecay(t - 0.75f, 0.08f) : 0.0f;
    float thud = sinf(TWO_PI * 60.0f * t) * thudEnv;
    float n = noise1(seed) * creakEnv * 0.15f;
    return creak * 0.5f + thud * 0.9f + n;
}

static float synth_sting(float t, Uint32 *seed, bool *done) {
    const float dur = 1.6f;
    *done = t >= dur;
    if (*done) return 0.0f;
    float env = linenv(t, dur, 0.9f); // slow swell in, then fades
    float f1 = 46.25f;                // low F#
    float f2 = f1 * 1.4142135f;       // tritone above
    float d1 = 0.5f * (t - t * 0.5f); // gentle detune drift so it beats
    float saw1 = 0.0f, saw2 = 0.0f;
    // cheap band-limited-ish saw via a few harmonics, keeps it inexpensive and NaN-free
    for (int k = 1; k <= 6; k++) {
        float amp = 1.0f / (float)k;
        saw1 += sinf(TWO_PI * f1 * (float)k * t + d1) * amp;
        saw2 += sinf(TWO_PI * f2 * (float)k * t) * amp;
    }
    float n = noise1(seed) * 0.15f * env;
    return (saw1 * 0.18f + saw2 * 0.18f) * env + n;
}

static float synth_sound(SoundId id, float t, Uint32 *seed, bool *done) {
    switch (id) {
        case SND_FOOTSTEP: return synth_footstep(t, seed, done);
        case SND_SWING:    return synth_swing(t, seed, done);
        case SND_HIT:      return synth_hit(t, seed, done);
        case SND_PARRY:    return synth_parry(t, seed, done);
        case SND_HURT:     return synth_hurt(t, seed, done);
        case SND_STAGGER:  return synth_stagger(t, seed, done);
        case SND_ROAR:     return synth_roar(t, seed, done);
        case SND_DEATH:    return synth_death(t, seed, done);
        case SND_BLIP:     return synth_blip(t, seed, done);
        case SND_HEART:    return synth_heart(t, seed, done);
        case SND_DOOR:     return synth_door(t, seed, done);
        case SND_STING:    return synth_sting(t, seed, done);
        default:           *done = true; return 0.0f;
    }
}

// ---------------------------------------------------------------- mixer callback

static void mix_block(float *out, int frames) {
    SDL_LockMutex(g_audio.mutex);

    // Smooth the ambient parameters with a one-pole low-pass, time constant
    // ~0.5s, computed once per callback block (block length is short enough
    // that per-sample smoothing isn't needed).
    float block_dt = (float)frames / (float)SAMPLE_RATE;
    float smooth_a = 1.0f - expf(-block_dt / 0.5f);
    g_audio.drone_level += smooth_a * (g_audio.drone_target - g_audio.drone_level);
    g_audio.fight_level += smooth_a * (g_audio.fight_target - g_audio.fight_level);
    float drone_level = g_audio.drone_level;
    float fight_level = g_audio.fight_level;
    float master = g_audio.master;

    Voice local[NUM_VOICES];
    memcpy(local, g_audio.voices, sizeof local);

    for (int i = 0; i < frames; i++) {
        float mix = 0.0f;

        for (int v = 0; v < NUM_VOICES; v++) {
            if (!local[v].active) continue;
            bool done = false;
            float s = synth_sound(local[v].id, local[v].t, &local[v].seed, &done);
            if (done) { local[v].active = false; continue; }
            mix += s * local[v].gain;
            local[v].t += local[v].pitch / (float)SAMPLE_RATE;
        }

        // Drone: two detuned low sines plus filtered noise, breathing with intensity.
        double dt = g_audio.drone_phase;
        float droneTone = sinf((float)(TWO_PI * 55.0 * dt)) * 0.6f
                         + sinf((float)(TWO_PI * 82.4 * dt) + 0.7f) * 0.4f;
        float droneNoise = lowpass1(&g_audio.drone_lp_state, noise1(&g_audio.drone_noise_seed), 0.02f);
        float drone = (droneTone * 0.5f + droneNoise * 1.5f) * drone_level;

        // Fight layer: a slow pulse on a low sine, plus a faint noise shimmer.
        float pulse = 0.5f + 0.5f * sinf((float)(TWO_PI * 1.2 * dt));
        float fightTone = sinf((float)(TWO_PI * 41.0 * dt)) * pulse;
        float shimmerRaw = noise1(&g_audio.fight_noise_seed);
        float shimmer = shimmerRaw - lowpass1(&g_audio.fight_shimmer_lp, shimmerRaw, 0.35f); // crude high-pass
        float fight = (fightTone * 0.7f + shimmer * 0.25f) * fight_level;

        mix += drone + fight;
        mix *= master;
        mix = tanhf(mix);
        if (!isfinite(mix)) mix = 0.0f;

        out[i * 2 + 0] = mix;
        out[i * 2 + 1] = mix;

        g_audio.drone_phase += 1.0 / (double)SAMPLE_RATE;
    }

    memcpy(g_audio.voices, local, sizeof local);
    SDL_UnlockMutex(g_audio.mutex);
}

static void SDLCALL audio_callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
    (void)userdata;
    (void)total_amount;
    if (additional_amount <= 0) return;

    // additional_amount is in bytes of the stream's format (F32 stereo).
    static float buf[4096 * 2];
    int frames = additional_amount / (int)(sizeof(float) * 2);
    while (frames > 0) {
        int chunk = frames > 4096 ? 4096 : frames;
        mix_block(buf, chunk);
        SDL_PutAudioStreamData(stream, buf, chunk * (int)(sizeof(float) * 2));
        frames -= chunk;
    }
}

// ---------------------------------------------------------------- public api

bool audio_init(void) {
    memset(&g_audio, 0, sizeof g_audio);
    g_audio.master = 1.0f;
    g_audio.rng = 0xC0FFEEu;
    g_audio.drone_noise_seed = 0x1234ABCDu;
    g_audio.fight_noise_seed = 0xFACEFEEDu;
    g_audio_ok = false;

    g_audio.mutex = SDL_CreateMutex();
    if (!g_audio.mutex) return false;

    SDL_AudioSpec spec = { .format = SDL_AUDIO_F32, .channels = 2, .freq = SAMPLE_RATE };
    g_audio.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_callback, NULL);
    if (!g_audio.stream) {
        SDL_DestroyMutex(g_audio.mutex);
        g_audio.mutex = NULL;
        return false;
    }

    if (!SDL_ResumeAudioStreamDevice(g_audio.stream)) {
        SDL_DestroyAudioStream(g_audio.stream);
        g_audio.stream = NULL;
        SDL_DestroyMutex(g_audio.mutex);
        g_audio.mutex = NULL;
        return false;
    }

    g_audio_ok = true;
    return true;
}

void audio_shutdown(void) {
    if (g_audio.stream) {
        SDL_DestroyAudioStream(g_audio.stream);
        g_audio.stream = NULL;
    }
    if (g_audio.mutex) {
        SDL_DestroyMutex(g_audio.mutex);
        g_audio.mutex = NULL;
    }
    g_audio_ok = false;
}

void audio_play(SoundId id, float gain, float pitch) {
    if (!g_audio_ok || !g_audio.mutex) return;
    if (id < 0 || id >= SND_COUNT) return;
    if (pitch <= 0.0001f) pitch = 1.0f;

    SDL_LockMutex(g_audio.mutex);

    int slot = -1;
    for (int v = 0; v < NUM_VOICES; v++) {
        if (!g_audio.voices[v].active) { slot = v; break; }
    }
    if (slot < 0) {
        // steal the oldest voice
        Uint64 oldest_age = g_audio.voices[0].age;
        slot = 0;
        for (int v = 1; v < NUM_VOICES; v++) {
            if (g_audio.voices[v].age < oldest_age) { oldest_age = g_audio.voices[v].age; slot = v; }
        }
    }

    Voice *voice = &g_audio.voices[slot];
    voice->id = id;
    voice->active = true;
    voice->t = 0.0f;
    voice->pitch = pitch;
    voice->gain = clampf(gain, 0.0f, 4.0f);
    voice->seed = xorshift32(&g_audio.rng) | 1u; // never zero
    voice->age = g_audio.next_age++;

    SDL_UnlockMutex(g_audio.mutex);
}

void audio_set_drone(float intensity) {
    if (!g_audio_ok || !g_audio.mutex) return;
    SDL_LockMutex(g_audio.mutex);
    g_audio.drone_target = clampf(intensity, 0.0f, 1.0f);
    SDL_UnlockMutex(g_audio.mutex);
}

void audio_set_fight(float intensity) {
    if (!g_audio_ok || !g_audio.mutex) return;
    SDL_LockMutex(g_audio.mutex);
    g_audio.fight_target = clampf(intensity, 0.0f, 1.0f);
    SDL_UnlockMutex(g_audio.mutex);
}

void audio_set_master(float v) {
    if (!g_audio_ok || !g_audio.mutex) return;
    SDL_LockMutex(g_audio.mutex);
    g_audio.master = clampf(v, 0.0f, 1.0f);
    SDL_UnlockMutex(g_audio.mutex);
}
