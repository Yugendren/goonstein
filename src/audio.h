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
    SND_COUNT
} SoundId;

bool audio_init(void);
void audio_shutdown(void);
// Fire-and-forget. gain 0..1, pitch multiplier (1 = normal). Thread-safe with the mixer.
void audio_play(SoundId id, float gain, float pitch);
// Ambient drone intensity 0..1 (0 = silent). Smoothly followed by the mixer.
void audio_set_drone(float intensity);
// Fight layer 0..1: adds a slow pulse / low throb under the drone.
void audio_set_fight(float intensity);
// Master volume 0..1
void audio_set_master(float v);

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
