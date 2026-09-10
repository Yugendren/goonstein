// Per-speaker jitter buffer + Opus decoder for proximity voice chat.
//
// One of these per remote speaker. Packets arrive over UDP out of order, duplicated and lossy;
// this reorders them into a small queue, holds VJ_SLOTS * VOICE_FRAME_MS ms of reordering room,
// and hands the mixer exactly one 20 ms frame of mono float per pull, forever. A short lost
// packet is recovered with Opus in-band FEC carried in the *next* packet; if that is not
// available either, Opus packet-loss concealment fills the gap for a few frames before the
// buffer gives up and falls back to silence.
#pragma once
#include "voice.h"
#include <stdbool.h>
#include <stdint.h>

#define VJ_SLOTS 12          // packets held per speaker: 240 ms of reordering room

// What a pull produced, for the stats line.
typedef enum VjResult { VJ_SILENT = 0, VJ_DECODED = 1, VJ_FEC = 2, VJ_CONCEALED = 3 } VjResult;

typedef struct VoiceJitter {
    void *dec;                        // OpusDecoder*, opaque here so callers need no opus header
    struct {
        bool used; uint16_t seq, t_ms; uint8_t flags; int len; uint8_t data[VOICE_MAX_PACKET];
    } q[VJ_SLOTS];
    bool     started;                 // true once enough packets are buffered to begin playing
    uint16_t next_seq;                // the sequence number the next pull wants
    int      target_frames;           // frames buffered before playback starts
    int      conceal_run;             // consecutive VJ_CONCEALED frames since the last real one;
                                       // capped at 3 before pull falls back to silence
    uint64_t last_warn_ms;            // SDL_GetTicks() of the last opus_decode_float error log,
                                       // so a broken stream logs at most once a second
    // counters, cumulative; the caller may zero them for a per-second stats line
    uint32_t n_pushed, n_dup, n_late, n_overflow, n_decoded, n_fec, n_concealed, n_silent;
    uint16_t last_t_ms;               // capture timestamp of the last frame handed out
    uint8_t  last_flags;
} VoiceJitter;

// target_ms is clamped to [40, 200]; 80 is the game's default.
bool voice_jitter_init(VoiceJitter *j, int target_ms);
void voice_jitter_free(VoiceJitter *j);
void voice_jitter_reset(VoiceJitter *j);      // keeps the decoder, drops everything queued
// Store one received packet. Duplicates and anything already played past are dropped and counted.
void voice_jitter_push(VoiceJitter *j, uint16_t seq, uint16_t t_ms, uint8_t flags,
                       const uint8_t *data, int len);
// Produce exactly VOICE_FRAME mono samples. Always writes the whole frame (silence when there is
// nothing to play). *out_t_ms gets the capture timestamp the frame came from (unchanged on
// silence) and *out_flags the sender's flag byte; either pointer may be NULL.
VjResult voice_jitter_pull(VoiceJitter *j, float *out, uint16_t *out_t_ms, uint8_t *out_flags);
// How many frames are queued ahead of the play cursor right now (for the latency readout).
int  voice_jitter_depth(const VoiceJitter *j);
