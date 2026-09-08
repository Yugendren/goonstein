// See widgets.h. Immediate-mode: every widget call both draws itself and handles input for the
// current frame. Ids are assigned in call order (ui_begin resets next_id to 1) so a widget keeps
// the same id frame to frame as long as the call sequence doesn't change, which is what lets
// `active` persist across frames for slider/list dragging.
#include "widgets.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------- palette

static const Vec4 UI_BASE    = {0.16f, 0.16f, 0.19f, 1.0f};
static const Vec4 UI_HOVER   = {0.24f, 0.23f, 0.28f, 1.0f};
static const Vec4 UI_ACTIVE  = {0.32f, 0.28f, 0.20f, 1.0f};
static const Vec4 UI_TEXT    = {0.90f, 0.90f, 0.88f, 1.0f};
static const Vec4 UI_ACCENT  = {1.00f, 0.85f, 0.40f, 1.0f};
static const Vec4 UI_DIM     = {0.60f, 0.58f, 0.55f, 1.0f};
static const Vec4 UI_BORDER  = {0.36f, 0.35f, 0.40f, 1.0f};

#define UI_FONT_H 7.0f  // approximate glyph height at scale 1

// ---------------------------------------------------------------- lifecycle

void ui_begin(Ui *ui, Gfx *g, UiInput in) {
    ui->g = g;
    ui->in = in;
    ui->next_id = 1;
    ui->hot = 0;
}

void ui_end(Ui *ui) {
    if (!ui->in.down) ui->active = 0;
}

// ---------------------------------------------------------------- small helpers

static bool point_in_rect(float px, float py, float x, float y, float w, float h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void draw_frame(Gfx *g, float x, float y, float w, float h, Vec4 fill) {
    gfx_ui_rect(g, x, y, w, h, fill);
    gfx_ui_rect(g, x, y, w, 2.0f, UI_BORDER);
    gfx_ui_rect(g, x, y + h - 2.0f, w, 2.0f, UI_BORDER);
    gfx_ui_rect(g, x, y, 2.0f, h, UI_BORDER);
    gfx_ui_rect(g, x + w - 2.0f, y, 2.0f, h, UI_BORDER);
}

static void draw_centered(Gfx *g, float cx, float cy, float scale, Vec4 color, const char *text) {
    float w = gfx_ui_text_width(scale, text);
    gfx_ui_text(g, cx - w * 0.5f, cy - UI_FONT_H * scale * 0.5f, scale, color, text);
}

static int next_id(Ui *ui) { return ui->next_id++; }

// ---------------------------------------------------------------- label / header

void ui_label(Ui *ui, float x, float y, const char *text, Vec4 color) {
    gfx_ui_text(ui->g, x, y, 1.2f, color, text);
}

void ui_header(Ui *ui, float x, float y, const char *text) {
    gfx_ui_text(ui->g, x, y, 1.4f, UI_ACCENT, text);
}

// ---------------------------------------------------------------- button

bool ui_button(Ui *ui, float x, float y, float w, float h, const char *text) {
    int id = next_id(ui);
    bool inside = point_in_rect(ui->in.mx, ui->in.my, x, y, w, h);
    if (inside) ui->hot = id;

    bool pressed_now = inside && ui->in.pressed;
    if (pressed_now) ui->active = id;

    Vec4 fill = UI_BASE;
    if (ui->active == id && ui->in.down) fill = UI_ACTIVE;
    else if (inside) fill = UI_HOVER;

    draw_frame(ui->g, x, y, w, h, fill);
    draw_centered(ui->g, x + w * 0.5f, y + h * 0.5f, 1.2f, UI_TEXT, text);

    return pressed_now;
}

// ---------------------------------------------------------------- toggle

bool ui_toggle(Ui *ui, float x, float y, float w, float h, const char *text, bool *v) {
    int id = next_id(ui);
    bool inside = point_in_rect(ui->in.mx, ui->in.my, x, y, w, h);
    if (inside) ui->hot = id;

    bool pressed_now = inside && ui->in.pressed;
    if (pressed_now) ui->active = id;

    Vec4 fill = UI_BASE;
    if (ui->active == id && ui->in.down) fill = UI_ACTIVE;
    else if (inside) fill = UI_HOVER;

    draw_frame(ui->g, x, y, w, h, fill);

    float box = h - 10.0f;
    float bx = x + 8.0f, by = y + (h - box) * 0.5f;
    gfx_ui_rect(ui->g, bx, by, box, box, UI_BORDER);
    if (*v) gfx_ui_rect(ui->g, bx + 2.0f, by + 2.0f, box - 4.0f, box - 4.0f, UI_ACCENT);
    else    gfx_ui_rect(ui->g, bx + 2.0f, by + 2.0f, box - 4.0f, box - 4.0f, UI_BASE);

    gfx_ui_text(ui->g, bx + box + 8.0f, y + h * 0.5f - UI_FONT_H * 1.2f * 0.5f, 1.2f, UI_TEXT, text);

    if (pressed_now) { *v = !*v; return true; }
    return false;
}

// ---------------------------------------------------------------- slider

#define UI_SLIDER_H 22.0f

// Shared slider body used both directly and from ui_color; returns true if the value changed.
static bool slider_body(Ui *ui, int id, float x, float y, float w, const char *label, float *v, float lo, float hi) {
    float track_x = x + 110.0f;
    float track_w = w - 120.0f;
    if (track_w < 10.0f) track_w = 10.0f;
    float track_y = y + (UI_SLIDER_H - 8.0f) * 0.5f;

    float t = (hi > lo) ? clampf((*v - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
    float knob_x = track_x + t * track_w - 5.0f;
    float knob_y = y + (UI_SLIDER_H - 16.0f) * 0.5f;

    bool over_track = point_in_rect(ui->in.mx, ui->in.my, track_x, y, track_w, UI_SLIDER_H);
    bool over_knob = point_in_rect(ui->in.mx, ui->in.my, knob_x, knob_y, 10.0f, 16.0f);
    bool inside = over_track || over_knob;
    if (inside) ui->hot = id;

    if (inside && ui->in.pressed) ui->active = id;

    bool changed = false;
    if (ui->active == id && ui->in.down) {
        float nt = clampf((ui->in.mx - track_x) / track_w, 0.0f, 1.0f);
        float nv = clampf(lo + nt * (hi - lo), lo, hi);
        if (nv != *v) { *v = nv; changed = true; }
        t = (hi > lo) ? clampf((*v - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
        knob_x = track_x + t * track_w - 5.0f;
    }

    if (label && label[0]) gfx_ui_text(ui->g, x, y + UI_SLIDER_H * 0.5f - UI_FONT_H * 1.2f * 0.5f, 1.2f, UI_TEXT, label);

    Vec4 track_fill = (ui->active == id && ui->in.down) ? UI_ACTIVE : (inside ? UI_HOVER : UI_BASE);
    gfx_ui_rect(ui->g, track_x, track_y, track_w, 8.0f, track_fill);
    gfx_ui_rect(ui->g, track_x, track_y, track_w, 2.0f, UI_BORDER);
    gfx_ui_rect(ui->g, track_x, track_y + 6.0f, track_w, 2.0f, UI_BORDER);

    Vec4 knob_color = (ui->active == id && ui->in.down) ? UI_ACCENT : UI_HOVER;
    gfx_ui_rect(ui->g, knob_x, knob_y, 10.0f, 16.0f, knob_color);

    char buf[32];
    if (hi - lo > 10.0f) snprintf(buf, sizeof buf, "%.1f", *v);
    else                 snprintf(buf, sizeof buf, "%.2f", *v);
    gfx_ui_text(ui->g, track_x + track_w + 8.0f, y + UI_SLIDER_H * 0.5f - UI_FONT_H * 1.2f * 0.5f, 1.2f, UI_DIM, buf);

    return changed;
}

bool ui_slider(Ui *ui, float x, float y, float w, const char *label, float *v, float lo, float hi) {
    int id = next_id(ui);
    return slider_body(ui, id, x, y, w, label, v, lo, hi);
}

// ---------------------------------------------------------------- color

bool ui_color(Ui *ui, float x, float y, float w, const char *label, Vec3 *c, float hi) {
    if (label && label[0]) gfx_ui_text(ui->g, x, y - UI_FONT_H * 1.2f - 2.0f, 1.2f, UI_TEXT, label);

    float swatch = 40.0f;
    float sliders_w = w - swatch - 10.0f;
    if (sliders_w < 40.0f) sliders_w = 40.0f;

    int id_r = next_id(ui), id_g = next_id(ui), id_b = next_id(ui);
    bool cr = slider_body(ui, id_r, x, y,        sliders_w, "r", &c->x, 0.0f, hi);
    bool cg = slider_body(ui, id_g, x, y + 22.0f, sliders_w, "g", &c->y, 0.0f, hi);
    bool cb = slider_body(ui, id_b, x, y + 44.0f, sliders_w, "b", &c->z, 0.0f, hi);

    Vec4 disp = v4(clampf(c->x, 0.0f, 1.0f), clampf(c->y, 0.0f, 1.0f), clampf(c->z, 0.0f, 1.0f), 1.0f);
    float swatch_x = x + w - swatch;
    gfx_ui_rect(ui->g, swatch_x, y, swatch, swatch, UI_BORDER);
    gfx_ui_rect(ui->g, swatch_x + 2.0f, y + 2.0f, swatch - 4.0f, swatch - 4.0f, disp);

    return cr || cg || cb;
}

// ---------------------------------------------------------------- list

bool ui_list(Ui *ui, int list_id, float x, float y, float w, float h, const char **items, int n, int *selected) {
    int id = next_id(ui);
    bool inside = point_in_rect(ui->in.mx, ui->in.my, x, y, w, h);
    if (inside) ui->hot = id;

    const float row_h = 18.0f;
    float content_h = (float)n * row_h;
    float max_scroll = content_h - h;
    if (max_scroll < 0.0f) max_scroll = 0.0f;

    float *scroll = &ui->scroll[list_id & 7];
    if (inside && ui->in.wheel != 0.0f) *scroll -= ui->in.wheel * row_h * 3.0f;
    *scroll = clampf(*scroll, 0.0f, max_scroll);

    bool changed = false;
    if (inside && ui->in.pressed) {
        ui->active = id;
        float local_y = ui->in.my - y + *scroll;
        int idx = (int)(local_y / row_h);
        if (idx >= 0 && idx < n && selected && *selected != idx) { *selected = idx; changed = true; }
    }

    draw_frame(ui->g, x, y, w, h, UI_BASE);

    bool has_scrollbar = max_scroll > 0.0f;
    float track_w = has_scrollbar ? 6.0f : 0.0f;
    float clip_w = w - track_w - 2.0f;

    float first = floorf(*scroll / row_h);
    float top_local = first * row_h - *scroll;
    for (int i = (int)first; i < n; i++) {
        float ry = y + top_local + (float)(i - (int)first) * row_h;
        if (ry >= y + h) break;
        if (ry + row_h <= y) continue;
        if (ry < y || ry + row_h > y + h) continue;  // clip: skip partially visible rows

        if (selected && *selected == i) {
            gfx_ui_rect(ui->g, x + 2.0f, ry, clip_w, row_h, UI_ACTIVE);
            gfx_ui_rect(ui->g, x + 2.0f, ry, 3.0f, row_h, UI_ACCENT);
        } else if (point_in_rect(ui->in.mx, ui->in.my, x, ry, w, row_h)) {
            gfx_ui_rect(ui->g, x + 2.0f, ry, clip_w, row_h, UI_HOVER);
        }

        Vec4 tc = (selected && *selected == i) ? UI_ACCENT : UI_TEXT;
        gfx_ui_text(ui->g, x + 8.0f, ry + row_h * 0.5f - UI_FONT_H * 1.1f * 0.5f, 1.1f, tc, items[i]);
    }

    if (has_scrollbar) {
        float bar_x = x + w - track_w - 2.0f;
        gfx_ui_rect(ui->g, bar_x, y + 2.0f, track_w, h - 4.0f, UI_BASE);
        float thumb_h = fmaxf((h / content_h) * (h - 4.0f), 8.0f);
        float thumb_y = y + 2.0f + (*scroll / max_scroll) * (h - 4.0f - thumb_h);
        gfx_ui_rect(ui->g, bar_x, thumb_y, track_w, thumb_h, UI_DIM);
    }

    return changed;
}

// ---------------------------------------------------------------- stepper

bool ui_stepper(Ui *ui, float x, float y, const char *label, float *v, float step, float lo, float hi) {
    const float btn = 22.0f;
    float label_w = label && label[0] ? gfx_ui_text_width(1.2f, label) + 10.0f : 0.0f;
    float minus_x = x + label_w;

    if (label && label[0]) gfx_ui_text(ui->g, x, y + btn * 0.5f - UI_FONT_H * 1.2f * 0.5f, 1.2f, UI_TEXT, label);

    bool changed = false;

    int id_minus = next_id(ui);
    bool inside_minus = point_in_rect(ui->in.mx, ui->in.my, minus_x, y, btn, btn);
    if (inside_minus) ui->hot = id_minus;
    bool press_minus = inside_minus && ui->in.pressed;
    if (press_minus) ui->active = id_minus;
    Vec4 fill_minus = (ui->active == id_minus && ui->in.down) ? UI_ACTIVE : (inside_minus ? UI_HOVER : UI_BASE);
    draw_frame(ui->g, minus_x, y, btn, btn, fill_minus);
    draw_centered(ui->g, minus_x + btn * 0.5f, y + btn * 0.5f, 1.2f, UI_TEXT, "-");
    if (press_minus) {
        float nv = clampf(*v - step, lo, hi);
        if (nv != *v) { *v = nv; changed = true; }
    }

    char buf[32];
    snprintf(buf, sizeof buf, "%.1f", *v);
    float val_w = 56.0f;
    float val_x = minus_x + btn + 6.0f;
    draw_centered(ui->g, val_x + val_w * 0.5f, y + btn * 0.5f, 1.2f, UI_TEXT, buf);

    float plus_x = val_x + val_w + 6.0f;
    int id_plus = next_id(ui);
    bool inside_plus = point_in_rect(ui->in.mx, ui->in.my, plus_x, y, btn, btn);
    if (inside_plus) ui->hot = id_plus;
    bool press_plus = inside_plus && ui->in.pressed;
    if (press_plus) ui->active = id_plus;
    Vec4 fill_plus = (ui->active == id_plus && ui->in.down) ? UI_ACTIVE : (inside_plus ? UI_HOVER : UI_BASE);
    draw_frame(ui->g, plus_x, y, btn, btn, fill_plus);
    draw_centered(ui->g, plus_x + btn * 0.5f, y + btn * 0.5f, 1.2f, UI_TEXT, "+");
    if (press_plus) {
        float nv = clampf(*v + step, lo, hi);
        if (nv != *v) { *v = nv; changed = true; }
    }

    return changed;
}

// ---------------------------------------------------------------- standalone test

#ifdef WIDGETS_TEST
// Stubs so the standalone test doesn't touch the real renderer; count calls instead.
static int g_rect_calls = 0, g_text_calls = 0;

void gfx_ui_rect(Gfx *g, float x, float y, float w, float h, Vec4 color) {
    (void)g; (void)x; (void)y; (void)w; (void)h; (void)color;
    g_rect_calls++;
}
void gfx_ui_text(Gfx *g, float x, float y, float scale, Vec4 color, const char *text) {
    (void)g; (void)x; (void)y; (void)scale; (void)color; (void)text;
    g_text_calls++;
}
float gfx_ui_text_width(float scale, const char *text) {
    return scale * 6.0f * (float)strlen(text);
}
void gfx_ui_quad(Gfx *g, const float *xy8, Vec4 color) { (void)g; (void)xy8; (void)color; }
void gfx_ui_text_xf(Gfx *g, float cx, float cy, float scale, float angle, Vec4 color, const char *text) {
    (void)g; (void)cx; (void)cy; (void)scale; (void)angle; (void)color; (void)text;
}
void gfx_ui_ring(Gfx *g, float cx, float cy, float radius, float thickness, Vec4 color) {
    (void)g; (void)cx; (void)cy; (void)radius; (void)thickness; (void)color;
}
void gfx_ui_disc(Gfx *g, float cx, float cy, float radius, Vec4 color) {
    (void)g; (void)cx; (void)cy; (void)radius; (void)color;
}
void gfx_ui_target(Gfx *g, int target) { (void)g; (void)target; }
void gfx_ui_image(Gfx *g, const Texture *t, float x, float y, float w, float h, const float *uv, Vec4 color) {
    (void)g; (void)t; (void)x; (void)y; (void)w; (void)h; (void)uv; (void)color;
}

static UiInput make_input(float mx, float my, bool down, bool pressed, bool released, float wheel) {
    UiInput in; in.mx = mx; in.my = my; in.down = down; in.pressed = pressed; in.released = released; in.wheel = wheel;
    return in;
}

int main(void) {
    Gfx g; memset(&g, 0, sizeof(g));
    Ui ui; memset(&ui, 0, sizeof(ui));
    int failures = 0;

    // --- button: press inside returns true this frame ---
    {
        UiInput in = make_input(50.0f, 15.0f, true, true, false, 0.0f);
        ui_begin(&ui, &g, in);
        bool clicked = ui_button(&ui, 10.0f, 10.0f, 100.0f, 22.0f, "Go");
        ui_end(&ui);
        printf("button clicked=%s\n", clicked ? "true" : "false");
        if (!clicked) failures++;
    }

    // --- slider: drag from x=110 (value=lo) toward the middle over a few frames ---
    {
        float value = 0.0f;
        float lo = 0.0f, hi = 1.0f;
        float x = 0.0f, y = 100.0f, w = 240.0f;      // track spans [x+110, x+110+w-120] = [110, 230]
        float track_x = x + 110.0f, track_w = w - 120.0f;

        // frame 1: press on the knob at the start of the track
        UiInput in = make_input(track_x + 1.0f, y + 10.0f, true, true, false, 0.0f);
        ui_begin(&ui, &g, in);
        bool changed1 = ui_slider(&ui, x, y, w, "vol", &value, lo, hi);
        ui_end(&ui);

        // frames 2..4: keep dragging toward the middle of the track
        bool changed_any = changed1;
        for (int i = 0; i < 4; i++) {
            float mx = track_x + track_w * 0.5f;
            UiInput in2 = make_input(mx, y + 10.0f, true, false, false, 0.0f);
            ui_begin(&ui, &g, in2);
            bool c = ui_slider(&ui, x, y, w, "vol", &value, lo, hi);
            ui_end(&ui);
            changed_any = changed_any || c;
        }

        printf("slider value=%.3f changed=%s\n", value, changed_any ? "true" : "false");
        if (fabsf(value - 0.5f) > 0.05f) failures++;
        if (!changed_any) failures++;
    }

    // --- list: wheel scroll then click selects item 5 ---
    {
        const char *items[10] = {"a0","a1","a2","a3","a4","a5","a6","a7","a8","a9"};
        int selected = -1;
        float x = 0.0f, y = 0.0f, w = 120.0f, h = 60.0f;  // 60 / 18 ~= 3.3 rows visible

        // scroll down with the wheel while hovering the list (no click)
        UiInput in = make_input(x + 10.0f, y + 10.0f, false, false, false, -3.0f);
        ui_begin(&ui, &g, in);
        ui_list(&ui, 0, x, y, w, h, items, 10, &selected);
        ui_end(&ui);
        float scroll_after_wheel = ui.scroll[0];

        // click near the top of the box; with scroll applied this should land on item 5
        float target_scroll = 5.0f * 18.0f;  // scroll so item 5 sits at the top
        ui.scroll[0] = target_scroll;
        UiInput in2 = make_input(x + 10.0f, y + 4.0f, true, true, false, 0.0f);
        ui_begin(&ui, &g, in2);
        bool changed = ui_list(&ui, 0, x, y, w, h, items, 10, &selected);
        ui_end(&ui);

        printf("list scroll_after_wheel=%.1f selected=%d changed=%s\n", scroll_after_wheel, selected, changed ? "true" : "false");
        if (selected != 5) failures++;
        if (!changed) failures++;
        if (scroll_after_wheel <= 0.0f) failures++;
    }

    printf("rect_calls=%d text_calls=%d\n", g_rect_calls, g_text_calls);
    printf("failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
#endif
