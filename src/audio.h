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
