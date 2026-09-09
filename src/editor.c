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

static void relayout(Editor *e);   // rebuilds e->lay from e->win_w / e->win_h (see "ui geometry")

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
    e->win_w = 1280; e->win_h = 800;                       // until editor_set_size says otherwise
    relayout(e);
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

#define N_TOOLS 10
#define N_ACTIONS 16
#define ED_PAD 12.0f
static bool inside(float px, float py, float x, float y, float w, float h) { return px >= x && px <= x + w && py >= y && py <= y + h; }
static Vec4 col4(uint32_t c) { return v4((c & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, ((c >> 16) & 255) / 255.0f, ((c >> 24) & 255) / 255.0f); }

static Btn btn_anim(const EdLayout *L, int i) { return (Btn){ L->anim_x + (i % L->anim_cols) * (L->anim_w + 4), L->anim_y + (i / L->anim_cols) * L->anim_h, L->anim_w, L->anim_h - 2 }; }
static Btn btn_tool(const EdLayout *L, int i) { return (Btn){ L->tool_x + (i % L->tool_cols) * L->tool_pitch, L->tool_y + (i / L->tool_cols) * L->tool_pitch, L->tool_sz, L->tool_sz }; }
static Btn btn_size_minus(const EdLayout *L) { return L->size_minus; }
static Btn btn_size_plus(const EdLayout *L)  { return L->size_plus; }
static Btn btn_pal(const EdLayout *L, int i) { return (Btn){ L->pal_x + (i % L->pal_cols) * L->pal_pitch, L->pal_y + (i / L->pal_cols) * L->pal_pitch, L->pal_sz, L->pal_sz }; }
static Btn btn_ramp(const EdLayout *L, int i) { return (Btn){ L->ramp_x + i * L->ramp_pitch, L->ramp_y, L->ramp_w, L->ramp_h }; }
static Btn btn_dir(const EdLayout *L, int i) { return (Btn){ L->dir_x + i * L->dir_pitch, L->dir_y, L->dir_w, L->dir_h }; }
static Btn btn_frame(const EdLayout *L, int i) { return (Btn){ L->frames_x + (i % L->frames_cols) * L->frames_pitch, L->frames_y + (i / L->frames_cols) * L->frames_pitch, L->frames_sz, L->frames_sz }; }
static Btn btn_action(const EdLayout *L, int i) { return (Btn){ L->act_x + (i % L->act_cols) * (L->act_w + 4), L->act_y + (i / L->act_cols) * L->act_pitch_y, L->act_w, L->act_h }; }
static const char *ACTIONS[] = { "SAVE ^S", "UNDO ^Z", "REDO ^Y", "PLAY Spc", "+FRAME N", "DUP D", "-FRAME X", "CONTACT K",
                                 "ONION O", "MIRROR M", "GRID H", "COPY>ALL C", "FLIP L<>R F", "+ANIM A", "-ANIM", "FPS -/+" };
// Same buttons without the shortcut hint, used when the full label will not fit the button.
static const char *ACTIONS_SHORT[] = { "SAVE", "UNDO", "REDO", "PLAY", "+FRAME", "DUP", "-FRAME", "CONTACT",
                                       "ONION", "MIRROR", "GRID", "COPY>ALL", "FLIP L<>R", "+ANIM", "-ANIM", "FPS -/+" };
static const char *TOOL_LABELS[N_TOOLS] = { "PEN", "ERS", "FIL", "PIK", "LIN", "REC", "ELL", "SEL", "LIT", "DRK" };
static const char *TOOL_NAMES[N_TOOLS] = { "PENCIL", "ERASER", "FILL", "PICK", "LINE", "RECT", "ELLIPSE", "SELECT", "LIGHTEN", "DARKEN" };

// Flows the right-hand panel top to bottom at density `d` (1 = roomy, lower = tighter) and
// returns the height everything above the preview needs. relayout() picks the largest density
// that fits the window; the preview then takes whatever is left.
static float panel_flow(Editor *e, EdLayout *L, float d) {
    PixAnim *a = cur_anim(e);
    float px = L->panel_x, pw = L->panel_w, y = L->panel_top;
    float g4 = floorf(4 * d), gap = floorf(8 * d), label_h = 18;

    // Animations: two columns when the panel is wide enough for them.
    L->anim_h = floorf(26 * d);
    L->anim_cols = pw >= 300 ? 2 : 1;
    L->anim_w = floorf((pw - (L->anim_cols - 1) * 4) / L->anim_cols);
    L->anim_x = px; L->anim_y = y;
    int arows = (e->doc.nanims + L->anim_cols - 1) / L->anim_cols;
    if (arows < 1) arows = 1;
    if (arows > 6) arows = 6;                                  // cap the list at what comfortably fits
    L->anim_max = arows * L->anim_cols;
    y += arows * L->anim_h + gap;

    // Tools, wrapping by panel width; the brush stepper rides the last row when there is room.
    L->tool_sz = floorf(34 * d); L->tool_pitch = L->tool_sz + g4;
    L->tool_cols = (int)((pw + g4) / L->tool_pitch);
    if (L->tool_cols < 1) L->tool_cols = 1; if (L->tool_cols > N_TOOLS) L->tool_cols = N_TOOLS;
    L->tool_x = px; L->tool_y = y;
    int trows = (N_TOOLS + L->tool_cols - 1) / L->tool_cols;
    float bw = floorf(22 * d), numw = floorf(26 * d), stepw = bw * 2 + numw + 8;
    int last_used = N_TOOLS - (trows - 1) * L->tool_cols;
    float sx = px + last_used * L->tool_pitch, sy = y + (trows - 1) * L->tool_pitch;
    if (px + pw - sx < stepw) { sx = px; sy = y + trows * L->tool_pitch; y += L->tool_sz + g4; }
    y += trows * L->tool_pitch - g4 + gap;
    L->size_minus = (Btn){ sx, sy, bw, L->tool_sz };
    L->size_val_x = sx + bw + 4;
    L->size_plus  = (Btn){ sx + bw + 4 + numw, sy, bw, L->tool_sz };

    // Palette
    L->pal_sz = floorf(30 * d); L->pal_pitch = L->pal_sz + g4;
    L->pal_cols = (int)((pw + g4) / L->pal_pitch); if (L->pal_cols < 1) L->pal_cols = 1;
    y += 3;                                                    // room for the selection outline
    L->pal_x = px; L->pal_y = y;
    int prows = (33 + L->pal_cols - 1) / L->pal_cols;
    y += prows * L->pal_pitch - g4 + 3 + gap;

    // Colour ramp
    L->ramp_h = floorf(26 * d); L->ramp_w = floorf(fminf(30 * d, (pw - 6 * g4) / 7));
    L->ramp_pitch = L->ramp_w + g4; L->ramp_x = px; L->ramp_y = y;
    y += L->ramp_h + gap;

    // Directions
    L->dir_h = floorf(26 * d); L->dir_w = floorf(fminf(90 * d, (pw - 3 * g4) / 4));
    L->dir_pitch = L->dir_w + g4; L->dir_x = px; L->dir_y = y;
    y += L->dir_h + gap;

    // Frames strip: at most two rows of thumbnails under a label.
    L->frames_label_y = y; y += label_h;
    L->frames_sz = floorf(68 * d); L->frames_pitch = L->frames_sz + g4;
    L->frames_cols = (int)((pw + g4) / L->frames_pitch); if (L->frames_cols < 1) L->frames_cols = 1;
    L->frames_max = L->frames_cols * 2; if (L->frames_max > 8) L->frames_max = 8;
    int nfr = a ? a->nframes : 1; if (nfr > L->frames_max) nfr = L->frames_max;
    int frows = (nfr + L->frames_cols - 1) / L->frames_cols; if (frows < 1) frows = 1;
    L->frames_x = px; L->frames_y = y;
    y += frows * L->frames_pitch - g4 + gap;

    // Actions
    L->act_h = floorf(30 * d); L->act_pitch_y = L->act_h + g4;
    float act_min = fmaxf(120 * d, 96);
    L->act_cols = (int)((pw + 4) / (act_min + 4));
    if (L->act_cols < 1) L->act_cols = 1; if (L->act_cols > 4) L->act_cols = 4;
    L->act_w = floorf((pw - (L->act_cols - 1) * 4) / L->act_cols);
    L->act_x = px; L->act_y = y;
    int nrows = (N_ACTIONS + L->act_cols - 1) / L->act_cols;
    y += nrows * L->act_pitch_y - g4 + gap;

    L->prev_label_y = y;
    return y + label_h - L->panel_top;
}

static void relayout(Editor *e) {
    EdLayout *L = &e->lay;
    PixAnim *a = cur_anim(e);
    int fw = a ? a->fw : e->def_fw, fh = a ? a->fh : e->def_fh;
    float w = e->win_w > 320 ? e->win_w : 320, h = e->win_h > 400 ? e->win_h : 400;
    L->w = w; L->h = h;

    // Title, a help line, and two lines of status / help pinned to the bottom.
    L->title_y = ED_PAD;
    L->help_y = L->title_y + 30;
    L->foot_y = h - ED_PAD - 18;
    L->msg_y = L->foot_y - 22;
    float top = L->help_y + 24, bot = L->msg_y - 8;

    L->panel_w = clampf(w * 0.42f, 360, 520);
    if (L->panel_w > w - 2 * ED_PAD) L->panel_w = w - 2 * ED_PAD;
    L->panel_x = w - ED_PAD - L->panel_w;
    L->panel_top = top; L->panel_bot = bot;

    // Canvas takes the rest of the width; it stays square and centred in that area.
    L->canvas_x = ED_PAD; L->canvas_y = top;
    L->canvas_w = L->panel_x - 12 - ED_PAD; if (L->canvas_w < 40) L->canvas_w = 40;
    L->canvas_h = bot - top; if (L->canvas_h < 40) L->canvas_h = 40;
    float side = fminf(L->canvas_w, L->canvas_h);
    int big = fw > fh ? fw : fh;
    e->zoom = (int)(side / (big > 0 ? big : 1)); if (e->zoom < 1) e->zoom = 1;
    e->cx = (int)(L->canvas_x + (L->canvas_w - fw * e->zoom) * 0.5f);
    e->cy = (int)(L->canvas_y + (L->canvas_h - fh * e->zoom) * 0.5f);

    // Densities from roomy to tight: take the first that leaves room for the animated preview,
    // else the first that fits at all, else the tightest.
    static const float DENS[] = { 1.0f, 0.94f, 0.88f, 0.82f, 0.76f, 0.7f, 0.64f };
    const int NDENS = (int)(sizeof DENS / sizeof *DENS);
    float avail = bot - top, min_prev = (float)fh + 8;
    float chosen = DENS[NDENS - 1]; bool with_prev = false; int pick = NDENS - 1;
    for (int i = 0; i < NDENS; i++) { if (panel_flow(e, L, DENS[i]) + min_prev <= avail) { pick = i; chosen = DENS[i]; with_prev = true; break; } }
    if (!with_prev) for (int i = 0; i < NDENS; i++) { if (panel_flow(e, L, DENS[i]) <= avail) { pick = i; chosen = DENS[i]; break; } }
    // One notch tighter is worth it if it lifts the preview from a useless 1x to 2x or better.
    if (with_prev && pick + 1 < NDENS && avail - panel_flow(e, L, chosen) < fh * 2 + 8 &&
        avail - panel_flow(e, L, DENS[pick + 1]) >= fh * 2 + 8) chosen = DENS[pick + 1];
    float need = panel_flow(e, L, chosen);

    // Preview: as large as the leftover allows, up to 4x. The row of per-direction thumbnails
    // only appears when the panel is wide enough for it.
    float left = avail - need;
    L->prev_on = a && left >= fh + 4;
    L->prev_x = L->panel_x; L->prev_y = L->prev_label_y + 18;
    L->prev_zoom = 1;
    if (L->prev_on) {
        int z = (int)((left - 4) / fh); if (z > 4) z = 4; if (z < 1) z = 1;
        L->prev_zoom = z;
        L->prev_thumbs = L->panel_w >= fw * z + 8 + 20 + fw + 8;
    } else L->prev_thumbs = false;
}

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

void editor_set_size(Editor *e, float w, float h) {
    if (w <= 0 || h <= 0) return;
    e->win_w = w; e->win_h = h;
    relayout(e);   // cheap, and the layout also follows the document (frame size, anim count)
}

void editor_tick(Editor *e, const Input *in, float mx, float my, float dt) {
    PixAnim *a = cur_anim(e);
    { static bool demo_done = false; if (!demo_done && SDL_getenv("HOLLOW_EDIT_DEMO")) { demo_done = true; demo_paint(e); do_action(e, 0, in); } }
    if (e->msg_t > 0) e->msg_t -= dt;
    e->ants_t += dt;
    relayout(e);
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
        const EdLayout *L = &e->lay;
        for (int i = 0; i < e->doc.nanims && i < L->anim_max; i++) { Btn b = btn_anim(L, i); if (inside(mx, my, b.x, b.y, b.w, b.h)) { e->anim = i; e->frame = 0; e->playing = false; } }
        for (int i = 0; i < N_TOOLS; i++) { Btn b = btn_tool(L, i); if (inside(mx, my, b.x, b.y, b.w, b.h)) e->tool = (EdTool)i; }
        { Btn bm = btn_size_minus(L); if (inside(mx, my, bm.x, bm.y, bm.w, bm.h)) e->brush_size = e->brush_size > 1 ? e->brush_size - 1 : 1; }
        { Btn bp = btn_size_plus(L); if (inside(mx, my, bp.x, bp.y, bp.w, bp.h)) e->brush_size = e->brush_size < 4 ? e->brush_size + 1 : 4; }
        for (int i = 0; i < 33; i++) { Btn b = btn_pal(L, i); if (inside(mx, my, b.x, b.y, b.w, b.h)) { e->pal = i; e->color = PALETTE[i]; if (i == 0) e->tool = TOOL_ERASER; else if (e->tool == TOOL_ERASER) e->tool = TOOL_PENCIL; } }
        for (int i = 0; i < 7; i++) { Btn b = btn_ramp(L, i); if (inside(mx, my, b.x, b.y, b.w, b.h)) { e->color = ramp_color(e->color, i); e->pal = -1; for (int k = 0; k < 33; k++) if (PALETTE[k] == e->color) e->pal = k; } }
        if (a) for (int i = 0; i < a->ndirs; i++) { Btn b = btn_dir(L, i); if (inside(mx, my, b.x, b.y, b.w, b.h)) e->dir = i; }
        if (a) for (int i = 0; i < a->nframes && i < L->frames_max; i++) { Btn b = btn_frame(L, i); if (inside(mx, my, b.x, b.y, b.w, b.h)) e->frame = i; }
        for (int i = 0; i < N_ACTIONS; i++) { Btn b = btn_action(L, i); if (inside(mx, my, b.x, b.y, b.w, b.h)) do_action(e, i, in); }
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

// Text helpers: everything is measured before it is drawn, so no label spills out of its box.
#define ED_HEAD  1.5f
#define ED_BODY  1.1f
#define ED_SMALL 0.95f

static void text_mid(Gfx *g, float cx, float cy, float scale, Vec4 c, const char *s) {
    gfx_ui_text(g, cx - gfx_ui_text_width(scale, s) * 0.5f, cy - gfx_ui_line_h(scale) * 0.5f, scale, c, s);
}
static void text_left(Gfx *g, float x, float cy, float scale, Vec4 c, const char *s) {
    gfx_ui_text(g, x, cy - gfx_ui_line_h(scale) * 0.5f, scale, c, s);
}
// Centres `s` in b, falling back to the shorter `alt` and then to the small scale.
static void btn_text(Gfx *g, Btn b, const char *s, const char *alt, Vec4 c) {
    float room = b.w - 6, cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f;
    if (gfx_ui_text_width(ED_BODY, s) <= room)             { text_mid(g, cx, cy, ED_BODY, c, s); return; }
    if (alt && gfx_ui_text_width(ED_BODY, alt) <= room)    { text_mid(g, cx, cy, ED_BODY, c, alt); return; }
    if (gfx_ui_text_width(ED_SMALL, s) <= room)            { text_mid(g, cx, cy, ED_SMALL, c, s); return; }
    text_mid(g, cx, cy, ED_SMALL, c, alt ? alt : s);
}
// Draws text truncated with an ellipsis if it would run past maxw.
static void text_clip(Gfx *g, float x, float y, float scale, Vec4 c, const char *s, float maxw) {
    if (gfx_ui_text_width(scale, s) <= maxw) { gfx_ui_text(g, x, y, scale, c, s); return; }
    char buf[320]; int n = 0;
    for (const char *p = s; *p && n < (int)sizeof buf - 5; p++) {
        buf[n] = *p; buf[n + 1] = 0;
        if (gfx_ui_text_width(scale, buf) > maxw - gfx_ui_text_width(scale, "...")) { buf[n] = 0; break; }
        n++;
    }
    snprintf(buf + n, sizeof buf - n, "...");
    gfx_ui_text(g, x, y, scale, c, buf);
}
// Joins as many hint fragments as fit in maxw (the first one always goes in).
static void fit_join(char *out, size_t n, float scale, float maxw, const char *const *parts, int nparts) {
    out[0] = 0;
    for (int i = 0; i < nparts; i++) {
        char cand[400]; snprintf(cand, sizeof cand, "%s%s%s", out, out[0] ? "   " : "", parts[i]);
        if (i > 0 && gfx_ui_text_width(scale, cand) > maxw) break;
        snprintf(out, n, "%s", cand);
    }
}

void editor_draw(Editor *e, Gfx *g) {
    PixAnim *a = cur_anim(e);
    const EdLayout *L = &e->lay;
    Vec4 white = v4(0.92f, 0.9f, 0.86f, 1), dim = v4(0.6f, 0.58f, 0.55f, 1), acc = v4(1, 0.85f, 0.4f, 1);
    Vec4 btn = v4(0.16f, 0.16f, 0.19f, 1), btn_on = v4(0.3f, 0.27f, 0.2f, 1);
    gfx_ui_rect(g, 0, 0, L->w, L->h, v4(0.09f, 0.09f, 0.11f, 1));

    // Title, with the document status right-aligned in the same row when there is room for it.
    char status[160];
    snprintf(status, sizeof status, "%s  %s  frame %d/%d  %.0f fps%s", a ? a->name : "-",
             a ? (const char *[]){"down", "up", "left", "right"}[e->dir] : "-", e->frame + 1, a ? a->nframes : 0,
             a ? a->fps : 0.0f, a && a->loop ? "  loop" : "");
    bool status_in_title;
    { char t[160]; snprintf(t, sizeof t, "SPRITE EDITOR  %s%s", e->doc.name, e->dirty ? " *" : "");
      float cy = L->title_y + 14, tw = gfx_ui_text_width(ED_HEAD, t), sw = gfx_ui_text_width(ED_BODY, status);
      status_in_title = ED_PAD + tw + 16 + sw <= L->w - ED_PAD;
      text_clip(g, ED_PAD, cy - gfx_ui_line_h(ED_HEAD) * 0.5f, ED_HEAD, white, t, L->w - 2 * ED_PAD);
      if (status_in_title) text_left(g, L->w - ED_PAD - sw, cy, ED_BODY, acc, status); }
    // Help line: as many hints as the width takes.
    { char tool_s[96]; snprintf(tool_s, sizeof tool_s, "tool %s%s  size %d", TOOL_NAMES[e->tool], e->mirror_x ? " (mirror)" : "", e->brush_size);
      const char *parts[12]; int np = 0;
      if (!status_in_title) parts[np++] = status;
      parts[np++] = tool_s;
      parts[np++] = ", . size";
      parts[np++] = "B E G I L U Y S ; ' tools";
      parts[np++] = "[ ] frames";
      parts[np++] = "1-4 dirs";
      parts[np++] = "arrows nudge/move sel";
      parts[np++] = "wheel colour";
      parts[np++] = "right-click pick, right-drag erase";
      char help[400]; fit_join(help, sizeof help, ED_SMALL, L->w - 2 * ED_PAD, parts, np);
      text_clip(g, ED_PAD, L->help_y, ED_SMALL, dim, help, L->w - 2 * ED_PAD); }

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
    for (int i = 0; i < e->doc.nanims && i < L->anim_max; i++) {
        Btn b = btn_anim(L, i); bool sel = i == e->anim;
        gfx_ui_rect(g, b.x, b.y, b.w, b.h, sel ? btn_on : btn);
        char s[64]; snprintf(s, sizeof s, "%s %d", e->doc.anims[i].name, e->doc.anims[i].nframes);
        btn_text(g, b, s, e->doc.anims[i].name, sel ? acc : white);
    }
    // Tools
    for (int i = 0; i < N_TOOLS; i++) { Btn b = btn_tool(L, i); bool sel = (int)e->tool == i;
        gfx_ui_rect(g, b.x, b.y, b.w, b.h, sel ? btn_on : btn);
        btn_text(g, b, TOOL_LABELS[i], NULL, sel ? acc : white); }
    // Brush size stepper
    { Btn bm = btn_size_minus(L), bp = btn_size_plus(L);
      gfx_ui_rect(g, bm.x, bm.y, bm.w, bm.h, btn); btn_text(g, bm, "-", NULL, white);
      gfx_ui_rect(g, bp.x, bp.y, bp.w, bp.h, btn); btn_text(g, bp, "+", NULL, white);
      char sz[8]; snprintf(sz, sizeof sz, "%d", e->brush_size);
      text_mid(g, (L->size_val_x + bp.x) * 0.5f, bm.y + bm.h * 0.5f, ED_BODY, acc, sz); }
    // Palette
    for (int i = 0; i < 33; i++) { Btn b = btn_pal(L, i);
        if (i == 0) { gfx_ui_rect(g, b.x, b.y, b.w, b.h, v4(0.2f, 0.2f, 0.23f, 1)); gfx_ui_rect(g, b.x + b.w * 0.2f, b.y + b.h * 0.45f, b.w * 0.6f, 4, v4(0.8f, 0.3f, 0.3f, 1)); }
        else gfx_ui_rect(g, b.x, b.y, b.w, b.h, col4(PALETTE[i]));
        if (i == e->pal) { gfx_ui_rect(g, b.x - 3, b.y - 3, b.w + 6, 3, white); gfx_ui_rect(g, b.x - 3, b.y + b.h, b.w + 6, 3, white); gfx_ui_rect(g, b.x - 3, b.y - 3, 3, b.h + 6, white); gfx_ui_rect(g, b.x + b.w, b.y - 3, 3, b.h + 6, white); } }
    // Colour ramp: darker/lighter shades of the current colour, for consistent shading
    for (int i = 0; i < 7; i++) { Btn b = btn_ramp(L, i);
        gfx_ui_rect(g, b.x, b.y, b.w, b.h, col4(ramp_color(e->color, i)));
        if (i == 3) gfx_ui_rect(g, b.x - 2, b.y - 2, b.w + 4, 2, white); }
    // Directions
    if (a) for (int i = 0; i < a->ndirs; i++) { Btn b = btn_dir(L, i); bool sel = i == e->dir;
        gfx_ui_rect(g, b.x, b.y, b.w, b.h, sel ? btn_on : btn);
        btn_text(g, b, (const char *[]){"DOWN 1", "UP 2", "LEFT 3", "RIGHT 4"}[i], (const char *[]){"DN", "UP", "L", "R"}[i], sel ? acc : white); }
    // Frames strip
    { const char *parts[] = { "FRAMES", "N new", "D dup", "X del", "K contact" };
      char s[160]; fit_join(s, sizeof s, ED_SMALL, L->panel_w, parts, 5);
      gfx_ui_text(g, L->frames_x, L->frames_label_y, ED_SMALL, dim, s); }
    if (a) for (int i = 0; i < a->nframes && i < L->frames_max; i++) { Btn b = btn_frame(L, i); bool sel = i == e->frame;
        gfx_ui_rect(g, b.x, b.y, b.w, b.h, sel ? btn_on : v4(0.14f, 0.14f, 0.17f, 1));
        float tz = floorf((b.w - 8) / (a->fw > a->fh ? a->fw : a->fh)); if (tz < 1) tz = 1;
        draw_frame_px(g, a, pix_frame(a, e->dir, i), b.x + (b.w - a->fw * tz) * 0.5f, b.y + (b.h - a->fh * tz) * 0.5f, tz, 1.0f);
        bool contact = false; for (int k = 0; k < a->ncontact; k++) if (a->contact[k] == i) contact = true;
        if (contact) gfx_ui_rect(g, b.x + b.w - 14, b.y + 4, 10, 10, v4(1, 0.3f, 0.2f, 1));
        char s[8]; snprintf(s, sizeof s, "%d", i + 1);
        gfx_ui_text(g, b.x + 4, b.y + b.h - gfx_ui_line_h(ED_SMALL) - 2, ED_SMALL, sel ? acc : dim, s); }
    // Actions
    for (int i = 0; i < N_ACTIONS; i++) { Btn b = btn_action(L, i);
        gfx_ui_rect(g, b.x, b.y, b.w, b.h, btn);
        bool on = (i == 8 && e->onion) || (i == 9 && e->mirror_x) || (i == 10 && e->grid) || (i == 3 && e->playing);
        btn_text(g, b, ACTIONS[i], ACTIONS_SHORT[i], on ? acc : white); }
    // Playback preview, plus 1x and per-direction thumbnails when the panel is wide enough
    if (a && L->prev_on) {
        int pf = e->playing ? (int)e->play_t : e->frame; if (pf >= a->nframes) pf = a->nframes - 1; if (pf < 0) pf = 0;
        float px = L->prev_x, py = L->prev_y, z = (float)L->prev_zoom;
        { const char *parts[] = { "PREVIEW", "(Space plays)" };
          char s[64]; fit_join(s, sizeof s, ED_SMALL, L->panel_w, parts, 2);
          gfx_ui_text(g, px, L->prev_label_y, ED_SMALL, dim, s); }
        gfx_ui_rect(g, px, py, a->fw * z + 8, a->fh * z + 8, v4(0.12f, 0.12f, 0.15f, 1));
        draw_frame_px(g, a, pix_frame(a, e->dir, pf), px + 4, py + 4, z, 1.0f);
        if (L->prev_thumbs) {
            float tx = px + a->fw * z + 8 + 12, right = L->panel_x + L->panel_w;
            gfx_ui_rect(g, tx, py, a->fw + 8.0f, a->fh + 8.0f, v4(0.12f, 0.12f, 0.15f, 1));
            draw_frame_px(g, a, pix_frame(a, e->dir, pf), tx + 4, py + 4, 1, 1.0f);
            float dx = tx + a->fw + 8 + 12;
            for (int d = 0; d < a->ndirs; d++) {
                if (dx + a->fw * 2 > right || py + a->fh * 2 > L->panel_bot) break;
                draw_frame_px(g, a, pix_frame(a, d, pf), dx, py + 4, 2, 1.0f);
                dx += a->fw * 2 + 6;
            }
        }
    }
    if (e->msg_t > 0) text_clip(g, ED_PAD, L->msg_y, ED_BODY, acc, e->msg, L->w - 2 * ED_PAD);
    { const char *parts[] = { "Ctrl+S save", "Ctrl+Z/Y undo/redo", "Ctrl+C/V copy/paste sel", "Enter deselect",
                              "Del/X clear sel", "C copy to dirs", "F mirror", "A add anim", "-/+ fps", "Esc quit" };
      char s[400]; fit_join(s, sizeof s, ED_SMALL, L->w - 2 * ED_PAD, parts, 10);
      text_clip(g, ED_PAD, L->foot_y, ED_SMALL, dim, s, L->w - 2 * ED_PAD); }
}

// ---------------------------------------------------------------- self-test
#ifdef EDITOR_TEST
#include <assert.h>

// gfx.h stubs: the test never renders, it only exercises the tool logic above.
void gfx_ui_rect(Gfx *g, float x, float y, float w, float h, Vec4 color) { (void)g; (void)x; (void)y; (void)w; (void)h; (void)color; }
void gfx_ui_text(Gfx *g, float x, float y, float scale, Vec4 color, const char *text) { (void)g; (void)x; (void)y; (void)scale; (void)color; (void)text; }
float gfx_ui_text_width(float scale, const char *text) { (void)scale; (void)text; return 0; }
float gfx_ui_line_h(float scale) { return 7.0f * scale + 2.0f; }

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
