// Three-tier render quality: potato / normal / high. One tier, one settings.txt line, one flag
// (--quality), applied to the handful of gfx levers that already existed for other reasons
// (render scale, shadow map size, texture cap, HDR format, scatter density, view distance).
//
// This header is the ONLY place the tier table's numbers appear -- quality.c just reads them out
// of quality_spec(). If a number in the table below ever needs to change, it changes here and
// nowhere else.
#pragma once
#include <stdbool.h>

struct Game;      // src/game.h; quality_apply and the probe reach into g->gfx and g->settings
struct Platform;  // src/platform.h; the probe is fed the platform each frame, unused today but
                   // kept in the signature so a later probe can look at vsync/fps_cap if it needs to

typedef enum Quality { Q_POTATO = 0, Q_NORMAL, Q_HIGH, Q_COUNT } Quality;

typedef struct QualitySpec {
    float render_scale;   // gfx_set_render_scale: fraction of the fixed 1280x800 UI space
    int   shadow_size;    // gfx_set_shadow_size: 2048 | 1024 | 512
    int   tex_cap;        // gfx_set_texture_cap: cap on every subsequently loaded texture's longest edge
    bool  small_hdr;      // gfx_request_small_hdr: RGBA16F -> R11G11B10 (must be set before gfx_init)
    float scatter_keep;   // fraction of small, far-LOD-having props (grass/plants/small rocks) drawn
    float far_scale;      // multiplies the level's `look far` camera distance; fog is pulled in to match
} QualitySpec;

const char  *quality_name(Quality q);                    // "potato" / "normal" / "high"
Quality      quality_parse(const char *word, Quality fallback);
QualitySpec  quality_spec(Quality q);
Quality      quality_current(void);

// props.c calls this once per prop collected; it has to be a bare static read (no SDL_getenv, no
// file I/O) or scatter thinning would cost more than the props it removes.
float        quality_scatter_keep(void);

// Chosen at startup from settings.txt, or guessed if the key is absent (see quality.c for what
// the guess can and can't see). *was_unset comes back true only when the guess fired, which is
// the caller's cue to arm the probe with quality_probe_start().
Quality      quality_startup(bool *was_unset);

// Remembers a tier (quality_current()/quality_scatter_keep() start reflecting it immediately).
// Does not touch the renderer -- see quality_apply_early/quality_apply for that.
void         quality_set(Quality q);
const char  *quality_source(void);   // "settings" | "guess" | "probe", for the log line and the overlay

// Before gfx_init: small_hdr has to be decided before the swapchain's HDR target is created.
void         quality_apply_early(Quality q);
// After gfx_init (or any time after, on a level reload): render scale, shadow map size, texture
// cap. Also calls quality_set(q), so callers do not need to call both.
void         quality_apply(struct Game *g, Quality q);

// The two-second probe. quality_probe_start() arms it (only when settings.txt had no `quality`
// line); quality_probe_frame() is called once per rendered frame from the main loop while it is
// running -- it returns true while still probing, false once it has finished (or if it was never
// armed, so the caller does not need to guard the call). When it finishes it may drop the tier by
// one step, apply the drop live, and write the result back to settings.txt with
// game_settings_set() so the next run starts there.
void         quality_probe_start(void);
bool         quality_probe_frame(struct Game *g, struct Platform *pf);
