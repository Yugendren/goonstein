// WAV in and out for the voice test hooks (see src/voice.h). Loading goes through SDL so any
// sample rate / format / channel count is accepted and converted; writing is a plain 16-bit PCM
// RIFF file written incrementally, with the two size fields patched on close.
#pragma once
#include <stdbool.h>
#include <stdio.h>

// Load a WAV and convert it to mono float at 48 kHz. Returns a malloc'd buffer the caller frees
// with free(), or NULL (with a reason logged through SDL_LogWarn). *out_frames gets the sample
// count.
float *voice_wav_load(const char *path, int *out_frames);

// A 16-bit PCM WAV being written a block at a time.
typedef struct VoiceWavOut {
    FILE *f;
    int channels, rate;
    unsigned frames;     // written so far
    bool open;
    char path[512];
} VoiceWavOut;

// Creates the file and writes a 44-byte header with placeholder sizes. False on failure.
bool voice_wav_open(VoiceWavOut *w, const char *path, int channels, int rate);
// Append `frames` frames of interleaved float, clamped to [-1,1] and converted to int16.
// No-op when the writer is not open, so callers need no null checks. Not thread-safe: call from
// a single thread only, same as the rest of VoiceWavOut's lifecycle.
void voice_wav_write(VoiceWavOut *w, const float *interleaved, int frames);
// Patch RIFF size and data size, close. Safe to call on a writer that was never opened.
void voice_wav_close(VoiceWavOut *w);
