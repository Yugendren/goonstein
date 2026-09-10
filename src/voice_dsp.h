#pragma once
#include <stdbool.h>

// Per-character voice changer: runs on the SENDER, on the raw 48 kHz mono microphone signal, in
// 20 ms frames of exactly VDSP_FRAME samples, before Opus encoding. See voice_dsp.c for the
// algorithms and the reasoning behind them; this header is only the public contract.

#define VDSP_FRAME 960          // 20 ms at 48 kHz; the only block size process() accepts

typedef enum VoiceEffect { VFX_NONE = 0, VFX_RADIO = 1, VFX_RING = 2, VFX_COUNT } VoiceEffect;

int         voice_effect_from_name(const char *s);   // "none"/"radio"/"ring"; -1 unknown
const char *voice_effect_name(int e);                // "none" when out of range

// Fixed capacities for the ring buffers below. All state lives inside VoiceDsp as plain arrays
// (no malloc anywhere); these are sized generously relative to the clamped pitch/formant ranges
// so the rings never need to catch up faster than real time. See voice_dsp.c for the derivation.
#define VDSP_PS_GRAIN    960     // pitch stage: WSOLA grain length (== VDSP_FRAME)
#define VDSP_PS_IN_CAP  4096     // pitch stage: raw input ring
#define VDSP_PS_S1_CAP  4096     // pitch stage: time-scaled (WSOLA overlap-add) ring
#define VDSP_FM_WIN      512     // formant stage: analysis/synthesis window length
#define VDSP_FM_IN_CAP  2048     // formant stage: input ring (post pitch-shift)
#define VDSP_FM_OUT_CAP 2048     // formant stage: overlap-add output ring

// A single RBJ biquad in transposed Direct Form II, used by VFX_RADIO's high-pass/low-pass pair.
typedef struct Biquad2 { float b0, b1, b2, a1, a2, z1, z2; } Biquad2;

typedef struct VoiceDsp {
    /* configuration */
    float pitch, formant; int effect;
    bool  bypass;                 /* true when pitch==1, formant==1, effect==none: process() is a no-op */

    /* ---- pitch stage: WSOLA time-scale (grains of VDSP_PS_GRAIN, 50% overlap) followed by a
       fractional-cursor resample by `pitch`. Runs whenever !bypass, even if pitch == 1 (in that
       case it degenerates to an identity pass plus the stage's fixed latency). */
    float    ps_hann[VDSP_PS_GRAIN];      // precomputed Hann analysis/synthesis window
    float    ps_in[VDSP_PS_IN_CAP];       // raw input ring, fed VDSP_FRAME samples per call
    unsigned long long ps_in_write;       // samples ever appended to ps_in
    unsigned long long ps_ideal_next;     // next WSOLA grain's un-refined read position
    int      ps_hop_in;                   // this instance's WSOLA input hop, from `pitch`
    int      ps_grain_idx;                // WSOLA grain counter (0, 1, 2, ...)
    float    ps_s1[VDSP_PS_S1_CAP];       // WSOLA overlap-add accumulator (the time-scaled stream)
    unsigned long long ps_s1_write;       // samples of ps_s1 finalized and safe to read
    double   ps_s2_read;                  // stage-2 fractional read cursor into ps_s1

    /* ---- formant stage: grain-resample approximation (see voice_dsp.c). Entirely skipped,
       including its latency, when formant == 1.0. */
    bool     formant_active;
    float    fm_hann[VDSP_FM_WIN];        // precomputed Hann window
    float    fm_in[VDSP_FM_IN_CAP];       // pitch-stage output, fed VDSP_FRAME samples per call
    unsigned long long fm_in_write;       // samples ever appended to fm_in
    int      fm_win_idx;                  // formant window counter (0, 1, 2, ...)
    float    fm_out[VDSP_FM_OUT_CAP];     // overlap-add accumulator (the formant-shifted stream)
    unsigned long long fm_out_write;      // samples of fm_out finalized and safe to read
    unsigned long long fm_out_read;       // samples of fm_out already consumed by process()

    /* ---- effect stage, applied last, in place */
    Biquad2      radio_hp, radio_lp;      // VFX_RADIO: 300 Hz HPF -> 3000 Hz LPF -> soft clip -> hiss
    unsigned int radio_seed;              // xorshift32 state for the hiss, seeded from a constant
    float        ring_phase;              // VFX_RING: continuous 55 Hz modulator phase, radians
} VoiceDsp;

void voice_dsp_init(VoiceDsp *d, float pitch, float formant, int effect);
/* Process exactly n == VDSP_FRAME mono samples in place. Streaming: internal latency is constant
   and reported by voice_dsp_latency(). Never allocates, never blocks. */
void voice_dsp_process(VoiceDsp *d, float *frame, int n);
/* Constant algorithmic delay in samples that process() adds, for the latency budget. */
int  voice_dsp_latency(const VoiceDsp *d);
/* RMS of a frame, used by the open-mic energy gate and the HUD level meter. */
float voice_dsp_rms(const float *frame, int n);
