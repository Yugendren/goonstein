#include "editor.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Endesga 32 palette (a widely used free palette), plus transparent-black at index 0 for erasing.
static const uint32_t PALETTE[33] = { 0x00000000,
    0xff2f4abe, 0xff4376d7, 0xffaad4ea, 0xff72a6e4, 0xff506fb8, 0xff393e73, 0xff31273e, 0xff3326a2,
    0xff443be4, 0xff2276f7, 0xff34aefe, 0xff61e7fe, 0xff4dc763, 0xff48893e, 0xff425c26, 0xff3e3c19,
    0xff894e12, 0xffdb9900, 0xfff5e82c, 0xffffffff, 0xffdccbc0, 0xffb49b8b, 0xff88695a, 0xff66443a,
    0xff442b26, 0xff251418, 0xff4400ff, 0xff6c3868, 0xff8850b5, 0xff7a75f6, 0xff96b7e8, 0xff6985c2 };

static const char *ANIM_PRESETS[] = { "idle", "walk", "attack", "hit", "dead", "roll", "cast", "kneel", "cheer", "run", "guard", "special" };
#define N_PRESETS 12

// Clipboard for the select tool's Ctrl+C / Ctrl+V. The only persistent storage this file adds
// beyond the Editor struct itself; everything else is either a fixed struct member or a static
// scratch buffer local to a function (same pattern as flood()'s stack below).
static PixFrame clip_buf; static int clip_w = 0, clip_h = 0; static bool clip_has = false;
static int clip_orig_x = 0, clip_orig_y = 0;

static void say(Editor *e, const char *s) { snprintf(e->msg, sizeof e->msg, "%s", s); e->msg_t = 2.5f; }

static PixAnim *cur_anim(Editor *e) { return (e->anim >= 0 && e->anim < e->doc.nanims) ? &e->doc.anims[e->anim] : NULL; }
static PixFrame *cur_frame(Editor *e) { PixAnim *a = cur_anim(e); return a ? pix_frame(a, e->dir < a->ndirs ? e->dir : 0, e->frame) : NULL; }

static void layout(Editor *e) {
    PixAnim *a = cur_anim(e);
    int fw = a ? a->fw : e->def_fw, fh = a ? a->fh : e->def_fh;
    int zx = 600 / fw, zy = 600 / fh;
    e->zoom = zx < zy ? zx : zy; if (e->zoom < 1) e->zoom = 1;
    e->cx = 40 + (600 - fw * e->zoom) / 2; e->cy = 110 + (600 - fh * e->zoom) / 2;
}

bool editor_init(Editor *e, const char *name, int frame_size) {
    memset(e, 0, sizeof *e);
    e->def_fw = e->def_fh = frame_size > 0 ? frame_size : 32;
    if (!pix_doc_load(&e->doc, HOLLOW_ASSET_DIR, name)) {
        pix_doc_init(&e->doc, name);
        pix_anim_add(&e->doc, "idle", e->def_fw, e->def_fh, 4, 2, 5, true);
        pix_anim_add(&e->doc, "walk", e->def_fw, e->def_fh, 4, 4, 9, true);
        pix_anim_add(&e->doc, "attack", e->def_fw, e->def_fh, 4, 4, 12, false);
        e->doc.anims[2].contact[0] = 2; e->doc.anims[2].ncontact = 1;
        say(e, "new character: idle, walk, attack");
    } else say(e, "loaded");
    e->anim = 0; e->dir = 0; e->frame = 0; e->tool = TOOL_PENCIL; e->pal = 20; e->color = PALETTE[20];
    e->onion = true; e->grid = true; e->mirror_x = false; e->hover_x = e->hover_y = -1;
    e->brush_size = 1;
    layout(e);
    return true;
}

void editor_shutdown(Editor *e) { pix_doc_free(&e->doc); }

// ---------------------------------------------------------------- undo

static void snapshot(Editor *e) {
    PixFrame *f = cur_frame(e); if (!f) return;
    if (e->undo_n == ED_UNDO) { memmove(e->undo, e->undo + 1, (ED_UNDO - 1) * sizeof *e->undo); e->undo_n--; }
    EdSnapshot *s = &e->undo[e->undo_n++];
    s->anim = e->anim; s->dir = e->dir; s->frame = e->frame; s->px = *f; s->valid = true;
    e->redo_n = 0;
}
static void restore(Editor *e, EdSnapshot *s, EdSnapshot *push_to, int *push_n) {
    if (s->anim >= e->doc.nanims) return;
    e->anim = s->anim; e->dir = s->dir; e->frame = s->frame;
    PixFrame *f = cur_frame(e); if (!f) return;
    EdSnapshot back = { e->anim, e->dir, e->frame, *f, true };
    if (*push_n < ED_UNDO) push_to[(*push_n)++] = back;
    *f = s->px; e->dirty = true;
}
static void undo(Editor *e) { if (e->undo_n == 0) { say(e, "nothing to undo"); return; } EdSnapshot s = e->undo[--e->undo_n]; restore(e, &s, e->redo, &e->redo_n); }
static void redo(Editor *e) { if (e->redo_n == 0) return; EdSnapshot s = e->redo[--e->redo_n]; restore(e, &s, e->undo, &e->undo_n); }

// ---------------------------------------------------------------- painting

static void put_px(Editor *e, PixFrame *f, int x, int y, uint32_t c) {
    PixAnim *a = cur_anim(e);
    if (x < 0 || y < 0 || x >= a->fw || y >= a->fh) return;
    f->px[y * PIX_MAX_SIZE + x] = c;
    if (e->mirror_x) { int mx = a->fw - 1 - x; if (mx != x) f->px[y * PIX_MAX_SIZE + mx] = c; }
}
// Filled square footprint of the current brush size, centred on (cx, cy). Odd sizes are centred;
// even sizes are offset toward +x/+y (so a size-2 brush covers cx, cx+1).
static void put_square(Editor *e, PixFrame *f, int cx, int cy, uint32_t c) {
    int s = e->brush_size < 1 ? 1 : e->brush_size, half = (s - 1) / 2;
    for (int y = cy - half; y < cy - half + s; y++) for (int x = cx - half; x < cx - half + s; x++) put_px(e, f, x, y, c);
}
static void plot_line(Editor *e, PixFrame *f, int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) { put_px(e, f, x0, y0, c); if (x0 == x1 && y0 == y1) break; int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } }
}
// Same interpolated Bresenham walk, but stamping the brush-size square at every step.
static void plot_line_sq(Editor *e, PixFrame *f, int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) { put_square(e, f, x0, y0, c); if (x0 == x1 && y0 == y1) break; int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } }
}
static void flood(Editor *e, PixFrame *f, int x, int y, uint32_t c) {
    PixAnim *a = cur_anim(e);
    if (x < 0 || y < 0 || x >= a->fw || y >= a->fh) return;
    uint32_t target = f->px[y * PIX_MAX_SIZE + x];
    if (target == c) return;
    static int stack[PIX_MAX_SIZE * PIX_MAX_SIZE * 2]; int n = 0;
    stack[n++] = x; stack[n++] = y;
    while (n > 0) {
        int py = stack[--n], px = stack[--n];
        if (px < 0 || py < 0 || px >= a->fw || py >= a->fh) continue;
        if (f->px[py * PIX_MAX_SIZE + px] != target) continue;
        f->px[py * PIX_MAX_SIZE + px] = c;
        if (n + 8 < (int)(sizeof stack / sizeof *stack)) { stack[n++] = px + 1; stack[n++] = py; stack[n++] = px - 1; stack[n++] = py; stack[n++] = px; stack[n++] = py + 1; stack[n++] = px; stack[n++] = py - 1; }
    }
}
static void nudge(Editor *e, int dx, int dy) {
    PixFrame *f = cur_frame(e); PixAnim *a = cur_anim(e); if (!f) return;
    snapshot(e);
    PixFrame tmp = {0};
    for (int y = 0; y < a->fh; y++) for (int x = 0; x < a->fw; x++) {
        int sx = x - dx, sy = y - dy;
        tmp.px[y * PIX_MAX_SIZE + x] = (sx >= 0 && sy >= 0 && sx < a->fw && sy < a->fh) ? f->px[sy * PIX_MAX_SIZE + sx] : 0;
    }
    *f = tmp; e->dirty = true;
}
static void flip_into_opposite(Editor *e) {
    PixAnim *a = cur_anim(e); if (!a || a->ndirs < 4) { say(e, "needs a 4-direction anim"); return; }
    int from = e->dir, to = from == 2 ? 3 : from == 3 ? 2 : -1;
    if (to < 0) { say(e, "flip works between left and right"); return; }
    PixFrame *src = pix_frame(a, from, e->frame), *dst = pix_frame(a, to, e->frame);
    for (int y = 0; y < a->fh; y++) for (int x = 0; x < a->fw; x++) dst->px[y * PIX_MAX_SIZE + (a->fw - 1 - x)] = src->px[y * PIX_MAX_SIZE + x];
    e->dirty = true; say(e, from == 2 ? "left mirrored into right" : "right mirrored into left");
}
static void copy_to_all_dirs(Editor *e) {
    PixAnim *a = cur_anim(e); if (!a) return;
    PixFrame *src = pix_frame(a, e->dir, e->frame);
    for (int d = 0; d < a->ndirs; d++) if (d != e->dir) *pix_frame(a, d, e->frame) = *src;
    e->dirty = true; say(e, "frame copied to every direction");
}

// ---------------------------------------------------------------- shading (lighten / darken)

static uint32_t shade_pixel(uint32_t c, bool lighten) {
    int r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255, a = (c >> 24) & 255;
    if (lighten) { r += (int)((255 - r) * 0.12f + 0.5f); g += (int)((255 - g) * 0.12f + 0.5f); b += (int)((255 - b) * 0.12f + 0.5f); }
    else { r = (int)(r * 0.88f + 0.5f); g = (int)(g * 0.88f + 0.5f); b = (int)(b * 0.88f + 0.5f); }
    if (r < 0) r = 0; if (r > 255) r = 255; if (g < 0) g = 0; if (g > 255) g = 255; if (b < 0) b = 0; if (b > 255) b = 255;
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}
// Never touches transparent pixels; skips anything already visited this stroke (e->shade_visited).
static void shade_one(Editor *e, PixFrame *f, PixAnim *a, int x, int y, bool lighten) {
    if (x < 0 || y < 0 || x >= a->fw || y >= a->fh) return;
    int idx = y * PIX_MAX_SIZE + x;
    if (!e->shade_visited[idx]) {
        uint32_t c = f->px[idx];
        if (c >> 24) { e->shade_visited[idx] = true; f->px[idx] = shade_pixel(c, lighten); }
    }
    if (e->mirror_x) {
        int mx = a->fw - 1 - x; if (mx == x) return;
        int midx = y * PIX_MAX_SIZE + mx;
        if (!e->shade_visited[midx]) {
            uint32_t mc = f->px[midx];
            if (mc >> 24) { e->shade_visited[midx] = true; f->px[midx] = shade_pixel(mc, lighten); }
        }
    }
}
static void shade_square(Editor *e, PixFrame *f, PixAnim *a, int cx, int cy, bool lighten) {
    int s = e->brush_size < 1 ? 1 : e->brush_size, half = (s - 1) / 2;
    for (int y = cy - half; y < cy - half + s; y++) for (int x = cx - half; x < cx - half + s; x++) shade_one(e, f, a, x, y, lighten);
}
static void shade_line(Editor *e, PixFrame *f, PixAnim *a, int x0, int y0, int x1, int y1, bool lighten) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) { shade_square(e, f, a, x0, y0, lighten); if (x0 == x1 && y0 == y1) break; int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } }
}

// ---------------------------------------------------------------- rectangle / ellipse

static void draw_rect_shape(Editor *e, PixFrame *f, PixAnim *a, int x0, int y0, int x1, int y1, uint32_t c, bool filled) {
    (void)a;
    int lx = x0 < x1 ? x0 : x1, hx = x0 < x1 ? x1 : x0, ly = y0 < y1 ? y0 : y1, hy = y0 < y1 ? y1 : y0;
    if (filled) { for (int y = ly; y <= hy; y++) for (int x = lx; x <= hx; x++) put_px(e, f, x, y, c); }
    else {
        for (int x = lx; x <= hx; x++) { put_px(e, f, x, ly, c); put_px(e, f, x, hy, c); }
        for (int y = ly; y <= hy; y++) { put_px(e, f, lx, y, c); put_px(e, f, hx, y, c); }
    }
}
static bool ellipse_inside(float x, float y, float cx, float cy, float rx, float ry) {
    if (rx < 0.5f) rx = 0.5f; if (ry < 0.5f) ry = 0.5f;
    float dx = (x - cx) / rx, dy = (y - cy) / ry;
    return dx * dx + dy * dy <= 1.0f;
}
static void draw_ellipse_shape(Editor *e, PixFrame *f, PixAnim *a, int x0, int y0, int x1, int y1, uint32_t c, bool filled) {
    (void)a;
    int lx = x0 < x1 ? x0 : x1, hx = x0 < x1 ? x1 : x0, ly = y0 < y1 ? y0 : y1, hy = y0 < y1 ? y1 : y0;
    float ecx = (lx + hx) / 2.0f + 0.5f, ecy = (ly + hy) / 2.0f + 0.5f, erx = (hx - lx + 1) / 2.0f, ery = (hy - ly + 1) / 2.0f;
    for (int y = ly; y <= hy; y++) for (int x = lx; x <= hx; x++) {
        if (!ellipse_inside(x + 0.5f, y + 0.5f, ecx, ecy, erx, ery)) continue;
        bool edge = !ellipse_inside(x + 1.5f, y + 0.5f, ecx, ecy, erx, ery) || !ellipse_inside(x - 0.5f, y + 0.5f, ecx, ecy, erx, ery) ||
                    !ellipse_inside(x + 0.5f, y + 1.5f, ecx, ecy, erx, ery) || !ellipse_inside(x + 0.5f, y - 0.5f, ecx, ecy, erx, ery);
        if (filled || edge) put_px(e, f, x, y, c);
    }
}

// ---------------------------------------------------------------- rectangle select / move / clipboard

static void clear_selection(Editor *e) {
    PixFrame *f = cur_frame(e); if (!f || !e->sel_on) return;
    snapshot(e);
    for (int y = e->sel_y0; y <= e->sel_y1; y++) for (int x = e->sel_x0; x <= e->sel_x1; x++) f->px[y * PIX_MAX_SIZE + x] = 0;
    e->dirty = true; say(e, "selection cleared");
}
// Cuts the selected rectangle out of the frame and pastes it at the shifted location; a
// transparent source pixel leaves whatever was already at the destination alone.
static void move_selection(Editor *e, int dx, int dy) {
    if (!e->sel_on || (dx == 0 && dy == 0)) return;
    PixFrame *f = cur_frame(e); PixAnim *a = cur_anim(e); if (!f || !a) return;
    snapshot(e);
    static uint32_t buf[PIX_MAX_SIZE * PIX_MAX_SIZE];
    int w = e->sel_x1 - e->sel_x0 + 1, h = e->sel_y1 - e->sel_y0 + 1;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) buf[y * w + x] = f->px[(e->sel_y0 + y) * PIX_MAX_SIZE + (e->sel_x0 + x)];
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) f->px[(e->sel_y0 + y) * PIX_MAX_SIZE + (e->sel_x0 + x)] = 0;
    int nx0 = e->sel_x0 + dx, ny0 = e->sel_y0 + dy;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        uint32_t c = buf[y * w + x]; if (!(c >> 24)) continue;
        int dxp = nx0 + x, dyp = ny0 + y; if (dxp < 0 || dyp < 0 || dxp >= a->fw || dyp >= a->fh) continue;
        f->px[dyp * PIX_MAX_SIZE + dxp] = c;
    }
    e->sel_x0 = nx0; e->sel_y0 = ny0; e->sel_x1 = nx0 + w - 1; e->sel_y1 = ny0 + h - 1;
    e->dirty = true;
}
static void copy_selection(Editor *e) {
    if (!e->sel_on) { say(e, "nothing selected"); return; }
    PixFrame *f = cur_frame(e); if (!f) return;
    int w = e->sel_x1 - e->sel_x0 + 1, h = e->sel_y1 - e->sel_y0 + 1;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) clip_buf.px[y * PIX_MAX_SIZE + x] = f->px[(e->sel_y0 + y) * PIX_MAX_SIZE + (e->sel_x0 + x)];
    clip_w = w; clip_h = h; clip_has = true; clip_orig_x = e->sel_x0; clip_orig_y = e->sel_y0;
    say(e, "copied");
}
// Pastes at the mouse position when it is over the canvas, otherwise back where it was copied
// from. Works across frames / directions / anims since the clipboard is a plain static buffer.
static void paste_selection(Editor *e) {
    if (!clip_has) { say(e, "clipboard empty"); return; }
    PixFrame *f = cur_frame(e); PixAnim *a = cur_anim(e); if (!f || !a) return;
    int px0 = e->hover_x >= 0 ? e->hover_x : clip_orig_x;
    int py0 = e->hover_x >= 0 ? e->hover_y : clip_orig_y;
    snapshot(e);
    for (int y = 0; y < clip_h; y++) for (int x = 0; x < clip_w; x++) {
        int dx = px0 + x, dy = py0 + y; if (dx < 0 || dy < 0 || dx >= a->fw || dy >= a->fh) continue;
        f->px[dy * PIX_MAX_SIZE + dx] = clip_buf.px[y * PIX_MAX_SIZE + x];
    }
    e->sel_on = true; e->sel_x0 = px0; e->sel_y0 = py0; e->sel_x1 = px0 + clip_w - 1; e->sel_y1 = py0 + clip_h - 1;
    e->dirty = true; say(e, "pasted");
}

// ---------------------------------------------------------------- colour ramp

static uint32_t shade_darker(uint32_t c, float f) {
    int r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255, a = (c >> 24) & 255;
    float fr = r * f * 0.96f, fg = g * f * 0.98f, fb = b * f;
    fb += (255.0f - fb) * 0.08f;   // slight hue shift toward blue as it darkens
    int ir = (int)(fr + 0.5f), ig = (int)(fg + 0.5f), ib = (int)(fb + 0.5f);
    if (ir < 0) ir = 0; if (ir > 255) ir = 255; if (ig < 0) ig = 0; if (ig > 255) ig = 255; if (ib < 0) ib = 0; if (ib > 255) ib = 255;
    return ((uint32_t)a << 24) | ((uint32_t)ib << 16) | ((uint32_t)ig << 8) | (uint32_t)ir;
}
static uint32_t shade_lighter(uint32_t c, float t) {
    int r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255, a = (c >> 24) & 255;
    const int wr = 255, wg = 244, wb = 214;   // warm white
    int ir = (int)(r + (wr - r) * t + 0.5f), ig = (int)(g + (wg - g) * t + 0.5f), ib = (int)(b + (wb - b) * t + 0.5f);
    if (ir < 0) ir = 0; if (ir > 255) ir = 255; if (ig < 0) ig = 0; if (ig > 255) ig = 255; if (ib < 0) ib = 0; if (ib > 255) ib = 255;
    return ((uint32_t)a << 24) | ((uint32_t)ib << 16) | ((uint32_t)ig << 8) | (uint32_t)ir;
}
// Index 0..2 = darker (darkest first), 3 = the colour itself, 4..6 = lighter (toward warm white).
static uint32_t ramp_color(uint32_t base, int i) {
    switch (i) {
    case 0: return shade_darker(base, 0.5f);
    case 1: return shade_darker(base, 0.64f);
    case 2: return shade_darker(base, 0.8f);
    case 3: return base;
    case 4: return shade_lighter(base, 0.3f);
    case 5: return shade_lighter(base, 0.5f);
    case 6: return shade_lighter(base, 0.7f);
    default: return base;
    }
}

// ---------------------------------------------------------------- ui geometry

#define PANEL_X 680.0f
#define N_TOOLS 10
static bool inside(float px, float py, float x, float y, float w, float h) { return px >= x && px <= x + w && py >= y && py <= y + h; }
static Vec4 col4(uint32_t c) { return v4((c & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, ((c >> 16) & 255) / 255.0f, ((c >> 24) & 255) / 255.0f); }

typedef struct Btn { float x, y, w, h; } Btn;
static Btn btn_anim(int i) { return (Btn){ PANEL_X, 110 + i * 26.0f, 120, 24 }; }
static Btn btn_tool(int i) { return (Btn){ PANEL_X + 130 + i * 36.0f, 110, 34, 30 }; }
static float tool_row_right(void) { return PANEL_X + 130 + N_TOOLS * 36.0f; }
static Btn btn_size_minus(void) { return (Btn){ tool_row_right() + 8, 110, 22, 30 }; }
static Btn btn_size_plus(void)  { return (Btn){ tool_row_right() + 8 + 22 + 26, 110, 22, 30 }; }
static Btn btn_pal(int i) { return (Btn){ PANEL_X + 130 + (i % 11) * 34.0f, 150 + (i / 11) * 34.0f, 30, 30 }; }
static Btn btn_ramp(int i) { return (Btn){ PANEL_X + 130 + i * 34.0f, 250, 30, 26 }; }
static Btn btn_dir(int i) { return (Btn){ PANEL_X + 130 + i * 60.0f, 292, 56, 26 }; }
static Btn btn_frame(int i) { return (Btn){ PANEL_X + i * 72.0f, 360, 68, 68 }; }
static Btn btn_action(int i) { return (Btn){ PANEL_X + (i % 4) * 140.0f, 450 + (i / 4) * 34.0f, 134, 30 }; }
static const char *ACTIONS[] = { "SAVE  ^S", "UNDO  ^Z", "REDO  ^Y", "PLAY  Spc", "+FRAME N", "DUP   D", "-FRAME X", "CONTACT K",
                                 "ONION O", "MIRROR M", "GRID  H", "COPY>ALL C", "FLIP L<>R F", "+ANIM  A", "-ANIM", "FPS -/+" };
#define N_ACTIONS 16
static const char *TOOL_LABELS[N_TOOLS] = { "PEN", "ERS", "FIL", "PIK", "LIN", "REC", "ELL", "SEL", "LIT", "DRK" };
static const char *TOOL_NAMES[N_TOOLS] = { "PENCIL", "ERASER", "FILL", "PICK", "LINE", "RECT", "ELLIPSE", "SELECT", "LIGHTEN", "DARKEN" };

static void do_action(Editor *e, int i, const Input *in);

// ---------------------------------------------------------------- tick

static void demo_paint(Editor *e) {
    // A small ninja-like figure drawn programmatically so the round trip can be verified headlessly.
    for (int ai = 0; ai < e->doc.nanims; ai++) {
        PixAnim *a = &e->doc.anims[ai];
        for (int d = 0; d < a->ndirs; d++) for (int fr = 0; fr < a->nframes; fr++) {
            PixFrame *f = pix_frame(a, d, fr);
            int cx = a->fw / 2, base = a->fh - 2, bob = (fr % 2) ? 1 : 0;
            uint32_t skin = PALETTE[4], cloth = PALETTE[13], dark = PALETTE[26], eye = PALETTE[20];
            for (int y = base - 12 - bob; y < base - 6 - bob; y++) for (int x = cx - 4; x < cx + 4; x++) f->px[y * PIX_MAX_SIZE + x] = cloth;   // torso
            for (int y = base - 18 - bob; y < base - 12 - bob; y++) for (int x = cx - 3; x < cx + 3; x++) f->px[y * PIX_MAX_SIZE + x] = skin;    // head
            if (d != 1) { int ex = d == 2 ? cx - 2 : d == 3 ? cx + 1 : cx - 2; f->px[(base - 15 - bob) * PIX_MAX_SIZE + ex] = eye; if (d == 0) f->px[(base - 15 - bob) * PIX_MAX_SIZE + cx + 1] = eye; }
            int step = (fr % 2) ? 2 : 0;
            for (int y = base - 6 - bob; y < base; y++) { f->px[y * PIX_MAX_SIZE + cx - 3 + (y > base - 3 ? step : 0)] = dark; f->px[y * PIX_MAX_SIZE + cx + 2 - (y > base - 3 ? step : 0)] = dark; }
            if (!strcmp(a->name, "attack")) { int reach = fr * 2; for (int x = cx + 4; x < cx + 4 + reach && x < a->fw; x++) f->px[(base - 9) * PIX_MAX_SIZE + x] = PALETTE[20]; }
        }
    }
    e->dirty = true;
}

void editor_tick(Editor *e, const Input *in, float mx, float my, float dt) {
    PixAnim *a = cur_anim(e);
    { static bool demo_done = false; if (!demo_done && SDL_getenv("HOLLOW_EDIT_DEMO")) { demo_done = true; demo_paint(e); do_action(e, 0, in); } }
    if (e->msg_t > 0) e->msg_t -= dt;
    e->ants_t += dt;
    layout(e);
    if (a && e->frame >= a->nframes) e->frame = a->nframes - 1;
    if (a && e->dir >= a->ndirs) e->dir = 0;

    // Playback
    if (e->playing && a) { e->play_t += dt * a->fps; if (e->play_t >= a->nframes) e->play_t = a->loop ? fmodf(e->play_t, (float)a->nframes) : (float)(a->nframes - 1); }

    // Keys (plain letters only; with Ctrl/Cmd they belong to the game's shortcuts)
    if (!in->ctrl) {
    if (in->key_down[SDL_SCANCODE_B]) e->tool = TOOL_PENCIL;
    if (in->key_down[SDL_SCANCODE_E]) e->tool = TOOL_ERASER;
    if (in->key_down[SDL_SCANCODE_G]) e->tool = TOOL_FILL;
    if (in->key_down[SDL_SCANCODE_I]) e->tool = TOOL_PICK;
    if (in->key_down[SDL_SCANCODE_L]) e->tool = TOOL_LINE;
    if (in->key_down[SDL_SCANCODE_U]) e->tool = TOOL_RECT;
    if (in->key_down[SDL_SCANCODE_Y]) e->tool = TOOL_ELLIPSE;
    if (in->key_down[SDL_SCANCODE_S]) e->tool = TOOL_SELECT;
    if (in->key_down[SDL_SCANCODE_SEMICOLON]) e->tool = TOOL_LIGHTEN;
    if (in->key_down[SDL_SCANCODE_APOSTROPHE]) e->tool = TOOL_DARKEN;
    if (in->key_down[SDL_SCANCODE_COMMA]) e->brush_size = e->brush_size > 1 ? e->brush_size - 1 : 1;
    if (in->key_down[SDL_SCANCODE_PERIOD]) e->brush_size = e->brush_size < 4 ? e->brush_size + 1 : 4;
    if (in->key_down[SDL_SCANCODE_RETURN]) { e->sel_on = e->sel_creating = e->sel_moving = false; }
    if (in->key_down[SDL_SCANCODE_O]) e->onion = !e->onion;
    if (in->key_down[SDL_SCANCODE_M]) { e->mirror_x = !e->mirror_x; say(e, e->mirror_x ? "mirror on" : "mirror off"); }
    if (in->key_down[SDL_SCANCODE_H]) e->grid = !e->grid;
    if (in->key_down[SDL_SCANCODE_SPACE]) { e->playing = !e->playing; e->play_t = 0; }
    if (in->key_down[SDL_SCANCODE_LEFTBRACKET] && a) e->frame = (e->frame + a->nframes - 1) % a->nframes;
    if (in->key_down[SDL_SCANCODE_RIGHTBRACKET] && a) e->frame = (e->frame + 1) % a->nframes;
    if (in->key_down[SDL_SCANCODE_N]) do_action(e, 4, in);
    if (in->key_down[SDL_SCANCODE_D]) do_action(e, 5, in);
    if (in->key_down[SDL_SCANCODE_X]) { if (e->sel_on) clear_selection(e); else do_action(e, 6, in); }
    if (in->key_down[SDL_SCANCODE_DELETE] && e->sel_on) clear_selection(e);
    if (in->key_down[SDL_SCANCODE_K]) do_action(e, 7, in);
    if (in->key_down[SDL_SCANCODE_C]) copy_to_all_dirs(e);
    if (in->key_down[SDL_SCANCODE_F]) flip_into_opposite(e);
    if (in->key_down[SDL_SCANCODE_A] && !in->ctrl) do_action(e, 13, in);
    if (in->key_down[SDL_SCANCODE_1]) e->dir = 0; if (in->key_down[SDL_SCANCODE_2]) e->dir = 1;
    if (in->key_down[SDL_SCANCODE_3]) e->dir = 2; if (in->key_down[SDL_SCANCODE_4]) e->dir = 3;
    if (in->key_down[SDL_SCANCODE_UP]) { if (e->sel_on) move_selection(e, 0, -1); else nudge(e, 0, -1); }
    if (in->key_down[SDL_SCANCODE_DOWN]) { if (e->sel_on) move_selection(e, 0, 1); else nudge(e, 0, 1); }
    if (in->key_down[SDL_SCANCODE_LEFT]) { if (e->sel_on) move_selection(e, -1, 0); else nudge(e, -1, 0); }
    if (in->key_down[SDL_SCANCODE_RIGHT]) { if (e->sel_on) move_selection(e, 1, 0); else nudge(e, 1, 0); }
    if (in->key_down[SDL_SCANCODE_MINUS] && a) { a->fps = fmaxf(1, a->fps - 1); e->dirty = true; }
    if (in->key_down[SDL_SCANCODE_EQUALS] && a) { a->fps = fminf(30, a->fps + 1); e->dirty = true; }
    }
    if (in->ctrl && in->key_down[SDL_SCANCODE_S]) do_action(e, 0, in);
    if (in->ctrl && in->key_down[SDL_SCANCODE_Z]) { if (in->shift_held) redo(e); else undo(e); }
    if (in->ctrl && in->key_down[SDL_SCANCODE_Y]) redo(e);
    if (in->wheel != 0 && a) { e->pal += in->wheel > 0 ? -1 : 1; if (e->pal < 0) e->pal = 32; if (e->pal > 32) e->pal = 0; e->color = PALETTE[e->pal]; }

    // Mouse over canvas
    e->hover_x = e->hover_y = -1;
    if (a && inside(mx, my, (float)e->cx, (float)e->cy, (float)(a->fw * e->zoom), (float)(a->fh * e->zoom))) {
        e->hover_x = (int)((mx - e->cx) / e->zoom); e->hover_y = (int)((my - e->cy) / e->zoom);
    }
    // Clamped canvas pixel for the current mouse position, valid even off-canvas (used while dragging).
    int cpx = 0, cpy = 0;
    if (a) {
        cpx = (int)floorf((mx - e->cx) / e->zoom); cpy = (int)floorf((my - e->cy) / e->zoom);
        if (cpx < 0) cpx = 0; if (cpy < 0) cpy = 0; if (cpx >= a->fw) cpx = a->fw - 1; if (cpy >= a->fh) cpy = a->fh - 1;
    }

    if (in->ctrl && in->key_down[SDL_SCANCODE_C]) copy_selection(e);
    if (in->ctrl && in->key_down[SDL_SCANCODE_V]) paste_selection(e);

    PixFrame *f = cur_frame(e);
    bool lmb = in->mouse_held, rmb = in->rmouse_held;
    bool lmb_release = e->lmb_prev && !lmb;

    // Right button: drag erases (brush-size square, same as the eraser); a plain click (release
    // within 8px of the press) picks the colour under the cursor instead, overriding the tool.
    if (in->rclick) { e->rmb_press_mx = mx; e->rmb_press_my = my; e->rmb_moved = false; }
    if (rmb && !e->rmb_moved) { float ddx = mx - e->rmb_press_mx, ddy = my - e->rmb_press_my; if (ddx * ddx + ddy * ddy > 64.0f) e->rmb_moved = true; }
    bool rmb_paint = rmb && e->rmb_moved;
    if (!rmb && e->rmb_prev && !e->rmb_moved && f && e->hover_x >= 0) {
        uint32_t p = f->px[e->hover_y * PIX_MAX_SIZE + e->hover_x];
        e->color = p; e->pal = -1; for (int i = 0; i < 33; i++) if (PALETTE[i] == p) e->pal = i;
        say(e, "picked colour");
    }
    if (f && a && e->hover_x >= 0 && rmb_paint) {
        if (!e->rstroke_open) { e->rstroke_open = true; snapshot(e); put_square(e, f, e->hover_x, e->hover_y, 0); e->rlast_x = e->hover_x; e->rlast_y = e->hover_y; e->dirty = true; }
        else if (e->hover_x != e->rlast_x || e->hover_y != e->rlast_y) { plot_line_sq(e, f, e->rlast_x, e->rlast_y, e->hover_x, e->hover_y, 0); e->rlast_x = e->hover_x; e->rlast_y = e->hover_y; e->dirty = true; }
    }
    if (!rmb) e->rstroke_open = false;
    e->rmb_prev = rmb;

    // Left button: dispatches on the active tool.
    if (f && a && e->hover_x >= 0 && lmb) {
        if (e->tool == TOOL_RECT || e->tool == TOOL_ELLIPSE) {
            if (!e->shape_drag) { e->shape_drag = true; e->shape_x0 = e->hover_x; e->shape_y0 = e->hover_y; }
            e->shape_x1 = cpx; e->shape_y1 = cpy; e->shape_filled = in->shift_held;
        } else if (e->tool == TOOL_SELECT) {
            if (!e->stroke_open) {
                e->stroke_open = true;
                if (e->sel_on && e->hover_x >= e->sel_x0 && e->hover_x <= e->sel_x1 && e->hover_y >= e->sel_y0 && e->hover_y <= e->sel_y1) {
                    e->sel_moving = true; e->sel_move_press_x = e->hover_x; e->sel_move_press_y = e->hover_y; e->sel_move_dx = e->sel_move_dy = 0;
                } else {
                    e->sel_creating = true; e->sel_on = false; e->sel_start_x = e->hover_x; e->sel_start_y = e->hover_y;
                    e->sel_x0 = e->sel_x1 = e->hover_x; e->sel_y0 = e->sel_y1 = e->hover_y;
                }
            } else if (e->sel_moving) {
                e->sel_move_dx = cpx - e->sel_move_press_x; e->sel_move_dy = cpy - e->sel_move_press_y;
            } else if (e->sel_creating) {
                int x0 = e->sel_start_x, y0 = e->sel_start_y, x1 = cpx, y1 = cpy;
                e->sel_x0 = x0 < x1 ? x0 : x1; e->sel_x1 = x0 < x1 ? x1 : x0; e->sel_y0 = y0 < y1 ? y0 : y1; e->sel_y1 = y0 < y1 ? y1 : y0;
            }
        } else {
            uint32_t c = e->tool == TOOL_ERASER ? 0 : e->color;
            if (!e->stroke_open) {
                e->stroke_open = true;
                if (e->tool == TOOL_LIGHTEN || e->tool == TOOL_DARKEN) memset(e->shade_visited, 0, sizeof e->shade_visited);
                if (e->tool == TOOL_PICK) { uint32_t p = f->px[e->hover_y * PIX_MAX_SIZE + e->hover_x]; if (p) { e->color = p; e->pal = -1; for (int i = 0; i < 33; i++) if (PALETTE[i] == p) e->pal = i; } }
                else if (e->tool == TOOL_FILL) { snapshot(e); flood(e, f, e->hover_x, e->hover_y, c); e->dirty = true; }
                else if (e->tool == TOOL_LINE) { if (!e->line_armed) { e->line_armed = true; e->line_x0 = e->hover_x; e->line_y0 = e->hover_y; } else { snapshot(e); plot_line(e, f, e->line_x0, e->line_y0, e->hover_x, e->hover_y, c); e->line_armed = false; e->dirty = true; } }
                else if (e->tool == TOOL_LIGHTEN || e->tool == TOOL_DARKEN) { snapshot(e); shade_square(e, f, a, e->hover_x, e->hover_y, e->tool == TOOL_LIGHTEN); e->dirty = true; }
                else { snapshot(e); put_square(e, f, e->hover_x, e->hover_y, c); e->dirty = true; }
                e->last_x = e->hover_x; e->last_y = e->hover_y;
            } else if (e->tool == TOOL_PENCIL || e->tool == TOOL_ERASER) {
                if (e->hover_x != e->last_x || e->hover_y != e->last_y) { plot_line_sq(e, f, e->last_x, e->last_y, e->hover_x, e->hover_y, c); e->last_x = e->hover_x; e->last_y = e->hover_y; e->dirty = true; }
            } else if (e->tool == TOOL_LIGHTEN || e->tool == TOOL_DARKEN) {
                if (e->hover_x != e->last_x || e->hover_y != e->last_y) { shade_line(e, f, a, e->last_x, e->last_y, e->hover_x, e->hover_y, e->tool == TOOL_LIGHTEN); e->last_x = e->hover_x; e->last_y = e->hover_y; e->dirty = true; }
            }
        }
    }
    if (!lmb) e->stroke_open = false;

    // Release: commit whatever drag was in progress.
    if (lmb_release) {
        if (e->shape_drag) {
            snapshot(e);
            if (e->tool == TOOL_RECT) draw_rect_shape(e, f, a, e->shape_x0, e->shape_y0, e->shape_x1, e->shape_y1, e->color, e->shape_filled);
            else if (e->tool == TOOL_ELLIPSE) draw_ellipse_shape(e, f, a, e->shape_x0, e->shape_y0, e->shape_x1, e->shape_y1, e->color, e->shape_filled);
            e->dirty = true; e->shape_drag = false;
        }
        if (e->sel_creating) { e->sel_creating = false; e->sel_on = true; }
        if (e->sel_moving) { move_selection(e, e->sel_move_dx, e->sel_move_dy); e->sel_moving = false; }
    }
    e->lmb_prev = lmb;

    // Clicks on the panel
    if (in->click) {
        for (int i = 0; i < e->doc.nanims; i++) { Btn b = btn_anim(i); if (inside(mx, my, b.x, b.y, b.w, b.h)) { e->anim = i; e->frame = 0; e->playing = false; } }
        for (int i = 0; i < N_TOOLS; i++) { Btn b = btn_tool(i); if (inside(mx, my, b.x, b.y, b.w, b.h)) e->tool = (EdTool)i; }
        { Btn bm = btn_size_minus(); if (inside(mx, my, bm.x, bm.y, bm.w, bm.h)) e->brush_size = e->brush_size > 1 ? e->brush_size - 1 : 1; }
        { Btn bp = btn_size_plus(); if (inside(mx, my, bp.x, bp.y, bp.w, bp.h)) e->brush_size = e->brush_size < 4 ? e->brush_size + 1 : 4; }
        for (int i = 0; i < 33; i++) { Btn b = btn_pal(i); if (inside(mx, my, b.x, b.y, b.w, b.h)) { e->pal = i; e->color = PALETTE[i]; if (i == 0) e->tool = TOOL_ERASER; else if (e->tool == TOOL_ERASER) e->tool = TOOL_PENCIL; } }
        for (int i = 0; i < 7; i++) { Btn b = btn_ramp(i); if (inside(mx, my, b.x, b.y, b.w, b.h)) { e->color = ramp_color(e->color, i); e->pal = -1; for (int k = 0; k < 33; k++) if (PALETTE[k] == e->color) e->pal = k; } }
        if (a) for (int i = 0; i < a->ndirs; i++) { Btn b = btn_dir(i); if (inside(mx, my, b.x, b.y, b.w, b.h)) e->dir = i; }
        if (a) for (int i = 0; i < a->nframes && i < 8; i++) { Btn b = btn_frame(i); if (inside(mx, my, b.x, b.y, b.w, b.h)) e->frame = i; }
        for (int i = 0; i < N_ACTIONS; i++) { Btn b = btn_action(i); if (inside(mx, my, b.x, b.y, b.w, b.h)) do_action(e, i, in); }
    }
}

static void do_action(Editor *e, int i, const Input *in) {
    (void)in;
    PixAnim *a = cur_anim(e);
    switch (i) {
    case 0: if (pix_doc_save(&e->doc, HOLLOW_ASSET_DIR)) { e->dirty = false; char s[200]; snprintf(s, sizeof s, "saved assets/sprites/own/%s.txt and characters/%s.txt", e->doc.name, e->doc.name); say(e, s); audio_play(SND_BLIP, 0.5f, 1.4f); } else say(e, "save failed, see log"); break;
    case 1: undo(e); break;
    case 2: redo(e); break;
    case 3: e->playing = !e->playing; e->play_t = 0; break;
    case 4: if (a && pix_anim_insert_frame(a, e->frame + 1, false)) { e->frame++; e->dirty = true; } break;
    case 5: if (a && pix_anim_insert_frame(a, e->frame + 1, true)) { e->frame++; e->dirty = true; } break;
    case 6: if (a && pix_anim_delete_frame(a, e->frame)) { if (e->frame >= a->nframes) e->frame = a->nframes - 1; e->dirty = true; } else say(e, "keep at least one frame"); break;
    case 7: if (a) {   // toggle contact on this frame
        int found = -1; for (int k = 0; k < a->ncontact; k++) if (a->contact[k] == e->frame) found = k;
        if (found >= 0) { for (int k = found; k < a->ncontact - 1; k++) a->contact[k] = a->contact[k + 1]; a->ncontact--; say(e, "contact cleared"); }
        else if (a->ncontact < PIX_MAX_CONTACT) { a->contact[a->ncontact++] = e->frame; say(e, "contact frame set: the parry beat lands here"); }
        e->dirty = true; } break;
    case 8: e->onion = !e->onion; break;
    case 9: e->mirror_x = !e->mirror_x; break;
    case 10: e->grid = !e->grid; break;
    case 11: copy_to_all_dirs(e); break;
    case 12: flip_into_opposite(e); break;
    case 13: {   // add the next preset anim that does not exist yet
        for (int k = 0; k < N_PRESETS; k++) {
            int idx = (e->new_anim_pick + k) % N_PRESETS; bool exists = false;
            for (int j = 0; j < e->doc.nanims; j++) if (!strcmp(e->doc.anims[j].name, ANIM_PRESETS[idx])) exists = true;
            if (exists) continue;
            bool loop = !strcmp(ANIM_PRESETS[idx], "idle") || !strcmp(ANIM_PRESETS[idx], "walk") || !strcmp(ANIM_PRESETS[idx], "run");
            int fw = a ? a->fw : e->def_fw, fh = a ? a->fh : e->def_fh;
            int n = pix_anim_add(&e->doc, ANIM_PRESETS[idx], fw, fh, 4, 2, loop ? 8 : 10, loop);
            if (n >= 0) { e->anim = n; e->frame = 0; e->new_anim_pick = idx + 1; char s[96]; snprintf(s, sizeof s, "added %s (press A again for the next preset)", ANIM_PRESETS[idx]); say(e, s); e->dirty = true; }
            break;
        }
    } break;
    case 14: if (e->doc.nanims > 1) { pix_anim_remove(&e->doc, e->anim); if (e->anim >= e->doc.nanims) e->anim = e->doc.nanims - 1; e->frame = 0; e->dirty = true; say(e, "anim removed"); } break;
    case 15: if (a) { a->fps = a->fps >= 30 ? 4 : a->fps + 2; e->dirty = true; } break;
    }
}

// ---------------------------------------------------------------- draw

static void draw_frame_px_skip(Gfx *g, const PixAnim *a, const PixFrame *f, float x, float y, float z, float alpha, int skip_x0, int skip_y0, int skip_x1, int skip_y1) {
    for (int py = 0; py < a->fh; py++) for (int px = 0; px < a->fw; px++) {
        if (px >= skip_x0 && px <= skip_x1 && py >= skip_y0 && py <= skip_y1) continue;
        uint32_t c = f->px[py * PIX_MAX_SIZE + px];
        if (!(c >> 24)) continue;
        Vec4 col = col4(c); col.w *= alpha;
        gfx_ui_rect(g, x + px * z, y + py * z, z, z, col);
    }
}
static void draw_frame_px(Gfx *g, const PixAnim *a, const PixFrame *f, float x, float y, float z, float alpha) {
    draw_frame_px_skip(g, a, f, x, y, z, alpha, -1, -1, -1, -1);
}
static void draw_dashed_rect(Gfx *g, float x, float y, float w, float h, float phase, Vec4 c) {
    float dash = 6, period = 10;
    for (float px = -period + fmodf(phase, period); px < w; px += period) {
        float x0 = px < 0 ? 0 : px, x1 = px + dash > w ? w : px + dash;
        if (x1 > x0) { gfx_ui_rect(g, x + x0, y, x1 - x0, 2, c); gfx_ui_rect(g, x + x0, y + h - 2, x1 - x0, 2, c); }
    }
    for (float py = -period + fmodf(phase, period); py < h; py += period) {
        float y0 = py < 0 ? 0 : py, y1 = py + dash > h ? h : py + dash;
        if (y1 > y0) { gfx_ui_rect(g, x, y + y0, 2, y1 - y0, c); gfx_ui_rect(g, x + w - 2, y + y0, 2, y1 - y0, c); }
    }
}

void editor_draw(Editor *e, Gfx *g) {
    PixAnim *a = cur_anim(e);
    Vec4 white = v4(0.92f, 0.9f, 0.86f, 1), dim = v4(0.6f, 0.58f, 0.55f, 1), acc = v4(1, 0.85f, 0.4f, 1);
    gfx_ui_rect(g, 0, 0, 1280, 800, v4(0.09f, 0.09f, 0.11f, 1));
    // Title
    { char s[160]; snprintf(s, sizeof s, "SPRITE EDITOR   %s%s   %s  dir %s  frame %d/%d  fps %.0f%s", e->doc.name, e->dirty ? " *" : "", a ? a->name : "-",
        a ? (const char *[]){"down", "up", "left", "right"}[e->dir] : "-", e->frame + 1, a ? a->nframes : 0, a ? a->fps : 0.0f, a && a->loop ? " loop" : "");
      gfx_ui_text(g, 40, 30, 1.8f, white, s); }
    { char s[280]; snprintf(s, sizeof s, "tool %s%s  size %d (,/.)   B E G I L U Y S ; '   [ ] frames   1-4 dirs   arrows nudge/move sel   wheel colour   right-click pick, right-drag erase",
        TOOL_NAMES[e->tool], e->mirror_x ? " (mirror)" : "", e->brush_size);
      gfx_ui_text(g, 40, 60, 1.1f, dim, s); }

    if (a) {
        // Canvas: checkerboard, onion, pixels, grid, hover
        float z = (float)e->zoom, cx = (float)e->cx, cy = (float)e->cy;
        for (int py = 0; py < a->fh; py += 2) for (int px = 0; px < a->fw; px += 2) {
            gfx_ui_rect(g, cx + px * z, cy + py * z, z * 2, z * 2, v4(0.16f, 0.16f, 0.18f, 1));
            gfx_ui_rect(g, cx + px * z, cy + py * z, z, z, v4(0.2f, 0.2f, 0.23f, 1));
            gfx_ui_rect(g, cx + (px + 1) * z, cy + (py + 1) * z, z, z, v4(0.2f, 0.2f, 0.23f, 1));
        }
        if (e->onion && e->frame > 0) draw_frame_px(g, a, pix_frame(a, e->dir, e->frame - 1), cx, cy, z, 0.3f);
        PixFrame *curf = pix_frame(a, e->dir, e->frame);
        if (e->sel_moving) {
            draw_frame_px_skip(g, a, curf, cx, cy, z, 1.0f, e->sel_x0, e->sel_y0, e->sel_x1, e->sel_y1);
            for (int y = e->sel_y0; y <= e->sel_y1; y++) for (int x = e->sel_x0; x <= e->sel_x1; x++) {
                uint32_t c = curf->px[y * PIX_MAX_SIZE + x]; if (!(c >> 24)) continue;
                int dx = x + e->sel_move_dx, dy = y + e->sel_move_dy;
                Vec4 col = col4(c); col.w *= 0.9f;
                gfx_ui_rect(g, cx + dx * z, cy + dy * z, z, z, col);
            }
        } else draw_frame_px(g, a, curf, cx, cy, z, 1.0f);
        if (e->grid && e->zoom >= 6) {
            for (int px = 0; px <= a->fw; px += 8) gfx_ui_rect(g, cx + px * z - 1, cy, 1, a->fh * z, v4(1, 1, 1, 0.12f));
            for (int py = 0; py <= a->fh; py += 8) gfx_ui_rect(g, cx, cy + py * z - 1, a->fw * z, 1, v4(1, 1, 1, 0.12f));
            gfx_ui_rect(g, cx + a->fw / 2 * z - 1, cy, 1, a->fh * z, v4(1, 0.8f, 0.3f, 0.25f));
        }
        // Rectangle / ellipse live preview
        if (e->shape_drag && (e->tool == TOOL_RECT || e->tool == TOOL_ELLIPSE)) {
            Vec4 pc = col4(e->color); pc.w = 0.6f;
            int lx = e->shape_x0 < e->shape_x1 ? e->shape_x0 : e->shape_x1, hx = e->shape_x0 < e->shape_x1 ? e->shape_x1 : e->shape_x0;
            int ly = e->shape_y0 < e->shape_y1 ? e->shape_y0 : e->shape_y1, hy = e->shape_y0 < e->shape_y1 ? e->shape_y1 : e->shape_y0;
            if (e->tool == TOOL_RECT) {
                if (e->shape_filled) gfx_ui_rect(g, cx + lx * z, cy + ly * z, (hx - lx + 1) * z, (hy - ly + 1) * z, pc);
                else {
                    gfx_ui_rect(g, cx + lx * z, cy + ly * z, (hx - lx + 1) * z, z, pc); gfx_ui_rect(g, cx + lx * z, cy + hy * z, (hx - lx + 1) * z, z, pc);
                    gfx_ui_rect(g, cx + lx * z, cy + ly * z, z, (hy - ly + 1) * z, pc); gfx_ui_rect(g, cx + hx * z, cy + ly * z, z, (hy - ly + 1) * z, pc);
                }
            } else {
                float ecx = (lx + hx) / 2.0f + 0.5f, ecy = (ly + hy) / 2.0f + 0.5f, erx = (hx - lx + 1) / 2.0f, ery = (hy - ly + 1) / 2.0f;
                for (int y = ly; y <= hy; y++) for (int x = lx; x <= hx; x++) {
                    if (!ellipse_inside(x + 0.5f, y + 0.5f, ecx, ecy, erx, ery)) continue;
                    bool edge = !ellipse_inside(x + 1.5f, y + 0.5f, ecx, ecy, erx, ery) || !ellipse_inside(x - 0.5f, y + 0.5f, ecx, ecy, erx, ery) ||
                                !ellipse_inside(x + 0.5f, y + 1.5f, ecx, ecy, erx, ery) || !ellipse_inside(x + 0.5f, y - 0.5f, ecx, ecy, erx, ery);
                    if (e->shape_filled || edge) gfx_ui_rect(g, cx + x * z, cy + y * z, z, z, pc);
                }
            }
        }
        // Selection: marching ants
        if (e->sel_on || e->sel_creating) {
            float ox = e->sel_moving ? e->sel_move_dx * z : 0, oy = e->sel_moving ? e->sel_move_dy * z : 0;
            draw_dashed_rect(g, cx + e->sel_x0 * z + ox, cy + e->sel_y0 * z + oy, (e->sel_x1 - e->sel_x0 + 1) * z, (e->sel_y1 - e->sel_y0 + 1) * z, e->ants_t * 20.0f, v4(1, 1, 0.3f, 0.9f));
        }
        // Hover: brush footprint (widened for pencil/eraser/shade tools), line-tool armed marker
        if (e->hover_x >= 0) {
            int fx0 = e->hover_x, fy0 = e->hover_y, fs = 1;
            if (e->tool == TOOL_PENCIL || e->tool == TOOL_ERASER || e->tool == TOOL_LIGHTEN || e->tool == TOOL_DARKEN) {
                int half = (e->brush_size - 1) / 2; fx0 = e->hover_x - half; fy0 = e->hover_y - half; fs = e->brush_size;
            }
            float fxp = cx + fx0 * z, fyp = cy + fy0 * z, fwp = fs * z, fhp = fs * z;
            gfx_ui_rect(g, fxp, fyp, fwp, 2, v4(1, 1, 1, 0.8f)); gfx_ui_rect(g, fxp, fyp + fhp - 2, fwp, 2, v4(1, 1, 1, 0.8f));
            gfx_ui_rect(g, fxp, fyp, 2, fhp, v4(1, 1, 1, 0.8f)); gfx_ui_rect(g, fxp + fwp - 2, fyp, 2, fhp, v4(1, 1, 1, 0.8f));
            if (e->line_armed) gfx_ui_rect(g, cx + e->line_x0 * z, cy + e->line_y0 * z, z, z, v4(1, 0.5f, 0.2f, 0.5f));
        }
        // Frame border
        gfx_ui_rect(g, cx - 2, cy - 2, a->fw * z + 4, 2, dim); gfx_ui_rect(g, cx - 2, cy + a->fh * z, a->fw * z + 4, 2, dim);
        gfx_ui_rect(g, cx - 2, cy - 2, 2, a->fh * z + 4, dim); gfx_ui_rect(g, cx + a->fw * z, cy - 2, 2, a->fh * z + 4, dim);
    }

    // Anim list
    for (int i = 0; i < e->doc.nanims; i++) {
        Btn b = btn_anim(i); bool sel = i == e->anim;
        gfx_ui_rect(g, b.x, b.y, b.w, b.h, sel ? v4(0.3f, 0.27f, 0.2f, 1) : v4(0.16f, 0.16f, 0.19f, 1));
        char s[48]; snprintf(s, sizeof s, "%s %d", e->doc.anims[i].name, e->doc.anims[i].nframes);
        gfx_ui_text(g, b.x + 8, b.y + 8, 1.2f, sel ? acc : white, s);
    }
    // Tools
    for (int i = 0; i < N_TOOLS; i++) { Btn b = btn_tool(i); bool sel = (int)e->tool == i;
        gfx_ui_rect(g, b.x, b.y, b.w, b.h, sel ? v4(0.3f, 0.27f, 0.2f, 1) : v4(0.16f, 0.16f, 0.19f, 1));
        gfx_ui_text(g, b.x + 3, b.y + 10, 1.0f, sel ? acc : white, TOOL_LABELS[i]); }
    // Brush size stepper
    { Btn bm = btn_size_minus(), bp = btn_size_plus();
      gfx_ui_rect(g, bm.x, bm.y, bm.w, bm.h, v4(0.16f, 0.16f, 0.19f, 1)); gfx_ui_text(g, bm.x + 6, bm.y + 10, 1.1f, white, "-");
      char sz[4]; snprintf(sz, sizeof sz, "%d", e->brush_size); gfx_ui_text(g, bm.x + bm.w + 6, bm.y + 10, 1.1f, acc, sz);
      gfx_ui_rect(g, bp.x, bp.y, bp.w, bp.h, v4(0.16f, 0.16f, 0.19f, 1)); gfx_ui_text(g, bp.x + 6, bp.y + 10, 1.1f, white, "+"); }
    // Palette
    for (int i = 0; i < 33; i++) { Btn b = btn_pal(i);
        if (i == 0) { gfx_ui_rect(g, b.x, b.y, b.w, b.h, v4(0.2f, 0.2f, 0.23f, 1)); gfx_ui_rect(g, b.x + 6, b.y + 13, 18, 4, v4(0.8f, 0.3f, 0.3f, 1)); }
        else gfx_ui_rect(g, b.x, b.y, b.w, b.h, col4(PALETTE[i]));
        if (i == e->pal) { gfx_ui_rect(g, b.x - 3, b.y - 3, b.w + 6, 3, white); gfx_ui_rect(g, b.x - 3, b.y + b.h, b.w + 6, 3, white); gfx_ui_rect(g, b.x - 3, b.y - 3, 3, b.h + 6, white); gfx_ui_rect(g, b.x + b.w, b.y - 3, 3, b.h + 6, white); } }
    // Colour ramp: darker/lighter shades of the current colour, for consistent shading
    for (int i = 0; i < 7; i++) { Btn b = btn_ramp(i);
        gfx_ui_rect(g, b.x, b.y, b.w, b.h, col4(ramp_color(e->color, i)));
        if (i == 3) gfx_ui_rect(g, b.x - 2, b.y - 2, b.w + 4, 2, white); }
    // Directions
    if (a) for (int i = 0; i < a->ndirs; i++) { Btn b = btn_dir(i); bool sel = i == e->dir;
        gfx_ui_rect(g, b.x, b.y, b.w, b.h, sel ? v4(0.3f, 0.27f, 0.2f, 1) : v4(0.16f, 0.16f, 0.19f, 1));
        gfx_ui_text(g, b.x + 8, b.y + 8, 1.1f, sel ? acc : white, (const char *[]){"DOWN 1", "UP 2", "LEFT 3", "RIGHT 4"}[i]); }
    // Frames strip
    gfx_ui_text(g, PANEL_X, 342, 1.0f, dim, "FRAMES   ( N new  D dup  X delete (no sel)  K contact )");
    if (a) for (int i = 0; i < a->nframes && i < 8; i++) { Btn b = btn_frame(i); bool sel = i == e->frame;
        gfx_ui_rect(g, b.x, b.y, b.w, b.h, sel ? v4(0.3f, 0.27f, 0.2f, 1) : v4(0.14f, 0.14f, 0.17f, 1));
        float tz = 60.0f / (a->fw > a->fh ? a->fw : a->fh);
        draw_frame_px(g, a, pix_frame(a, e->dir, i), b.x + 4, b.y + 4, tz, 1.0f);
        bool contact = false; for (int k = 0; k < a->ncontact; k++) if (a->contact[k] == i) contact = true;
        if (contact) gfx_ui_rect(g, b.x + b.w - 14, b.y + 4, 10, 10, v4(1, 0.3f, 0.2f, 1));
        char s[8]; snprintf(s, sizeof s, "%d", i + 1); gfx_ui_text(g, b.x + 4, b.y + b.h - 12, 1.0f, dim, s); }
    // Actions
    for (int i = 0; i < N_ACTIONS; i++) { Btn b = btn_action(i);
        gfx_ui_rect(g, b.x, b.y, b.w, b.h, v4(0.16f, 0.16f, 0.19f, 1));
        bool on = (i == 8 && e->onion) || (i == 9 && e->mirror_x) || (i == 10 && e->grid) || (i == 3 && e->playing);
        gfx_ui_text(g, b.x + 8, b.y + 10, 1.1f, on ? acc : white, ACTIONS[i]); }
    // Playback preview at 4x and 1x
    if (a) {
        int pf = e->playing ? (int)e->play_t : e->frame; if (pf >= a->nframes) pf = a->nframes - 1;
        float px = PANEL_X, py = 590;
        gfx_ui_text(g, px, py - 16, 1.0f, dim, "PREVIEW  (Space plays)");
        gfx_ui_rect(g, px, py, a->fw * 4.0f + 8, a->fh * 4.0f + 8, v4(0.12f, 0.12f, 0.15f, 1));
        draw_frame_px(g, a, pix_frame(a, e->dir, pf), px + 4, py + 4, 4, 1.0f);
        gfx_ui_rect(g, px + a->fw * 4.0f + 20, py, a->fw + 8.0f, a->fh + 8.0f, v4(0.12f, 0.12f, 0.15f, 1));
        draw_frame_px(g, a, pix_frame(a, e->dir, pf), px + a->fw * 4.0f + 24, py + 4, 1, 1.0f);
        // all four directions of this frame, small
        for (int d = 0; d < a->ndirs; d++) draw_frame_px(g, a, pix_frame(a, d, pf), px + a->fw * 4.0f + 60 + d * (a->fw * 2.0f + 6), py + 4, 2, 1.0f);
    }
    if (e->msg_t > 0) gfx_ui_text(g, 40, 760, 1.3f, acc, e->msg);
    gfx_ui_text(g, 40, 780, 1.0f, dim, "Ctrl+S save  Ctrl+Z/Y undo/redo  Ctrl+C/V copy/paste sel  Enter deselect  Del/X clear sel  C copy to dirs  F mirror  A add anim  -/+ fps  Esc quit");
}

// ---------------------------------------------------------------- self-test
#ifdef EDITOR_TEST
#include <assert.h>

// gfx.h stubs: the test never renders, it only exercises the tool logic above.
void gfx_ui_rect(Gfx *g, float x, float y, float w, float h, Vec4 color) { (void)g; (void)x; (void)y; (void)w; (void)h; (void)color; }
void gfx_ui_text(Gfx *g, float x, float y, float scale, Vec4 color, const char *text) { (void)g; (void)x; (void)y; (void)scale; (void)color; (void)text; }
float gfx_ui_text_width(float scale, const char *text) { (void)scale; (void)text; return 0; }

void audio_play(SoundId id, float gain, float pitch) { (void)id; (void)gain; (void)pitch; }

static int count_opaque(const PixFrame *f) {
    int n = 0; for (int i = 0; i < PIX_MAX_SIZE * PIX_MAX_SIZE; i++) if (f->px[i] >> 24) n++; return n;
}

int main(void) {
    Editor e; memset(&e, 0, sizeof e);
    pix_doc_init(&e.doc, "test");
    int ai = pix_anim_add(&e.doc, "idle", 16, 16, 1, 1, 8, true);
    assert(ai == 0);
    e.anim = 0; e.dir = 0; e.frame = 0; e.color = 0xffffffffu; e.brush_size = 1;
    PixAnim *a = cur_anim(&e); PixFrame *f = cur_frame(&e);
    assert(a && f);

    // 1. Pencil with size 3 paints 9 pixels.
    memset(f->px, 0, sizeof f->px);
    e.brush_size = 3;
    put_square(&e, f, 8, 8, e.color);
    int cnt = count_opaque(f);
    assert(cnt == 9);
    printf("pencil size 3: %d pixels (expect 9) OK\n", cnt);

    // 2. Rectangle outline tool paints the right perimeter count for a 5x4 rect (14 pixels).
    memset(f->px, 0, sizeof f->px);
    draw_rect_shape(&e, f, a, 2, 2, 6, 5, e.color, false);   // x0..x1 = 2..6 (5 wide), y0..y1 = 2..5 (4 tall)
    cnt = count_opaque(f);
    assert(cnt == 14);
    printf("rect outline 5x4: %d pixels (expect 14) OK\n", cnt);

    // 3. Select-move moves 4 pixels by (2, 1).
    memset(f->px, 0, sizeof f->px);
    f->px[3 * PIX_MAX_SIZE + 3] = 0xffffffffu; f->px[3 * PIX_MAX_SIZE + 4] = 0xffffffffu;
    f->px[4 * PIX_MAX_SIZE + 3] = 0xffffffffu; f->px[4 * PIX_MAX_SIZE + 4] = 0xffffffffu;
    e.sel_on = true; e.sel_x0 = 3; e.sel_y0 = 3; e.sel_x1 = 4; e.sel_y1 = 4;
    move_selection(&e, 2, 1);
    assert(e.sel_x0 == 5 && e.sel_y0 == 4 && e.sel_x1 == 6 && e.sel_y1 == 5);
    cnt = count_opaque(f);
    assert(cnt == 4);
    assert((f->px[4 * PIX_MAX_SIZE + 5] >> 24) && (f->px[4 * PIX_MAX_SIZE + 6] >> 24) && (f->px[5 * PIX_MAX_SIZE + 5] >> 24) && (f->px[5 * PIX_MAX_SIZE + 6] >> 24));
    assert(!(f->px[3 * PIX_MAX_SIZE + 3] >> 24));
    printf("select-move: %d pixels moved by (2,1) OK\n", cnt);

    // 4. Lighten changes a pixel's value (and never touches transparent ones).
    memset(f->px, 0, sizeof f->px);
    uint32_t before = 0xff804020u;   // A=ff B=80 G=40 R=20
    f->px[5 * PIX_MAX_SIZE + 5] = before;
    memset(e.shade_visited, 0, sizeof e.shade_visited);
    shade_one(&e, f, a, 5, 5, true);
    uint32_t after = f->px[5 * PIX_MAX_SIZE + 5];
    assert(after != before);
    assert((after & 255) >= (before & 255));
    shade_one(&e, f, a, 6, 6, true);   // still transparent, must stay untouched
    assert(f->px[6 * PIX_MAX_SIZE + 6] == 0);
    printf("lighten: %08x -> %08x OK\n", before, after);

    // 5. Ramp swatch selection changes the colour.
    uint32_t base = 0xff2f4abeu;
    uint32_t swatch = ramp_color(base, 0);
    assert(swatch != base);
    assert(ramp_color(base, 3) == base);
    e.color = base; e.color = ramp_color(e.color, 5);
    assert(e.color != base);
    printf("ramp swatch: base %08x -> %08x OK\n", base, e.color);

    pix_doc_free(&e.doc);
    printf("ALL EDITOR TESTS PASSED\n");
    return 0;
}

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "pixio.c"
#endif
