// Where the frame went, made concrete. See prof.h for what this measures, what it cannot measure
// (GPU time), and why phases nest the way they do. Nothing here allocates after startup: the
// history ring below is a handful of static arrays living in the data segment, sized once at
// compile time, so a thousand-frame benchmark costs the same heap as a ten-frame smoke check (none).
#include "prof.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define PROF_HISTORY 4096
#define PROF_WARMUP 60          // frames dropped from median/p90 so JIT-ish one-time costs (shader
                                 // warm-up, first-touch page faults, the level's first hot-reload
                                 // check) do not drag a whole run's number around
#define PROF_MIN_FOR_WARMUP (g_warmup + 8)      // below this, dropping the warm-up would leave almost nothing

// How many frames after a reset are thrown away. 60 is right for a played session; the benchmark
// asks for more, because a GPU that has been idle needs longer than sixty frames to come up to
// clock and a median taken across that ramp is a median of two different machines. See
// prof_set_warmup and BENCH_WARMUP_FRAMES.
static int g_warmup = PROF_WARMUP;
void prof_set_warmup(int frames) { g_warmup = frames < 0 ? 0 : frames; }

static const char *PHASE_NAMES[PROF_COUNT] = {
    "frame", "input", "tick", "phys", "items", "net", "game", "reload", "render", "cull", "shadow",
    "world", "water", "particles", "post", "ui", "present_wait", "submit", "cap_sleep",
};
static const char *COUNTER_NAMES[PROF_C_COUNT] = {
    "draws", "instances", "tris", "props_drawn", "props_culled", "batches",
};

// Phases the overlay indents under the one that contains them: what runs inside TICK and what
// runs inside RENDER. An outer phase's number already includes them (prof.h is explicit about
// this), so this is
// display only -- it does not change what prof_begin/prof_end sum or what the median is taken over.
static bool phase_is_nested(ProfPhase p) {
    return p == PROF_TICK_PHYS || p == PROF_TICK_ITEMS || p == PROF_TICK_NET || p == PROF_TICK_GAME
        || p == PROF_TICK_RELOAD
        || p == PROF_CULL || p == PROF_SHADOW || p == PROF_WORLD || p == PROF_WATER
        || p == PROF_PARTICLES || p == PROF_POST || p == PROF_UI;
}

// The frame currently being built: accumulators fed by prof_begin/prof_end and prof_count since the
// last prof_frame_begin, and each phase's open/closed state (so a stray prof_end with no matching
// prof_begin is a no-op instead of reading garbage).
static float    accum_ms[PROF_COUNT];
static Uint64   phase_start_tick[PROF_COUNT];
static bool     phase_open[PROF_COUNT];
static unsigned counter_cur[PROF_C_COUNT];
static Uint64   input_mark_tick, present_mark_tick;
static bool     input_marked, present_marked;
static Uint64   frame_start_tick;
static bool     frame_started;   // false until the first prof_frame_begin (or right after a reset),
                                  // so that call does not try to close a PROF_FRAME interval against
                                  // a stamp that was never taken

// History ring. hist_write is the slot the NEXT frame lands in. frames_seen counts every frame ever
// pushed, uncapped -- it is what tells "still inside the warm-up window" apart from "the ring has
// wrapped so many times the oldest frame in it is nowhere near frame 60".
static float    hist_phase[PROF_HISTORY][PROF_COUNT];
static unsigned hist_counter[PROF_HISTORY][PROF_C_COUNT];
static float    hist_input_latency[PROF_HISTORY];
static int      hist_write;
static long     frames_seen;

// Scratch for sorting a window of history into a median/p90: never on the stack (PROF_HISTORY
// floats is 16 KB), reused by every call site below in turn since none of them are reentrant.
static float scratch[PROF_HISTORY];

const char *prof_phase_name(ProfPhase p)     { return (p >= 0 && p < PROF_COUNT)   ? PHASE_NAMES[p]   : "?"; }
const char *prof_counter_name(ProfCounter c) { return (c >= 0 && c < PROF_C_COUNT) ? COUNTER_NAMES[c] : "?"; }

static double ms_per_tick(void) {
    static double m = -1;
    if (m < 0) { Uint64 f = SDL_GetPerformanceFrequency(); m = f ? 1000.0 / (double)f : 0.0; }
    return m;
}

void prof_reset(void) {
    memset(accum_ms, 0, sizeof accum_ms);
    memset(phase_start_tick, 0, sizeof phase_start_tick);
    memset(phase_open, 0, sizeof phase_open);
    memset(counter_cur, 0, sizeof counter_cur);
    input_mark_tick = present_mark_tick = 0;
    input_marked = present_marked = false;
    frame_start_tick = 0; frame_started = false;
    memset(hist_phase, 0, sizeof hist_phase);
    memset(hist_counter, 0, sizeof hist_counter);
    memset(hist_input_latency, 0, sizeof hist_input_latency);
    hist_write = 0; frames_seen = 0;
}

void prof_begin(ProfPhase p) {
    if (p < 0 || p >= PROF_COUNT) return;
    phase_start_tick[p] = SDL_GetPerformanceCounter();
    phase_open[p] = true;
}

void prof_end(ProfPhase p) {
    if (p < 0 || p >= PROF_COUNT || !phase_open[p]) return;   // no open prof_begin: ignore rather than fault
    Uint64 now = SDL_GetPerformanceCounter();
    accum_ms[p] += (float)((double)(now - phase_start_tick[p]) * ms_per_tick());
    phase_open[p] = false;
}

void prof_count(ProfCounter c, unsigned add) {
    if (c < 0 || c >= PROF_C_COUNT) return;
    counter_cur[c] += add;
}

unsigned prof_counter_value(ProfCounter c) { return (c >= 0 && c < PROF_C_COUNT) ? counter_cur[c] : 0; }

void prof_mark_input(void)   { input_mark_tick = SDL_GetPerformanceCounter();   input_marked = true; }
void prof_mark_present(void) { present_mark_tick = SDL_GetPerformanceCounter(); present_marked = true; }

void prof_frame_begin(void) {
    Uint64 now = SDL_GetPerformanceCounter();
    if (frame_started) {
        accum_ms[PROF_FRAME] = (float)((double)(now - frame_start_tick) * ms_per_tick());
        float latency = (input_marked && present_marked)
                       ? (float)((double)(present_mark_tick - input_mark_tick) * ms_per_tick()) : 0.0f;
        int slot = hist_write;
        memcpy(hist_phase[slot], accum_ms, sizeof accum_ms);
        memcpy(hist_counter[slot], counter_cur, sizeof counter_cur);
        hist_input_latency[slot] = latency;
        hist_write = (hist_write + 1) % PROF_HISTORY;
        frames_seen++;
    }
    memset(accum_ms, 0, sizeof accum_ms);
    memset(phase_start_tick, 0, sizeof phase_start_tick);
    memset(phase_open, 0, sizeof phase_open);
    memset(counter_cur, 0, sizeof counter_cur);
    input_marked = present_marked = false;
    frame_start_tick = now;
    frame_started = true;
}

// The window median/p90/prof_frames/prof_input_latency_ms all read: the newest `count` frames in
// the ring, with the oldest PROF_WARMUP of them dropped -- unless the run is too short to spare 60
// frames and still say anything, in which case nothing is dropped (out_short_run tells the caller
// that happened, for prof_dump's caveat line).
static void usable_range(int *out_base, int *out_count, bool *out_short_run) {
    int ring_count = (int)(frames_seen < PROF_HISTORY ? frames_seen : PROF_HISTORY);
    bool short_run = frames_seen < PROF_MIN_FOR_WARMUP;
    // Once the ring has wrapped, its oldest frame is already long past frame 60, so there is no
    // warm-up left inside it to drop.
    int warmup = (!short_run && frames_seen < PROF_HISTORY) ? g_warmup : 0;
    int usable = ring_count - warmup;
    if (usable < 0) usable = 0;
    int base = ((hist_write - usable) % PROF_HISTORY + PROF_HISTORY) % PROF_HISTORY;
    *out_base = base; *out_count = usable;
    if (out_short_run) *out_short_run = short_run;
}

static int gather_phase(ProfPhase p, float *out) {
    int base, count; usable_range(&base, &count, NULL);
    for (int i = 0; i < count; i++) out[i] = hist_phase[(base + i) % PROF_HISTORY][p];
    return count;
}
static int gather_counter(ProfCounter c, float *out) {
    int base, count; usable_range(&base, &count, NULL);
    for (int i = 0; i < count; i++) out[i] = (float)hist_counter[(base + i) % PROF_HISTORY][c];
    return count;
}
static int cmp_float(const void *a, const void *b) {
    float fa = *(const float *)a, fb = *(const float *)b;
    return (fa > fb) - (fa < fb);
}

float prof_ms(ProfPhase p) {
    if (p < 0 || p >= PROF_COUNT || frames_seen == 0) return 0.0f;
    int last = (hist_write - 1 + PROF_HISTORY) % PROF_HISTORY;
    return hist_phase[last][p];
}

void prof_copy_frame(float out[PROF_COUNT]) {
    if (!out) return;
    if (frames_seen == 0) { memset(out, 0, sizeof(float) * PROF_COUNT); return; }
    int last = (hist_write - 1 + PROF_HISTORY) % PROF_HISTORY;
    memcpy(out, hist_phase[last], sizeof(float) * PROF_COUNT);
}

float prof_median(ProfPhase p) {
    int n = gather_phase(p, scratch);
    if (n <= 0) return 0.0f;
    qsort(scratch, (size_t)n, sizeof(float), cmp_float);
    return scratch[n / 2];
}

float prof_p90(ProfPhase p) {
    int n = gather_phase(p, scratch);
    if (n <= 0) return 0.0f;
    qsort(scratch, (size_t)n, sizeof(float), cmp_float);
    return scratch[n - 1 - n / 10];
}

float prof_counter_median(ProfCounter c) {
    int n = gather_counter(c, scratch);
    if (n <= 0) return 0.0f;
    qsort(scratch, (size_t)n, sizeof(float), cmp_float);
    return scratch[n / 2];
}

int prof_frames(void) {
    int base, count; usable_range(&base, &count, NULL);
    return count;
}

float prof_input_latency_ms(void) {
    int base, count; usable_range(&base, &count, NULL);
    int n = 0;
    for (int i = 0; i < count; i++) {
        float v = hist_input_latency[(base + i) % PROF_HISTORY];
        if (v != 0.0f) scratch[n++] = v;   // a frame with no mark on one side reads 0; skip it
    }
    if (n <= 0) return 0.0f;
    qsort(scratch, (size_t)n, sizeof(float), cmp_float);
    return scratch[n / 2];
}

int prof_overlay_lines(char lines[][96], int max) {
    if (max <= 0) return 0;
    int n = 0;
    float frame_med = prof_median(PROF_FRAME);
    for (int p = 0; p < PROF_COUNT && n < max; p++) {
        float med = prof_median((ProfPhase)p);
        if (med <= 0.0f) continue;   // nothing recorded for this phase yet (PROF_CULL until a later commit wires it, say)
        float p90 = prof_p90((ProfPhase)p);
        float ratio = frame_med > 0.0f ? med / frame_med : 0.0f;
        if (ratio < 0.0f) ratio = 0.0f; if (ratio > 1.0f) ratio = 1.0f;
        int filled = (int)(ratio * 10.0f + 0.5f);
        if (filled < 0) filled = 0; if (filled > 10) filled = 10;
        char bar[11]; for (int i = 0; i < 10; i++) bar[i] = i < filled ? '#' : '-'; bar[10] = 0;
        char name[16]; snprintf(name, sizeof name, "%s%s", phase_is_nested((ProfPhase)p) ? "  " : "", prof_phase_name((ProfPhase)p));
        snprintf(lines[n], 96, "%-13s%.2f ms  p90 %.2f  %s", name, med, p90, bar);
        n++;
    }
    if (n < max) {
        float draws = prof_counter_median(PROF_C_DRAWS), inst = prof_counter_median(PROF_C_INSTANCES),
              tris = prof_counter_median(PROF_C_TRIS), batches = prof_counter_median(PROF_C_BATCHES),
              drawn = prof_counter_median(PROF_C_PROPS_DRAWN), culled = prof_counter_median(PROF_C_PROPS_CULLED);
        char tris_s[16];
        if (tris >= 1000.0f) snprintf(tris_s, sizeof tris_s, "%.0fk", tris / 1000.0f);
        else snprintf(tris_s, sizeof tris_s, "%.0f", tris);
        snprintf(lines[n], 96, "draws %.0f  inst %.0f  tris %s  batches %.0f  props %.0f/%.0f",
                 draws, inst, tris_s, batches, drawn, culled);
        n++;
    }
    return n;
}

// The `prof:` block. Kept to the shape documented in prof.h -- tooling greps this, so the first
// line and the per-phase line stay exactly as they are; the short-run caveat is an extra line
// rather than a change to either of those.
void prof_dump(const char *tag) {
    int base, count; bool short_run; usable_range(&base, &count, &short_run);
    (void)base;
    SDL_Log("prof: %s  frames %d  (median ms / p90 ms)", tag, count);
    if (short_run) SDL_Log("prof:   (only %d frames recorded, fewer than the %d needed to drop warm-up; every frame counted)", count, PROF_MIN_FOR_WARMUP);
    for (int p = 0; p < PROF_COUNT; p++) {
        float med = prof_median((ProfPhase)p);
        if (med <= 0.0f) continue;
        SDL_Log("prof:   %-14s %7.3f %7.3f", prof_phase_name((ProfPhase)p), med, prof_p90((ProfPhase)p));
    }
    SDL_Log("prof:   counters draws %.0f instances %.0f tris %.0f batches %.0f props_drawn %.0f props_culled %.0f",
            prof_counter_median(PROF_C_DRAWS), prof_counter_median(PROF_C_INSTANCES), prof_counter_median(PROF_C_TRIS),
            prof_counter_median(PROF_C_BATCHES), prof_counter_median(PROF_C_PROPS_DRAWN), prof_counter_median(PROF_C_PROPS_CULLED));
    SDL_Log("prof:   input->present %.2f ms (median)", prof_input_latency_ms());
}
