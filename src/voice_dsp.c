// Per-character voice changer for proximity voice chat. Pure C11 DSP, no SDL, no Opus, no malloc
// -- every piece of state is a fixed-size array inside VoiceDsp, sized for the clamped parameter
// ranges (pitch in [0.5, 2.0], formant in [0.7, 1.5]) so the rings never need to run faster than
// real time to keep up.
//
// Pipeline, per 20 ms / VDSP_FRAME(960)-sample call: pitch shift -> formant shift (optional) ->
// effect (optional) -> clamp to [-1, 1].
//
// ---------------------------------------------------------------------- 1. pitch shift
// Two stages, streaming, each with its own ring buffer so 960 samples in always yields 960
// samples out:
//
//   a) WSOLA time-scale by a factor of `pitch`. Fixed *synthesis* hop PS_HOP (480, half of the
//      960-sample grain, so a plain Hann window OLA's to a constant -- see the COLA note below).
//      The *analysis* hop (how far the read pointer advances through the input per grain) is
//      PS_HOP / pitch: for pitch > 1 that is a SMALLER hop than the synthesis hop, so the
//      algorithm re-reads more of the input per output sample and the time-scaled stream comes
//      out `pitch` times LONGER than the input (same pitch, slower). For pitch < 1 the analysis
//      hop is larger, so the stream comes out `pitch` times SHORTER (same pitch, faster).
//      (Note: this is the opposite direction from the naive "input hop = hop * pitch" formula --
//      that formula makes the *output/input* duration ratio 1/pitch, which is the wrong sign to
//      cancel out in stage (b) below. Using PS_HOP/pitch as the analysis hop makes the
//      output/input ratio equal to `pitch`, which is what stage (b) needs to undo. This is
//      flagged here because it is the one place this file's behaviour diverges from a literal
//      reading of "input hop = round(480 * pitch)"; the formula used is the one that actually
//      produces the requested pitch shift.)
//      Each grain's read position is `analysis_hop` past the previous one, refined by a
//      normalised cross-correlation search over +/- PS_SEARCH samples against the samples that
//      are already sitting in the accumulator where this grain's first half will land (i.e. the
//      second half of the previous grain) -- the classic WSOLA alignment step, so the Hann
//      crossfade doesn't create phase-cancellation artefacts. Grains are windowed with a
//      (periodic) Hann window and overlap-added at the fixed 480-sample hop; a periodic Hann at
//      50% overlap sums to an exact constant (w[n] + w[n + N/2] == 1 for all n), so this is a
//      clean COLA reconstruction with no extra gain compensation needed.
//   b) Resample the time-scaled stream by `pitch`: a fractional read cursor advances by `pitch`
//      per output sample (linear interpolation between the two neighbouring samples). Reading
//      faster (pitch > 1) both raises every frequency by `pitch` and shrinks the duration by
//      1/pitch, which exactly cancels stage (a)'s stretch -- net: pitch multiplied by `pitch`,
//      duration unchanged, 960 in -> 960 out.
//
// Both stages are implemented against ring buffers addressed by monotonically increasing sample
// counters (never reset, indexed into the physical array with %). The very first PS_LATENCY
// samples of the time-scaled ring are *not* written by any grain; ps_s1_write starts at
// PS_LATENCY rather than 0, so reads of those positions fall through to the "not yet finalized"
// zero-fill path in ring_read_lerp(). That silence IS the pitch stage's reported latency: it is
// exactly the amount of raw input (one grain plus the search radius) that has to have arrived
// before the very first WSOLA grain can be placed.
//
// ------------------------------------------------------------------- 2. formant shift (cheap)
// A second, independent grain-resample stage, applied to the pitch-shifted stream. Hann windows
// of VDSP_FM_WIN(512) samples at hop FM_HOP(256) -- again 50% overlap, again exact COLA. Window k
// is centred at sample k*FM_HOP + FM_WIN/2 of the pitch-shifted stream; sample j of the window
// reads that stream at `centre + (j - FM_WIN/2) * formant` (linearly interpolated). Reading at
// step `formant` scales every frequency inside the grain by `formant` (formant > 1 brightens /
// raises the vocal tract, < 1 enlarges it), while the *unchanged* OLA hop keeps the overall
// duration the same and largely re-imposes the original grain rate. This is a grain-resample
// approximation of a true formant shift, not an LPC/cepstral envelope warp -- over the presets'
// +/-15-30% range it reads as a body-size change with no audible artefacts, and it costs one
// multiply-add per output sample. When formant is within 1e-3 of 1.0 the whole stage (and its
// latency) is skipped.
//
// -------------------------------------------------------------------------------- 3. effects
//   VFX_NONE : nothing.
//   VFX_RADIO: 2nd-order Butterworth (RBJ biquad, Q = 1/sqrt(2)) high-pass at 300 Hz, then the
//              same shape low-pass at 3000 Hz, then a soft clip tanhf(x*2.5)*0.6, then +6% white
//              noise scaled by *this frame's* RMS (via voice_dsp_rms) so the hiss only exists
//              while someone is actually talking. The noise generator is a tiny xorshift32 seeded
//              from the fixed constant 0x9E3779B9 (not from the instance's address), so every
//              radio-preset instance produces the same hiss sequence.
//   VFX_RING : ring modulation with a 55 Hz sine at 60% depth: x * (0.4 + 0.6*sin(phase)), phase
//              advanced per sample at 2*pi*55/48000 and wrapped, continuous across frames.
//
// Every filter's recursive state gets a TINY_DC (1e-20f) offset added each sample, matching the
// denormal guard already used in audio.c's one-pole filters. The final output is hard-clamped to
// [-1, 1] regardless of which stages ran.
//
// -------------------------------------------------------------------------- streaming & safety
// process() always consumes exactly VDSP_FRAME samples and always emits exactly VDSP_FRAME
// samples, drawing from/pushing into the rings described above. If a ring's write pointer ever
// hasn't caught up to where a stage wants to read (should not happen in steady state given the
// ring capacities below and pitch/formant's clamped ranges) the read helper returns 0.0f instead
// of touching unwritten/stale memory.

#include "voice_dsp.h"
#include <math.h>
#include <string.h>

#define VDSP_PI   3.14159265358979323846f
#define VDSP_RATE 48000.0f
#define TINY_DC   1e-20f // keeps recursive filter state out of denormal land, same trick as audio.c

#define PS_HOP     (VDSP_PS_GRAIN / 2)        // 480: WSOLA synthesis hop (50% overlap)
#define PS_SEARCH  128                        // WSOLA alignment search radius, samples
#define PS_LATENCY (VDSP_PS_GRAIN + PS_SEARCH) // 1088: raw input needed before the first grain

#define FM_HOP     (VDSP_FM_WIN / 2)          // 256: formant synthesis hop (50% overlap)
#define FM_LATENCY (VDSP_FM_WIN / 2)          // 256: one half-window of formant-stage priming

// ---------------------------------------------------------------- small helpers

static float clampf_local(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static unsigned int xorshift32(unsigned int *state) {
    unsigned int x = *state ? *state : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

// white noise in [-1, 1]
static float noise1(unsigned int *state) {
    unsigned int r = xorshift32(state);
    return (float)((double)r / (double)0xFFFFFFFFu) * 2.0f - 1.0f;
}

// Read a ring buffer at an absolute (monotonically increasing) sample position. The caller is
// responsible for only calling this with a position it knows has already been written.
static float ring_read1(const float *ring, int cap, unsigned long long pos) {
    return ring[(size_t)(pos % (unsigned long long)cap)];
}

// Linear-interpolated read at a fractional absolute position, with "not written yet" (either
// before the stream started or not produced yet) falling back to silence rather than reading
// unwritten or stale ring memory.
static float ring_read_lerp(const float *ring, int cap, unsigned long long write_pos, double pos) {
    if (pos < 0.0) return 0.0f;
    double p0d = floor(pos);
    unsigned long long p0 = (unsigned long long)p0d;
    if (p0 + 1ULL >= write_pos) return 0.0f;
    float a = ring_read1(ring, cap, p0);
    float b = ring_read1(ring, cap, p0 + 1ULL);
    float frac = (float)(pos - p0d);
    return a + (b - a) * frac;
}

// ---------------------------------------------------------------- RBJ biquad (VFX_RADIO)

static float biquad2_process(Biquad2 *bq, float x) {
    // transposed Direct Form II
    float y = bq->b0 * x + bq->z1;
    bq->z1 = bq->b1 * x - bq->a1 * y + bq->z2 + TINY_DC;
    bq->z2 = bq->b2 * x - bq->a2 * y + TINY_DC;
    return y;
}

static void biquad2_set_hp(Biquad2 *bq, float f0, float fs, float q) {
    float w0 = 2.0f * VDSP_PI * f0 / fs;
    float cosw0 = cosf(w0);
    float alpha = sinf(w0) / (2.0f * q);
    float a0 = 1.0f + alpha;
    bq->b0 = ((1.0f + cosw0) / 2.0f) / a0;
    bq->b1 = (-(1.0f + cosw0)) / a0;
    bq->b2 = ((1.0f + cosw0) / 2.0f) / a0;
    bq->a1 = (-2.0f * cosw0) / a0;
    bq->a2 = (1.0f - alpha) / a0;
    bq->z1 = 0.0f;
    bq->z2 = 0.0f;
}

static void biquad2_set_lp(Biquad2 *bq, float f0, float fs, float q) {
    float w0 = 2.0f * VDSP_PI * f0 / fs;
    float cosw0 = cosf(w0);
    float alpha = sinf(w0) / (2.0f * q);
    float a0 = 1.0f + alpha;
    bq->b0 = ((1.0f - cosw0) / 2.0f) / a0;
    bq->b1 = (1.0f - cosw0) / a0;
    bq->b2 = ((1.0f - cosw0) / 2.0f) / a0;
    bq->a1 = (-2.0f * cosw0) / a0;
    bq->a2 = (1.0f - alpha) / a0;
    bq->z1 = 0.0f;
    bq->z2 = 0.0f;
}

// ---------------------------------------------------------------- pitch stage

// Runs WSOLA grains until ps_s1_write reaches target_write, or until the raw input ring runs
// out of the samples the next grain would need (in which case it stops; the caller tries again
// once more input has arrived).
static void ps_run_wsola(VoiceDsp *d, unsigned long long target_write) {
    while (d->ps_s1_write < target_write) {
        unsigned long long ideal = d->ps_ideal_next;
        unsigned long long need_in = ideal + (unsigned long long)PS_SEARCH + (unsigned long long)VDSP_PS_GRAIN;
        if (d->ps_in_write < need_in) break;

        unsigned long long out_base = (unsigned long long)PS_LATENCY +
                                       (unsigned long long)d->ps_grain_idx * (unsigned long long)PS_HOP;
        unsigned long long s_best = ideal;

        if (d->ps_grain_idx > 0) {
            int lo = -PS_SEARCH, hi = PS_SEARCH;
            if (ideal < (unsigned long long)PS_SEARCH) lo = -(int)ideal;
            float best_score = -1.0e30f;
            unsigned long long best_s = ideal;
            for (int k = lo; k <= hi; k++) {
                unsigned long long s = ideal + (unsigned long long)(long long)k;
                float num = 0.0f, ea = 0.0f, eb = 0.0f;
                for (int t = 0; t < PS_HOP; t++) {
                    float a = ring_read1(d->ps_s1, VDSP_PS_S1_CAP, out_base + (unsigned long long)t);
                    float b = ring_read1(d->ps_in, VDSP_PS_IN_CAP, s + (unsigned long long)t);
                    num += a * b;
                    ea += a * a;
                    eb += b * b;
                }
                float score = num / sqrtf(ea * eb + TINY_DC);
                if (score > best_score) { best_score = score; best_s = s; }
            }
            s_best = best_s;
        }

        for (int t = 0; t < VDSP_PS_GRAIN; t++) {
            float x = ring_read1(d->ps_in, VDSP_PS_IN_CAP, s_best + (unsigned long long)t);
            unsigned long long p = out_base + (unsigned long long)t;
            size_t idx = (size_t)(p % (unsigned long long)VDSP_PS_S1_CAP);
            if (t >= PS_HOP) d->ps_s1[idx] = 0.0f;  // first touch of this position: clear stale data
            d->ps_s1[idx] += d->ps_hann[t] * x;
        }

        d->ps_s1_write = out_base + (unsigned long long)PS_HOP;
        d->ps_ideal_next = ideal + (unsigned long long)d->ps_hop_in;
        d->ps_grain_idx += 1;
    }
}

// ---------------------------------------------------------------- formant stage

static void fm_run_ola(VoiceDsp *d, unsigned long long target_write) {
    while (d->fm_out_write < target_write) {
        int k = d->fm_win_idx;
        unsigned long long centre = (unsigned long long)k * (unsigned long long)FM_HOP +
                                     (unsigned long long)(VDSP_FM_WIN / 2);
        double max_off = ((double)(VDSP_FM_WIN - 1) - (double)(VDSP_FM_WIN / 2)) * (double)d->formant;
        double max_pos = (double)centre + max_off;
        if (max_pos + 1.0 >= (double)d->fm_in_write) break;

        unsigned long long out_base = (unsigned long long)FM_LATENCY +
                                       (unsigned long long)k * (unsigned long long)FM_HOP;
        for (int j = 0; j < VDSP_FM_WIN; j++) {
            double pos = (double)centre + ((double)j - (double)(VDSP_FM_WIN / 2)) * (double)d->formant;
            float sample = ring_read_lerp(d->fm_in, VDSP_FM_IN_CAP, d->fm_in_write, pos);
            unsigned long long p = out_base + (unsigned long long)j;
            size_t idx = (size_t)(p % (unsigned long long)VDSP_FM_OUT_CAP);
            if (j >= FM_HOP) d->fm_out[idx] = 0.0f;
            d->fm_out[idx] += d->fm_hann[j] * sample;
        }

        d->fm_out_write = out_base + (unsigned long long)FM_HOP;
        d->fm_win_idx = k + 1;
    }
}

// ---------------------------------------------------------------- effect stage

static void apply_effect(VoiceDsp *d, float *frame, int n) {
    switch (d->effect) {
    case VFX_RADIO: {
        for (int i = 0; i < n; i++) {
            float x = biquad2_process(&d->radio_hp, frame[i]);
            x = biquad2_process(&d->radio_lp, x);
            frame[i] = tanhf(x * 2.5f) * 0.6f;
        }
        float rms = voice_dsp_rms(frame, n);
        for (int i = 0; i < n; i++) {
            frame[i] += noise1(&d->radio_seed) * 0.06f * rms;
        }
        break;
    }
    case VFX_RING: {
        const float step = 2.0f * VDSP_PI * 55.0f / VDSP_RATE;
        for (int i = 0; i < n; i++) {
            frame[i] *= 0.4f + 0.6f * sinf(d->ring_phase);
            d->ring_phase += step;
            if (d->ring_phase >= 2.0f * VDSP_PI) d->ring_phase -= 2.0f * VDSP_PI;
        }
        break;
    }
    case VFX_NONE:
    default:
        break;
    }
}

// ---------------------------------------------------------------- public API

int voice_effect_from_name(const char *s) {
    if (!s) return -1;
    if (strcmp(s, "none") == 0) return VFX_NONE;
    if (strcmp(s, "radio") == 0) return VFX_RADIO;
    if (strcmp(s, "ring") == 0) return VFX_RING;
    return -1;
}

const char *voice_effect_name(int e) {
    switch (e) {
    case VFX_RADIO: return "radio";
    case VFX_RING:  return "ring";
    default:        return "none";
    }
}

void voice_dsp_init(VoiceDsp *d, float pitch, float formant, int effect) {
    memset(d, 0, sizeof(*d));

    pitch = clampf_local(pitch, 0.5f, 2.0f);
    formant = clampf_local(formant, 0.7f, 1.5f);
    if (effect < 0 || effect >= VFX_COUNT) effect = VFX_NONE;

    d->pitch = pitch;
    d->formant = formant;
    d->effect = effect;
    d->bypass = (fabsf(pitch - 1.0f) < 1e-3f) && (fabsf(formant - 1.0f) < 1e-3f) && (effect == VFX_NONE);
    d->formant_active = fabsf(formant - 1.0f) > 1e-3f;

    // pitch stage
    for (int i = 0; i < VDSP_PS_GRAIN; i++) {
        d->ps_hann[i] = 0.5f - 0.5f * cosf(2.0f * VDSP_PI * (float)i / (float)VDSP_PS_GRAIN);
    }
    d->ps_hop_in = (int)lroundf((float)PS_HOP / pitch);
    if (d->ps_hop_in < 1) d->ps_hop_in = 1;
    d->ps_s1_write = (unsigned long long)PS_LATENCY;

    // formant stage
    for (int i = 0; i < VDSP_FM_WIN; i++) {
        d->fm_hann[i] = 0.5f - 0.5f * cosf(2.0f * VDSP_PI * (float)i / (float)VDSP_FM_WIN);
    }
    d->fm_out_write = (unsigned long long)FM_LATENCY;

    // effect stage
    if (effect == VFX_RADIO) {
        biquad2_set_hp(&d->radio_hp, 300.0f, VDSP_RATE, 0.7071f);
        biquad2_set_lp(&d->radio_lp, 3000.0f, VDSP_RATE, 0.7071f);
    }
    d->radio_seed = 0x9E3779B9u;
}

void voice_dsp_process(VoiceDsp *d, float *frame, int n) {
    if (d->bypass) return;
    if (n != VDSP_FRAME) return; // contract violation; stay silent-safe rather than read/write OOB

    // ---- pitch stage: push raw input, run WSOLA up to what stage 2 will need, then resample ----
    for (int i = 0; i < n; i++) {
        unsigned long long p = d->ps_in_write + (unsigned long long)i;
        d->ps_in[(size_t)(p % (unsigned long long)VDSP_PS_IN_CAP)] = frame[i];
    }
    d->ps_in_write += (unsigned long long)n;

    double s2_end = d->ps_s2_read + (double)n * (double)d->pitch;
    ps_run_wsola(d, (unsigned long long)ceil(s2_end) + 2ULL);

    for (int i = 0; i < n; i++) {
        frame[i] = ring_read_lerp(d->ps_s1, VDSP_PS_S1_CAP, d->ps_s1_write, d->ps_s2_read);
        d->ps_s2_read += (double)d->pitch;
    }

    // ---- formant stage (optional): push pitch-shifted output, run OLA, read back 1:1 ----
    if (d->formant_active) {
        for (int i = 0; i < n; i++) {
            unsigned long long p = d->fm_in_write + (unsigned long long)i;
            d->fm_in[(size_t)(p % (unsigned long long)VDSP_FM_IN_CAP)] = frame[i];
        }
        d->fm_in_write += (unsigned long long)n;

        fm_run_ola(d, d->fm_out_read + (unsigned long long)n);

        for (int i = 0; i < n; i++) {
            unsigned long long p = d->fm_out_read + (unsigned long long)i;
            frame[i] = ring_read1(d->fm_out, VDSP_FM_OUT_CAP, p);
        }
        d->fm_out_read += (unsigned long long)n;
    }

    // ---- effect stage ----
    apply_effect(d, frame, n);

    // ---- final safety clamp ----
    for (int i = 0; i < n; i++) {
        frame[i] = clampf_local(frame[i], -1.0f, 1.0f);
    }
}

int voice_dsp_latency(const VoiceDsp *d) {
    if (d->bypass) return 0;
    int lat = PS_LATENCY;
    if (d->formant_active) lat += FM_LATENCY;
    return lat;
}

float voice_dsp_rms(const float *frame, int n) {
    if (n <= 0) return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        double x = (double)frame[i];
        sum += x * x;
    }
    return (float)sqrt(sum / (double)n);
}
