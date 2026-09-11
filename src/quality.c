// See quality.h for the table and what each field does. This file is startup plumbing around
// that table: settings.txt <-> Quality, a coarse guess for a machine we have never seen, and the
// two-second probe that corrects the guess instead of trusting it.
#include "quality.h"
#include "game.h"    // Game.gfx, game_settings_set
#include "gfx.h"
#include "platform.h"
#include "prof.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

// The tier table. Nowhere else in the codebase names these six numbers.
static const QualitySpec SPECS[Q_COUNT] = {
    [Q_POTATO] = { .render_scale = 0.66f, .shadow_size = 512,  .tex_cap = 256,  .small_hdr = true,  .scatter_keep = 0.50f, .far_scale = 0.70f },
    [Q_NORMAL] = { .render_scale = 1.00f, .shadow_size = 1024, .tex_cap = 1024, .small_hdr = false, .scatter_keep = 1.00f, .far_scale = 1.00f },
    [Q_HIGH]   = { .render_scale = 1.00f, .shadow_size = 2048, .tex_cap = 2048, .small_hdr = false, .scatter_keep = 1.00f, .far_scale = 1.00f },
};
static const char *NAMES[Q_COUNT] = { "potato", "normal", "high" };

static Quality     g_current = Q_NORMAL;
static const char *g_source  = "settings";        // "settings" | "guess" | "probe"
static float       g_scatter_keep = 1.0f;         // quality_scatter_keep()'s entire job is to read this
static bool        g_probing = false;
static Uint64      g_probe_start_ms = 0;

const char *quality_name(Quality q) { return (q >= 0 && q < Q_COUNT) ? NAMES[q] : "?"; }

Quality quality_parse(const char *word, Quality fallback) {
    if (!word) return fallback;
    for (int i = 0; i < Q_COUNT; i++) if (!strcmp(word, NAMES[i])) return (Quality)i;
    return fallback;
}

QualitySpec quality_spec(Quality q) { return SPECS[(q >= 0 && q < Q_COUNT) ? q : Q_NORMAL]; }
Quality     quality_current(void)   { return g_current; }
const char *quality_source(void)    { return g_source; }
float       quality_scatter_keep(void) { return g_scatter_keep; }

void quality_set(Quality q) {
    if (q < 0 || q >= Q_COUNT) q = Q_NORMAL;
    g_current = q;
    g_scatter_keep = SPECS[q].scatter_keep;
}

void quality_apply_early(Quality q) {
    gfx_request_small_hdr(quality_spec(q).small_hdr);
}

void quality_apply(struct Game *g, Quality q) {
    quality_set(q);
    QualitySpec s = quality_spec(q);
    gfx_set_render_scale(&g->gfx, s.render_scale, false);
    gfx_set_shadow_size(&g->gfx, s.shadow_size);
    gfx_set_texture_cap(&g->gfx, s.tex_cap);
}

// settings.txt's own reader, independent of main.c's dense parser: quality.c owns the whole
// startup/guess/probe story, so it reads its one key itself rather than main.c threading a parsed
// string through a function that is meant to be usable on its own (see quality.h's declaration).
static bool read_settings_quality(char *out, size_t outn) {
    char sp[640]; snprintf(sp, sizeof sp, "%s/settings.txt", HOLLOW_ASSET_DIR);
    size_t n = 0; char *st = SDL_LoadFile(sp, &n);
    if (!st) return false;
    bool found = false;
    char *cur = st;
    while (*cur) {
        char *line = cur; char *nl = strchr(cur, '\n');
        if (nl) { *nl = 0; cur = nl + 1; } else cur += strlen(cur);
        char *hash = strchr(line, '#'); if (hash) *hash = 0;
        char key[32], val[32];
        if (sscanf(line, "%31s %31s", key, val) == 2 && !strcmp(key, "quality")) { snprintf(out, outn, "%s", val); found = true; }
    }
    SDL_free(st);
    return found;
}

Quality quality_startup(bool *was_unset) {
    if (was_unset) *was_unset = false;
    char word[32] = "";
    if (read_settings_quality(word, sizeof word)) {
        Quality q = quality_parse(word, Q_NORMAL);
        quality_set(q);
        g_source = "settings";
        return q;
    }
    // No `quality` line: first run on this machine (or someone deleted it). The guess below is
    // deliberately blind to the GPU -- verified against SDL 3.4's own headers
    // (build/_deps/sdl3-src/include/SDL3/SDL_gpu.h): SDL_GetGPUDeviceDriver(SDL_GPUDevice*) only
    // names the backend API ("metal"/"vulkan"/"direct3d12"), not the hardware behind it, and it
    // takes a device pointer -- so does the one API that DOES carry an actual device name,
    // SDL_GetGPUDeviceProperties()'s SDL_PROP_GPU_DEVICE_NAME_STRING. Neither is callable here:
    // small_hdr has to be decided before gfx_init creates that device at all. SDL_GetGPUDriver(i)
    // enumerates compiled-in backends without a device, but that is "which API exists on this
    // machine", not "how fast is it" -- Metal on an M4 and Metal on a 2016 MacBook say the same
    // thing there. So the guess uses only what is knowable with no device: cores and RAM. It is
    // not supposed to be right, only not embarrassing -- quality_probe_frame is what actually
    // measures this machine and corrects it.
    int cores = SDL_GetNumLogicalCPUCores();
    int ram_mb = SDL_GetSystemRAM();
    Quality q = (cores <= 4 || ram_mb < 8192) ? Q_POTATO : Q_NORMAL;
    quality_set(q);
    g_source = "guess";
    if (was_unset) *was_unset = true;
    SDL_Log("quality: no settings.txt line, guessed %s (cores=%d ram=%dMB) -- probing to check it", quality_name(q), cores, ram_mb);
    return q;
}

void quality_probe_start(void) {
    g_probing = true;
    g_probe_start_ms = SDL_GetTicks();
    prof_reset();   // the loading frames before this point are not "real play"; keep them out of the median
}

// The display's current refresh period in ms, or 0 if the platform/window/mode isn't known yet.
static float probe_refresh_ms(struct Platform *pf) {
    if (!pf || !pf->window) return 0.0f;
    const SDL_DisplayMode *m = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(pf->window));
    return (m && m->refresh_rate > 0.0f) ? 1000.0f / m->refresh_rate : 0.0f;
}

bool quality_probe_frame(struct Game *g, struct Platform *pf) {
    if (!g_probing) return false;
    if (SDL_GetTicks() - g_probe_start_ms < 2000) return true;
    g_probing = false;
    Quality before = g_current;
    float median = prof_median(PROF_FRAME);
    // PROF_FRAME is wall clock start-to-start, so with vsync on it is capped at the display's
    // refresh period no matter how cheap the frame actually was -- a 60Hz machine reads ~16.7ms
    // even when it is bored, and a 12ms budget would judge it "slow" forever. So the budget rides
    // the refresh rate up when vsync is on: a display-limited frame must never trip this, only a
    // frame that is genuinely late (over budget even after accounting for the wait) should. The
    // 10% slack absorbs scheduling jitter around the refresh period itself. This never lowers the
    // budget below 12ms, so a fast/uncapped machine (or a low refresh rate under 12ms, which
    // doesn't really happen) is still held to the same floor.
    float budget = 12.0f;
    // `!no_present` matters: main.c sets pf->vsync back to true from settings.txt after
    // platform_init cleared it, so under HOLLOW_NOPRESENT the flag still reads true even though
    // nothing is ever handed to a compositor and no frame is display-limited. Raising the budget
    // to the refresh period there would forgive a slowness that is entirely the machine's.
    if (pf && pf->vsync && !pf->no_present) {
        float refresh = probe_refresh_ms(pf);
        if (refresh > 0.0f) budget = SDL_max(budget, refresh * 1.10f);
    }
    // A frame cap does to PROF_FRAME exactly what vsync does, and it is the player's own choice
    // rather than a verdict on the hardware: `fps 30` in settings.txt must not be read as "this
    // machine cannot cope" and answered by quietly dropping them to potato.
    if (pf && pf->fps_cap > 0) budget = SDL_max(budget, 1000.0f / (float)pf->fps_cap * 1.10f);
    // Only ever drop, never raise: a guess that undershot just means the player enjoys the
    // headroom, which is not a problem worth a startup stutter to go fix.
    if (median > budget && (before == Q_NORMAL || before == Q_HIGH)) {
        Quality dropped = before == Q_HIGH ? Q_NORMAL : Q_POTATO;
        quality_apply(g, dropped);
        game_settings_set(g, "quality", quality_name(dropped));
        g_source = "probe";
        SDL_Log("quality: probe median %.2fms at %s (budget %.1fms) -- dropped to %s, wrote settings.txt", (double)median, quality_name(before), (double)budget, quality_name(dropped));
    } else {
        SDL_Log("quality: probe median %.2fms at %s (budget %.1fms) -- keeping it", (double)median, quality_name(before), (double)budget);
    }
    return false;
}
