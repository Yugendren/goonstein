// Audio: procedural sound effects and an ambient drone, mixed in an SDL3 audio stream.
// No asset files. Every sound is synthesised so the skeleton runs anywhere.
#pragma once
#include <stdbool.h>

typedef enum SoundId {
    SND_FOOTSTEP,   // short low thud with a little noise
    SND_SWING,      // whoosh (noise sweep)
    SND_HIT,        // meaty impact
    SND_PARRY,      // bright metallic ping, the most satisfying sound in the game
    SND_HURT,       // player takes damage: dull crack plus low tone
    SND_STAGGER,    // boss posture breaks: deep crunch with a ring
    SND_ROAR,       // boss roar: pitched noise with growl modulation, ~1 s
    SND_DEATH,      // low descending tone, ~1.5 s
    SND_BLIP,       // ui / subtitle tick
    SND_HEART,      // single heartbeat thump
    SND_DOOR,       // heavy door, ~1 s
    SND_STING,      // horror sting: dissonant swell, ~1.5 s
    SND_WHIFF,      // parry raised but nothing came: soft leather creak / air, ~0.2 s
    SND_FAIL,       // parry failed: dull low clunk, clearly not the ping, ~0.3 s
    SND_GRAB,       // hands close on an object: a short cloth/wood scuff, ~0.12 s, soft
    SND_DROP,       // something heavy set down or dropped: a dull thud with a little rattle, ~0.35 s
    SND_SMASH,      // a fragile thing breaks: bright shatter over a low crack, ~0.6 s, the one
                     // the player remembers -- reads clearly even at a low gain
    SND_SHOT,       // pistol crack: short bright noise burst over a fast low thump, ~0.25 s --
                     // fired hundreds of times a run, so the high end is kept short to not fatigue
    SND_BOOM,       // pump shotgun: SND_SHOT an octave lower and twice as long, ~0.5 s, with more
                     // low-end body and a rattling (amplitude-modulated) tail
    SND_CLICK,      // empty chamber: dry mechanical tick, ~0.06 s, a single tiny filtered noise
                     // pop with almost no tone, kept quiet
    SND_RELOAD,     // reload: two mechanical clunks ~0.35 s apart (mag out, mag in) inside ~1.0 s,
                     // each a filtered noise burst with a low woody resonance
    SND_WHOOSH,     // a bat/wrench swing through the air: noise sweep that rises then falls,
                     // ~0.3 s -- SND_SWING's shape but heavier, with a lower centre and body tone
    SND_THUD,       // a goon hitting the ground: soft, dull, low-frequency body impact with a
                     // little cloth rustle, ~0.4 s -- comedic, not violent, no crack or snap
    SND_COUNT
} SoundId;

bool audio_init(void);
void audio_shutdown(void);
// Fire-and-forget. gain 0..1, pitch multiplier (1 = normal). Thread-safe with the mixer.
void audio_play(SoundId id, float gain, float pitch);
// Sound ids by name ("parry", "smash", ...). -1 when the name is unknown.
int         audio_sound_from_name(const char *name);
const char *audio_sound_name(int id);   // "" when the id is out of range
// Ambient drone intensity 0..1 (0 = silent). Smoothly followed by the mixer.
void audio_set_drone(float intensity);
// Fight layer 0..1: adds a slow pulse / low throb under the drone.
void audio_set_fight(float intensity);
// Master volume 0..1
void audio_set_master(float v);

// --- voice ---
// Proximity voice chat bus. The mixer calls this once per callback block, on the audio thread,
// *after* the game mix has been scaled by the master volume, so voice is audible with the game
// muted (`--volume 0`). The callback must FILL `out` with `frames` interleaved stereo frames
// (it is handed a zeroed buffer) and must not block: it owns its own lock, and the mixer's lock
// is not held while it runs. Pass NULL to detach. See src/voice.c.
typedef void (*AudioVoicePull)(float *out_stereo, int frames, void *user);
void audio_set_voice_source(AudioVoicePull fn, void *user);

// Sample playback (WAV via SDL_LoadWAV, OGG via stb_vorbis). Files are decoded fully at load and
// cached by path; ids are stable for the run. Returns -1 on failure.
int  audio_load(const char *path);
// Fire-and-forget playback of a loaded sample. gain 0..1, pitch is a playback-rate multiplier.
void audio_play_sample(int id, float gain, float pitch);
// Convenience: load (cached) and play in one call.
void audio_play_file(const char *path, float gain, float pitch);
// Music: one track at a time, decoded fully at load (OGG), looping, with a crossfade to the next.
void audio_music_play(const char *path, bool loop, float gain, float fade_in_s);
void audio_music_stop(float fade_out_s);
void audio_music_set_gain(float gain);
