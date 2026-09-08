// Screen-space text effects: damage/heal/block numbers, parry and fail call-outs, turn banners
// and plain fading labels. Everything lives in a fixed-size array, no allocations; uifx_draw
// only calls gfx_ui_rect / gfx_ui_text / gfx_ui_text_width.
#pragma once
#include "hmath.h"
#include "gfx.h"

#define UIFX_MAX 64

typedef enum UifxStyle {
    UIFX_DAMAGE,     // big number, pops up and drifts upward, scales in with overshoot, fades
    UIFX_HEAL,       // like damage but green and floats gently
    UIFX_BLOCK,      // grey-blue, small, brief
    UIFX_PARRY,      // gold "PARRY" or "PERFECT": hard scale-in with overshoot, then shrink
    UIFX_FAIL,       // red "LATE"/"EARLY"/"MISS": shake horizontally, fade
    UIFX_BANNER,     // full-width turn banner: dark bar across the centre, text slides in from left, holds, slides out right
    UIFX_LABEL,      // plain small text that fades, for intents or notes
} UifxStyle;

typedef struct UifxItem {
    bool alive; UifxStyle style;
    float x, y;             // anchor in UI pixels (for BANNER, y is the bar centre)
    float t, dur;           // age and lifetime
    float scale;            // base text scale (gfx_ui_text scale)
    Vec4 color;
    char text[64];
    float seed;
} UifxItem;

typedef struct Uifx { UifxItem items[UIFX_MAX]; } Uifx;

void uifx_init(Uifx *u);
void uifx_clear(Uifx *u);
// Spawn one. Returns nothing. For BANNER use x = 0 (ignored) and y = bar centre.
void uifx_spawn(Uifx *u, UifxStyle style, float x, float y, const char *text, Vec4 color, float scale, float dur);
void uifx_update(Uifx *u, float dt);
void uifx_draw(const Uifx *u, Gfx *g);
// Project a world position to UI pixels with a view-projection matrix; returns false if behind the camera.
bool uifx_project(Mat4 view_proj, Vec3 world, float *out_x, float *out_y);
