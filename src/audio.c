// Audio: procedural sound effects and an ambient drone, mixed in an SDL3 audio stream.
// Everything in the SoundId layer below is synthesised sample-by-sample; on top of that this
// file also supports loading WAV/OGG sample assets and a single (crossfading) music track.
#include "audio.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// stb_vorbis is a single-file library shipped as a .c file; include it directly so it becomes
// part of this translation unit. We use its simplest API (stb_vorbis_decode_filename), so the
// push-data streaming API is disabled to keep the compiled surface small.
#define STB_VORBIS_NO_PUSHDATA_API
#include "vendor/stb_vorbis.c"

#define SAMPLE_RATE 48000
#define NUM_VOICES 16
#define TWO_PI 6.28318530717958647692f
#define TINY_DC 1e-20f // keeps one-pole filters out of denormal land

#define MAX_SAMPLES 128        // loaded WAV/OGG assets, cached by path
#define NUM_SAMPLE_VOICES 24   // concurrently playing sample instances

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

// ------------------------------------------------------------- sample assets

// A fully-decoded WAV/OGG asset: interleaved stereo float at SAMPLE_RATE. Loaded (and malloc'd)
// on the main thread; once published into g_samples[] it is immutable, so the mixer can read it
// without a lock.
typedef struct SampleData {
    char path[512];
    float *frames; // interleaved L,R,L,R...
    int frame_count;
} SampleData;

static SampleData g_samples[MAX_SAMPLES];
static int g_sample_count = 0;

// A currently-playing instance of a loaded sample.
typedef struct SampleVoice {
    bool active;
    int sample_id;
    float pos;   // fractional frame index into the sample, advances by `pitch` per output frame
    float pitch;
    float gain;
    Uint64 age;
} SampleVoice;

// ------------------------------------------------------------------- music

// One music track: fully decoded like a sample, plus loop/fade state used to crossfade between
// tracks. Fades are updated once per callback block (not per-sample).
typedef struct MusicTrack {
    float *frames;
    int frame_count;
    bool loop;
    bool active;
    int pos;          // integer frame index (music always plays at its native decoded rate)
    float gain;        // per-track gain, set at audio_music_play()
    float fade;         // current envelope value, 0..1
    float fade_target;  // 0 (fading out, stops when reached) or 1 (fading in / steady)
    float fade_rate;     // envelope change per second
    char path[512];
} MusicTrack;

typedef struct AudioState {
    SDL_AudioStream *stream;
    SDL_Mutex *mutex;

    Voice voices[NUM_VOICES];
    Uint64 next_age;
    Uint32 rng; // drives the per-voice seed assignment

    SampleVoice sample_voices[NUM_SAMPLE_VOICES];

    MusicTrack music_cur;  // the track that's current (playing in / steady / fading out on stop)
    MusicTrack music_prev; // the previous track, fading out during a crossfade
    float music_gain;      // bus gain for music, on top of each track's own gain

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

static float synth_whiff(float t, Uint32 *seed, bool *done) {
    const float dur = 0.2f;
    *done = t >= dur;
    if (*done) return 0.0f;
    float frac = t / dur;
    float env = sinf((float)M_PI * frac);
    float n = noise1(seed) * env * 0.35f;
    float creak = sinf(TWO_PI * (180.0f + 60.0f * frac) * t) * env * 0.25f;
    return (n + creak) * 0.7f;
}

static float synth_fail(float t, Uint32 *seed, bool *done) {
    const float dur = 0.3f;
    *done = t >= dur;
    if (*done) return 0.0f;
    float env = expdecay(t, 0.07f);
    float clunk = sinf(TWO_PI * 140.0f * t) * env + sinf(TWO_PI * 95.0f * t) * expdecay(t, 0.12f) * 0.7f;
    float knock = noise1(seed) * expdecay(t, 0.012f) * 0.5f;
    return (clunk * 0.6f + knock) * 0.8f;
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

static float synth_grab(float t, Uint32 *seed, bool *done) {
    const float dur = 0.12f;
    *done = t >= dur;
    if (*done) return 0.0f;
    // Hands closing on an object: a soft scuff, not a hit. Average two consecutive noise samples
    // as a crude one-pole-ish smoothing (no persistent per-voice filter state to reuse here) so
    // the noise reads as cloth/wood texture rather than a sharp hiss, under a short punchy env.
    float env = expdecay(t, 0.035f);
    float n1 = noise1(seed);
    float n2 = noise1(seed);
    float scuff = (n1 + n2) * 0.5f * env;
    // A brief low wood-ish tap for the moment of contact, gone almost immediately.
    float tap = sinf(TWO_PI * 260.0f * t) * expdecay(t, 0.018f) * 0.3f;
    return (scuff * 0.6f + tap) * 0.6f;
}

static float synth_drop(float t, Uint32 *seed, bool *done) {
    const float dur = 0.35f;
    *done = t >= dur;
    if (*done) return 0.0f;
    // Dull thud: a low body tone plus sub, both decaying quickly, with a bit of rattle
    // (amplitude-modulated noise) trailing off as the object settles.
    float env = expdecay(t, 0.06f);
    float thud = sinf(TWO_PI * 80.0f * t) * env;
    float sub = sinf(TWO_PI * 50.0f * t) * expdecay(t, 0.13f) * 0.5f;
    float rattleEnv = expdecay(t, 0.16f) * (0.5f + 0.5f * sinf(TWO_PI * 17.0f * t));
    float rattle = noise1(seed) * rattleEnv * 0.35f;
    return (thud * 0.75f + sub * 0.5f + rattle) * 0.75f;
}

static float synth_smash(float t, Uint32 *seed, bool *done) {
    const float dur = 0.6f;
    *done = t >= dur;
    if (*done) return 0.0f;
    // Bright shatter: a fast-decaying noise burst plus a handful of ringing high partials, sat
    // over a low crack (a shorter, lower noise burst plus a low thump). Kept dense so it still
    // reads clearly at the low gain the caller is expected to use for the "big" moment.
    float shatterEnv = expdecay(t, 0.16f);
    float partials = sinf(TWO_PI * 3200.0f * t) * expdecay(t, 0.08f) * 0.25f
                    + sinf(TWO_PI * 4700.0f * t) * expdecay(t, 0.06f) * 0.18f
                    + sinf(TWO_PI * 6100.0f * t) * expdecay(t, 0.045f) * 0.12f;
    float shatter = noise1(seed) * shatterEnv * 0.6f + partials;
    float crackEnv = expdecay(t, 0.025f);
    float crack = noise1(seed) * crackEnv * 0.7f + sinf(TWO_PI * 90.0f * t) * expdecay(t, 0.09f) * 0.5f;
    return (shatter * 0.6f + crack * 0.5f) * 0.5f;
}

static float synth_shot(float t, Uint32 *seed, bool *done) {
    const float dur = 0.25f;
    *done = t >= dur;
    if (*done) return 0.0f;
    // Pistol crack: a clipped noise burst kept to a few ms (this fires hundreds of times a run,
    // so the high end must not linger) over a fast-decaying low thump for the punch.
    float crackEnv = expdecay(t, 0.006f);
    float crack = clampf(noise1(seed) * 2.5f, -1.0f, 1.0f) * crackEnv;
    float thump = sinf(TWO_PI * 95.0f * t) * expdecay(t, 0.05f);
    float tail = noise1(seed) * expdecay(t, 0.08f) * 0.1f;
    return (crack * 0.6f + thump * 0.7f + tail) * 0.75f;
}

static float synth_boom(float t, Uint32 *seed, bool *done) {
    const float dur = 0.5f;
    *done = t >= dur;
    if (*done) return 0.0f;
    // SND_SHOT an octave lower and twice as long: more low-end body plus a rattling tail
    // (amplitude-modulated noise) as the pump shotgun's report dies away.
    float crackEnv = expdecay(t, 0.012f);
    float crack = clampf(noise1(seed) * 2.5f, -1.0f, 1.0f) * crackEnv;
    float thump = sinf(TWO_PI * 47.0f * t) * expdecay(t, 0.12f);
    float sub = sinf(TWO_PI * 32.0f * t) * expdecay(t, 0.2f) * 0.6f;
    float rattleEnv = expdecay(t, 0.3f) * (0.5f + 0.5f * sinf(TWO_PI * 24.0f * t));
    float rattle = noise1(seed) * rattleEnv * 0.3f;
    return (crack * 0.55f + thump * 0.8f + sub * 0.5f + rattle) * 0.75f;
}

static float synth_click(float t, Uint32 *seed, bool *done) {
    const float dur = 0.06f;
    *done = t >= dur;
    if (*done) return 0.0f;
    // Empty chamber: a single tiny filtered noise pop (averaging two consecutive noise samples,
    // same crude smoothing trick as SND_GRAB), almost no tone, kept quiet.
    float env = expdecay(t, 0.008f);
    float n1 = noise1(seed);
    float n2 = noise1(seed);
    float pop = (n1 + n2) * 0.5f * env;
    return pop * 0.35f;
}

static float synth_reload(float t, Uint32 *seed, bool *done) {
    const float dur = 1.0f;
    *done = t >= dur;
    if (*done) return 0.0f;
    // Two mechanical clunks (magazine out, magazine in) ~0.35 s apart, each a short filtered
    // noise burst with a low woody resonance, so it reads clearly as "something is being done"
    // under WEAP_RELOAD_TIME.
    float out = 0.0f;
    float t1 = t;
    if (t1 >= 0.0f && t1 < 0.15f) {
        float env = expdecay(t1, 0.03f);
        float n1 = noise1(seed);
        float n2 = noise1(seed);
        float clunk = (n1 + n2) * 0.5f * env;
        float wood = sinf(TWO_PI * 150.0f * t1) * expdecay(t1, 0.05f) * 0.5f;
        out += clunk * 0.6f + wood;
    }
    float t2 = t - 0.35f;
    if (t2 >= 0.0f && t2 < 0.15f) {
        float env = expdecay(t2, 0.03f);
        float n1 = noise1(seed);
        float n2 = noise1(seed);
        float clunk = (n1 + n2) * 0.5f * env;
        float wood = sinf(TWO_PI * 130.0f * t2) * expdecay(t2, 0.05f) * 0.5f;
        out += clunk * 0.6f + wood;
    }
    return out * 0.8f;
}

static float synth_whoosh(float t, Uint32 *seed, bool *done) {
    const float dur = 0.3f;
    *done = t >= dur;
    if (*done) return 0.0f;
    // A bat/wrench cutting the air: SND_SWING's shape (noise sweep, rises then falls) but
    // heavier -- a lower filter centre and a low body tone so it reads as mass, not a blade.
    float frac = t / dur;
    float cutoff = 0.05f + 0.3f * sinf((float)M_PI * frac);
    float n = noise1(seed);
    float env = sinf((float)M_PI * frac);
    float body = sinf(TWO_PI * 60.0f * t) * env * 0.35f;
    return (n * env * cutoff * 1.6f + body) * 0.9f;
}

static float synth_thud(float t, Uint32 *seed, bool *done) {
    const float dur = 0.4f;
    *done = t >= dur;
    if (*done) return 0.0f;
    // A goon hitting the ground: soft and dull, not violent -- no crack or snap, just a low body
    // impact plus a little cloth rustle as the pile settles.
    float env = expdecay(t, 0.09f);
    float body = sinf(TWO_PI * 65.0f * t) * env;
    float sub = sinf(TWO_PI * 42.0f * t) * expdecay(t, 0.16f) * 0.5f;
    float rustleEnv = expdecay(t, 0.2f) * 0.25f;
    float rustle = noise1(seed) * rustleEnv;
    return (body * 0.75f + sub * 0.5f + rustle) * 0.7f;
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
        case SND_WHIFF:    return synth_whiff(t, seed, done);
        case SND_FAIL:     return synth_fail(t, seed, done);
        case SND_GRAB:     return synth_grab(t, seed, done);
        case SND_DROP:     return synth_drop(t, seed, done);
        case SND_SMASH:    return synth_smash(t, seed, done);
        case SND_SHOT:     return synth_shot(t, seed, done);
        case SND_BOOM:     return synth_boom(t, seed, done);
        case SND_CLICK:    return synth_click(t, seed, done);
        case SND_RELOAD:   return synth_reload(t, seed, done);
        case SND_WHOOSH:   return synth_whoosh(t, seed, done);
        case SND_THUD:     return synth_thud(t, seed, done);
        default:           *done = true; return 0.0f;
    }
}

// ---------------------------------------------------------------- sound name lookup
//
// Lets data files and cutscenes reference a sound by name instead of the numeric SoundId. This
// table must stay in step with the SoundId enum in audio.h: one entry per id, in enum order.
static const char *SOUND_NAMES[SND_COUNT] = {
    "footstep", "swing", "hit", "parry", "hurt", "stagger", "roar", "death",
    "blip", "heart", "door", "sting", "whiff", "fail", "grab", "drop", "smash",
    "shot", "boom", "click", "reload", "whoosh", "thud",
};

int audio_sound_from_name(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < SND_COUNT; i++) {
        if (strcmp(SOUND_NAMES[i], name) == 0) return i;
    }
    return -1;
}

const char *audio_sound_name(int id) {
    if (id < 0 || id >= SND_COUNT) return "";
    return SOUND_NAMES[id];
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

    SampleVoice local_samples[NUM_SAMPLE_VOICES];
    memcpy(local_samples, g_audio.sample_voices, sizeof local_samples);

    // Music fade envelopes are linear and only stepped once per block (not per-sample) -- the
    // block is short enough (a few ms to ~85ms) that this reads as a smooth crossfade.
    MusicTrack *tracks[2] = { &g_audio.music_cur, &g_audio.music_prev };
    for (int m = 0; m < 2; m++) {
        MusicTrack *t = tracks[m];
        if (!t->active) continue;
        float step = t->fade_rate * block_dt;
        if (t->fade < t->fade_target) t->fade = fminf(t->fade + step, t->fade_target);
        else if (t->fade > t->fade_target) t->fade = fmaxf(t->fade - step, t->fade_target);
        if (t->fade_target <= 0.0001f && t->fade <= 0.0001f) t->active = false; // fully faded out
    }
    float music_gain = g_audio.music_gain;

    for (int i = 0; i < frames; i++) {
        float mono = 0.0f;

        for (int v = 0; v < NUM_VOICES; v++) {
            if (!local[v].active) continue;
            bool done = false;
            float s = synth_sound(local[v].id, local[v].t, &local[v].seed, &done);
            if (done) { local[v].active = false; continue; }
            mono += s * local[v].gain;
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

        mono += drone + fight;
        float mixL = mono, mixR = mono;

        // Sample voices: linear-interpolated so `pitch` can retune playback speed.
        for (int sv = 0; sv < NUM_SAMPLE_VOICES; sv++) {
            SampleVoice *voice = &local_samples[sv];
            if (!voice->active) continue;
            int id = voice->sample_id;
            if (id < 0 || id >= g_sample_count) { voice->active = false; continue; }
            SampleData *sd = &g_samples[id];
            int i0 = (int)voice->pos;
            if (sd->frame_count < 2 || i0 >= sd->frame_count - 1) { voice->active = false; continue; }
            int i1 = i0 + 1;
            float frac = voice->pos - (float)i0;
            float l = sd->frames[i0 * 2 + 0] + (sd->frames[i1 * 2 + 0] - sd->frames[i0 * 2 + 0]) * frac;
            float r = sd->frames[i0 * 2 + 1] + (sd->frames[i1 * 2 + 1] - sd->frames[i0 * 2 + 1]) * frac;
            mixL += l * voice->gain;
            mixR += r * voice->gain;
            voice->pos += voice->pitch;
        }

        // Music: the current track (playing / fading in) plus, mid-crossfade, the previous
        // track fading out. Both always play at their decoded (native 48kHz) rate.
        for (int m = 0; m < 2; m++) {
            MusicTrack *t = tracks[m];
            if (!t->active || !t->frames || t->frame_count <= 0) continue;
            int idx = t->pos;
            if (idx >= t->frame_count) {
                if (t->loop) { idx %= t->frame_count; t->pos = idx; }
                else { t->active = false; continue; }
            }
            float g = t->fade * t->gain * music_gain;
            mixL += t->frames[idx * 2 + 0] * g;
            mixR += t->frames[idx * 2 + 1] * g;
            t->pos = idx + 1;
            if (t->loop && t->pos >= t->frame_count) t->pos = 0;
        }

        mixL *= master;
        mixR *= master;
        mixL = tanhf(mixL);
        mixR = tanhf(mixR);
        if (!isfinite(mixL)) mixL = 0.0f;
        if (!isfinite(mixR)) mixR = 0.0f;

        out[i * 2 + 0] = mixL;
        out[i * 2 + 1] = mixR;

        g_audio.drone_phase += 1.0 / (double)SAMPLE_RATE;
    }

    memcpy(g_audio.voices, local, sizeof local);
    memcpy(g_audio.sample_voices, local_samples, sizeof local_samples);
    SDL_UnlockMutex(g_audio.mutex);
}

// --- voice ---
// The voice chat bus. Set once from the main thread at startup and cleared at shutdown, read on
// the audio thread; a torn read is not possible for a pointer-sized store on any platform we
// ship, and the worst case is one block of missing or extra voice.
static AudioVoicePull g_voice_pull = NULL;
static void *g_voice_user = NULL;

void audio_set_voice_source(AudioVoicePull fn, void *user) { g_voice_user = user; g_voice_pull = fn; }

// Added after the master volume and after the game mix's tanh, then hard-clipped: voice must stay
// intelligible when the game is loud, and must still be there when the game is silent.
static void voice_bus_mix(float *buf, int frames) {
    AudioVoicePull fn = g_voice_pull;
    if (!fn) return;
    static float vbuf[4096 * 2];
    if (frames > 4096) frames = 4096;
    memset(vbuf, 0, (size_t)frames * 2 * sizeof(float));
    fn(vbuf, frames, g_voice_user);
    for (int i = 0; i < frames * 2; i++) {
        float v = buf[i] + vbuf[i];
        buf[i] = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
    }
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
        voice_bus_mix(buf, chunk);   // --- voice ---
        SDL_PutAudioStreamData(stream, buf, chunk * (int)(sizeof(float) * 2));
        frames -= chunk;
    }
}

#ifdef AUDIO_TEST
// Test-only introspection, compiled out of normal builds.
static int g_debug_last_source_rate = 0;
int audio_debug_last_source_rate(void) { return g_debug_last_source_rate; }
int audio_debug_sample_frame_count(int id) {
    return (id >= 0 && id < g_sample_count) ? g_samples[id].frame_count : -1;
}
bool audio_debug_music_active(void) { return g_audio_ok && g_audio.music_cur.active; }
int audio_debug_music_frame_count(void) { return g_audio.music_cur.frame_count; }
int audio_debug_music_pos(void) { return g_audio.music_cur.pos; }
float audio_debug_sample_voice_pos(int slot) {
    return (slot >= 0 && slot < NUM_SAMPLE_VOICES) ? g_audio.sample_voices[slot].pos : -1.0f;
}
int audio_debug_active_sample_voice_count(void) {
    if (!g_audio_ok || !g_audio.mutex) return 0;
    int n = 0;
    SDL_LockMutex(g_audio.mutex);
    for (int i = 0; i < NUM_SAMPLE_VOICES; i++) if (g_audio.sample_voices[i].active) n++;
    SDL_UnlockMutex(g_audio.mutex);
    return n;
}
#endif

// -------------------------------------------------------- asset decoding

// Decodes a WAV or OGG file (by extension) to malloc'd interleaved stereo float at SAMPLE_RATE.
// Runs on the main thread; may allocate freely. Returns false (and leaves *out_frames NULL) on
// any failure.
static bool decode_audio_file(const char *path, float **out_frames, int *out_frame_count) {
    *out_frames = NULL;
    *out_frame_count = 0;

    size_t len = strlen(path);
    bool is_ogg = (len >= 4 && SDL_strcasecmp(path + len - 4, ".ogg") == 0);

    if (is_ogg) {
        int channels = 0, rate = 0;
        short *pcm = NULL;
        int in_frames = stb_vorbis_decode_filename(path, &channels, &rate, &pcm);
        if (in_frames <= 0 || !pcm || channels <= 0) {
            if (pcm) free(pcm);
            return false;
        }
#ifdef AUDIO_TEST
        g_debug_last_source_rate = rate;
#endif

        float *stereo = (float *)malloc(sizeof(float) * 2 * (size_t)in_frames);
        if (!stereo) { free(pcm); return false; }
        for (int i = 0; i < in_frames; i++) {
            float l, r;
            if (channels >= 2) {
                l = pcm[i * channels + 0] / 32768.0f;
                r = pcm[i * channels + 1] / 32768.0f;
            } else {
                l = r = pcm[i * channels + 0] / 32768.0f;
            }
            stereo[i * 2 + 0] = l;
            stereo[i * 2 + 1] = r;
        }
        free(pcm);

        if (rate == SAMPLE_RATE) {
            *out_frames = stereo;
            *out_frame_count = in_frames;
            return true;
        }

        // Linear-interpolation resample to SAMPLE_RATE.
        double ratio = (double)rate / (double)SAMPLE_RATE;
        int resampled_frames = (int)((double)in_frames / ratio);
        if (resampled_frames < 1) resampled_frames = 1;
        float *resampled = (float *)malloc(sizeof(float) * 2 * (size_t)resampled_frames);
        if (!resampled) { free(stereo); return false; }
        for (int i = 0; i < resampled_frames; i++) {
            double srcpos = (double)i * ratio;
            int i0 = (int)srcpos;
            if (i0 >= in_frames) i0 = in_frames - 1;
            int i1 = i0 + 1 < in_frames ? i0 + 1 : i0;
            float frac = (float)(srcpos - (double)i0);
            resampled[i * 2 + 0] = stereo[i0 * 2 + 0] + (stereo[i1 * 2 + 0] - stereo[i0 * 2 + 0]) * frac;
            resampled[i * 2 + 1] = stereo[i0 * 2 + 1] + (stereo[i1 * 2 + 1] - stereo[i0 * 2 + 1]) * frac;
        }
        free(stereo);
        *out_frames = resampled;
        *out_frame_count = resampled_frames;
        return true;
    }

    // WAV: let SDL do the format/rate/channel conversion for us.
    SDL_AudioSpec src_spec;
    Uint8 *buf = NULL;
    Uint32 buf_len = 0;
    if (!SDL_LoadWAV(path, &src_spec, &buf, &buf_len)) return false;
#ifdef AUDIO_TEST
    g_debug_last_source_rate = src_spec.freq;
#endif

    SDL_AudioSpec dst_spec = { .format = SDL_AUDIO_F32, .channels = 2, .freq = SAMPLE_RATE };
    Uint8 *dst_data = NULL;
    int dst_len = 0;
    bool ok = SDL_ConvertAudioSamples(&src_spec, buf, (int)buf_len, &dst_spec, &dst_data, &dst_len);
    SDL_free(buf);
    if (!ok || !dst_data || dst_len <= 0) {
        if (dst_data) SDL_free(dst_data);
        return false;
    }

    int frame_count = dst_len / (int)(sizeof(float) * 2);
    float *frames = (float *)malloc(sizeof(float) * 2 * (size_t)frame_count);
    if (!frames) { SDL_free(dst_data); return false; }
    memcpy(frames, dst_data, sizeof(float) * 2 * (size_t)frame_count);
    SDL_free(dst_data);

    *out_frames = frames;
    *out_frame_count = frame_count;
    return true;
}

// Frees a music track's decoded buffer if it's sitting there stopped (never called from the
// audio thread -- only from the main-thread API below, under the lock).
static void music_track_release(MusicTrack *t) {
    if (t->frames) { free(t->frames); t->frames = NULL; }
    memset(t, 0, sizeof *t);
}

// ---------------------------------------------------------------- public api

bool audio_init(void) {
    memset(&g_audio, 0, sizeof g_audio);
    g_audio.master = 1.0f;
    g_audio.music_gain = 1.0f;
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

    // The stream (and thus the mixer callback) is torn down above, so it's safe to free the
    // decoded asset/music buffers here without the lock.
    for (int i = 0; i < g_sample_count; i++) {
        free(g_samples[i].frames);
        g_samples[i].frames = NULL;
    }
    g_sample_count = 0;
    music_track_release(&g_audio.music_cur);
    music_track_release(&g_audio.music_prev);
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

// ------------------------------------------------------- sample playback api

int audio_load(const char *path) {
    if (!g_audio_ok || !path) return -1;

    for (int i = 0; i < g_sample_count; i++) {
        if (strcmp(g_samples[i].path, path) == 0) return i;
    }
    if (g_sample_count >= MAX_SAMPLES) return -1;

    float *frames = NULL;
    int frame_count = 0;
    if (!decode_audio_file(path, &frames, &frame_count)) return -1;

    int id = g_sample_count;
    SampleData *s = &g_samples[id];
    snprintf(s->path, sizeof s->path, "%s", path);
    s->frames = frames;
    s->frame_count = frame_count;
    g_sample_count = id + 1; // published last: audio_play_sample()'s lock/unlock is the barrier

    return id;
}

void audio_play_sample(int id, float gain, float pitch) {
    if (!g_audio_ok || !g_audio.mutex) return;
    if (id < 0 || id >= g_sample_count) return;
    if (pitch <= 0.0001f) pitch = 1.0f;

    SDL_LockMutex(g_audio.mutex);

    int slot = -1;
    for (int v = 0; v < NUM_SAMPLE_VOICES; v++) {
        if (!g_audio.sample_voices[v].active) { slot = v; break; }
    }
    if (slot < 0) {
        // steal the oldest voice
        Uint64 oldest_age = g_audio.sample_voices[0].age;
        slot = 0;
        for (int v = 1; v < NUM_SAMPLE_VOICES; v++) {
            if (g_audio.sample_voices[v].age < oldest_age) { oldest_age = g_audio.sample_voices[v].age; slot = v; }
        }
    }

    SampleVoice *voice = &g_audio.sample_voices[slot];
    voice->active = true;
    voice->sample_id = id;
    voice->pos = 0.0f;
    voice->pitch = pitch;
    voice->gain = clampf(gain, 0.0f, 4.0f);
    voice->age = g_audio.next_age++;

    SDL_UnlockMutex(g_audio.mutex);
}

void audio_play_file(const char *path, float gain, float pitch) {
    int id = audio_load(path);
    if (id < 0) return;
    audio_play_sample(id, gain, pitch);
}

// ------------------------------------------------------------------ music api

void audio_music_play(const char *path, bool loop, float gain, float fade_in_s) {
    if (!g_audio_ok || !g_audio.mutex || !path) return;

    SDL_LockMutex(g_audio.mutex);
    bool already_current = g_audio.music_cur.active && g_audio.music_cur.frames &&
                            g_audio.music_cur.fade_target > 0.5f &&
                            strcmp(g_audio.music_cur.path, path) == 0;
    SDL_UnlockMutex(g_audio.mutex);
    if (already_current) return; // same track already playing (or fading in): do nothing

    float *frames = NULL;
    int frame_count = 0;
    if (!decode_audio_file(path, &frames, &frame_count)) return;

    float rate = fade_in_s > 0.0001f ? (1.0f / fade_in_s) : 1000.0f; // effectively instant

    SDL_LockMutex(g_audio.mutex);

    // Make room in the "previous" slot: if it's still holding a fully-stopped track, free it;
    // if it's mid-fade (a crossfade was already in flight), drop it in favour of this new one.
    if (g_audio.music_prev.frames) music_track_release(&g_audio.music_prev);

    if (g_audio.music_cur.active && g_audio.music_cur.frames) {
        g_audio.music_prev = g_audio.music_cur;
        g_audio.music_prev.fade_target = 0.0f;
        g_audio.music_prev.fade_rate = rate;
    } else if (g_audio.music_cur.frames) {
        music_track_release(&g_audio.music_cur);
    }

    memset(&g_audio.music_cur, 0, sizeof g_audio.music_cur);
    g_audio.music_cur.frames = frames;
    g_audio.music_cur.frame_count = frame_count;
    g_audio.music_cur.loop = loop;
    g_audio.music_cur.active = true;
    g_audio.music_cur.pos = 0;
    g_audio.music_cur.gain = clampf(gain, 0.0f, 4.0f);
    g_audio.music_cur.fade = (fade_in_s > 0.0001f) ? 0.0f : 1.0f;
    g_audio.music_cur.fade_target = 1.0f;
    g_audio.music_cur.fade_rate = rate;
    snprintf(g_audio.music_cur.path, sizeof g_audio.music_cur.path, "%s", path);

    SDL_UnlockMutex(g_audio.mutex);
}

void audio_music_stop(float fade_out_s) {
    if (!g_audio_ok || !g_audio.mutex) return;
    SDL_LockMutex(g_audio.mutex);
    if (g_audio.music_cur.active) {
        g_audio.music_cur.fade_target = 0.0f;
        g_audio.music_cur.fade_rate = fade_out_s > 0.0001f ? (1.0f / fade_out_s) : 1000.0f;
    }
    if (g_audio.music_prev.active) {
        g_audio.music_prev.fade_target = 0.0f;
        g_audio.music_prev.fade_rate = fade_out_s > 0.0001f ? (1.0f / fade_out_s) : 1000.0f;
    }
    SDL_UnlockMutex(g_audio.mutex);
}

void audio_music_set_gain(float gain) {
    if (!g_audio_ok || !g_audio.mutex) return;
    SDL_LockMutex(g_audio.mutex);
    g_audio.music_gain = clampf(gain, 0.0f, 4.0f);
    SDL_UnlockMutex(g_audio.mutex);
}
