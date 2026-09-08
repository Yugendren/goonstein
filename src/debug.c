#include "debug.h"
#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define DBG_LINES 96
#define DBG_LEN   160

static char g_lines[DBG_LINES][DBG_LEN];
static int g_head, g_count;
static double g_time;
static FILE *g_file;
static char g_log_path[512];

void dbg_init(const char *log_path) {
    snprintf(g_log_path, sizeof g_log_path, "%s", log_path);
    g_file = fopen(log_path, "w");
    if (g_file) { fprintf(g_file, "# hollow debug log\n"); fflush(g_file); }
}
void dbg_set_time(double t) { g_time = t; }

void dbg_log(const char *fmt, ...) {
    char msg[DBG_LEN - 12];
    va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    char *slot = g_lines[(g_head + g_count) % DBG_LINES];
    if (g_count == DBG_LINES) { g_head = (g_head + 1) % DBG_LINES; slot = g_lines[(g_head + g_count - 1) % DBG_LINES]; }
    else g_count++;
    snprintf(slot, DBG_LEN, "%7.2f  %s", g_time, msg);
    if (g_file) { fputs(slot, g_file); fputc('\n', g_file); fflush(g_file); }
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

void dbg_shutdown(void) { if (g_file) fclose(g_file); g_file = NULL; }
