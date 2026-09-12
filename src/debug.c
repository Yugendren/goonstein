#include "debug.h"
#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define DBG_LINES 96
// 256, not 160: the `smooth:` line carries eye, flat, traverse, frame and cadence now, and a
// diagnostic that silently loses its last field is worse than no diagnostic.
#define DBG_LEN   256

static char g_lines[DBG_LINES][DBG_LEN];
static int g_head, g_count;
static double g_time;
static FILE *g_file;
static char g_log_path[512];
#define WARN_LINES 12
static char g_warn[WARN_LINES][DBG_LEN]; static int g_warn_head, g_warn_count;
static SDL_LogOutputFunction g_default_out; static void *g_default_ud;

static void log_hook(void *ud, int category, SDL_LogPriority priority, const char *message) {
    (void)ud;   // SDL hands back the userdata we registered; the real one is g_default_ud
    if (g_default_out) g_default_out(g_default_ud, category, priority, message);
    if (priority < SDL_LOG_PRIORITY_WARN) return;
    char *slot = g_warn[(g_warn_head + g_warn_count) % WARN_LINES];
    if (g_warn_count == WARN_LINES) { g_warn_head = (g_warn_head + 1) % WARN_LINES; slot = g_warn[(g_warn_head + g_warn_count - 1) % WARN_LINES]; } else g_warn_count++;
    snprintf(slot, DBG_LEN, "%7.2f  %s", g_time, message);
    dbg_log("[warn] %s", message);
}

// The log used to fflush every single line, and dbg_log is called from inside the SDL event drain
// (every key and every mouse button writes one). An fflush is a write(2), and a write(2) to a file
// on this machine is not bounded: with Spotlight or a backup walking the same directory it blocks
// for tens of milliseconds, inside the input phase, where it reads as the game hanging on the
// keyboard. That is exactly the shape of the mid-play `input 52 ms` hitches in a played session's
// log. So: a real stdio buffer, flushed at most four times a second, and immediately for anything
// that might be the last line before a crash (a warning, a hitch, a stall). The cost of the flush
// is measured by the flush itself -- if it ever does block, the count and the worst one are in the
// `log:` line at exit rather than in a frame time.
static Uint64 g_last_flush_ns;
static unsigned g_flushes, g_slow_flushes; static double g_worst_flush_ms;
static char g_buf[1 << 16];
static void dbg_flush_now(void) {
    if (!g_file) return;
    Uint64 t0 = SDL_GetTicksNS();
    fflush(g_file);
    double ms = (double)(SDL_GetTicksNS() - t0) * 1e-6;
    g_flushes++; if (ms > 2.0) g_slow_flushes++;
    if (ms > g_worst_flush_ms) g_worst_flush_ms = ms;
    g_last_flush_ns = SDL_GetTicksNS();
}
void dbg_init(const char *log_path) {
    snprintf(g_log_path, sizeof g_log_path, "%s", log_path);
    g_file = fopen(log_path, "w");
    if (g_file) { setvbuf(g_file, g_buf, _IOFBF, sizeof g_buf); fprintf(g_file, "# hollow debug log\n"); dbg_flush_now(); }
    SDL_GetLogOutputFunction(&g_default_out, &g_default_ud);
    SDL_SetLogOutputFunction(log_hook, NULL);
}
void dbg_set_time(double t) { g_time = t; }

void dbg_log(const char *fmt, ...) {
    char msg[DBG_LEN - 12];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    char *slot = g_lines[(g_head + g_count) % DBG_LINES];
    if (g_count == DBG_LINES) { g_head = (g_head + 1) % DBG_LINES; slot = g_lines[(g_head + g_count - 1) % DBG_LINES]; }
    else g_count++;
    snprintf(slot, DBG_LEN, "%7.2f  %s", g_time, msg);
    if (g_file) {
        fputs(slot, g_file); fputc('\n', g_file);
        // Anything that could be the last line before a crash goes out now; everything else rides
        // the buffer until the quarter-second timer.
        bool urgent = msg[0] == '[' || !SDL_strncmp(msg, "hitch", 5) || !SDL_strncmp(msg, "stall", 5);
        Uint64 now = SDL_GetTicksNS();
        if (urgent || now - g_last_flush_ns > 250 * SDL_NS_PER_MS) dbg_flush_now();
    }
}

void dbg_log_stats(unsigned *flushes, unsigned *slow, double *worst_ms) {
    if (flushes) *flushes = g_flushes;
    if (slow) *slow = g_slow_flushes;
    if (worst_ms) *worst_ms = g_worst_flush_ms;
}

int dbg_line_count(void) { return g_count; }
const char *dbg_line(int i) { return g_lines[(g_head + i) % DBG_LINES]; }

bool dbg_snapshot(const char *header) {
    static char buf[DBG_LINES * DBG_LEN + 4096];
    size_t n = 0;
    n += (size_t)snprintf(buf + n, sizeof buf - n, "=== hollow snapshot at %.2f s ===\n%s\n--- recent events (oldest first) ---\n", g_time, header);
    for (int i = 0; i < g_count && n < sizeof buf - DBG_LEN; i++) n += (size_t)snprintf(buf + n, sizeof buf - n, "%s\n", dbg_line(i));
    FILE *f = fopen("hollow_snapshot.txt", "w");
    bool ok = f != NULL;
    if (f) { fputs(buf, f); fclose(f); }
    if (!SDL_SetClipboardText(buf)) ok = false;
    return ok;
}

void dbg_shutdown(void) {
    if (g_file) {
        dbg_log("log: %u flushes, %u over 2 ms, worst %.1f ms", g_flushes, g_slow_flushes, g_worst_flush_ms);
        SDL_Log("log: %u flushes, %u over 2 ms, worst %.1f ms", g_flushes, g_slow_flushes, g_worst_flush_ms);
        fclose(g_file);
    }
    g_file = NULL;
}

int dbg_warning_count(void) { return g_warn_count; }
const char *dbg_warning(int i) { return g_warn[(g_warn_head + i) % WARN_LINES]; }
