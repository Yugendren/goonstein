// See uifx.h. Simulation (age/lifetime bookkeeping) lives in uifx_update; the visual curves
// (scale, offset, colour, alpha over age) live in uifx_draw so the two stay decoupled, same
// split as particles.c.
#include "uifx.h"
#include <string.h>

#ifdef UIFX_TEST
#include <stdio.h>
#endif

// ---------------------------------------------------------------- deterministic rng

static Uint32 g_uifx_rng_state = 0x9e3779b9u;

static inline Uint32 uifx_xorshift32(Uint32 *state) {
    Uint32 x = *state ? *state : 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static inline float uifx_frand(void) {  // [0, 1)
    Uint32 r = uifx_xorshift32(&g_uifx_rng_state);
    return (float)((double)r / ((double)0xFFFFFFFFu + 1.0));
}

// ---------------------------------------------------------------- lifecycle

void uifx_init(Uifx *u) { memset(u, 0, sizeof(*u)); }
void uifx_clear(Uifx *u) { memset(u, 0, sizeof(*u)); }

void uifx_spawn(Uifx *u, UifxStyle style, float x, float y, const char *text, Vec4 color, float scale, float dur) {
    int slot = -1;
    for (int i = 0; i < UIFX_MAX; i++)
        if (!u->items[i].alive) { slot = i; break; }
    if (slot < 0) {
        // Replace the oldest (furthest through its life, as a fraction) item.
        float worst = -1.0f;
        for (int i = 0; i < UIFX_MAX; i++) {
            float frac = u->items[i].t / fmaxf(u->items[i].dur, 1e-4f);
            if (frac > worst) { worst = frac; slot = i; }
        }
    }
    UifxItem *it = &u->items[slot];
    it->alive = true;
    it->style = style;
    it->x = x;
    it->y = y;
    it->t = 0.0f;
    it->dur = fmaxf(dur, 0.05f);
    it->scale = scale;
    it->color = color;
    it->seed = uifx_frand();
    snprintf(it->text, sizeof(it->text), "%s", text ? text : "");
}

void uifx_update(Uifx *u, float dt) {
    for (int i = 0; i < UIFX_MAX; i++) {
        UifxItem *it = &u->items[i];
        if (!it->alive) continue;
        it->t += dt;
        if (it->t >= it->dur) it->alive = false;
    }
}

// ---------------------------------------------------------------- draw

#ifdef UIFX_TEST
// Stubs so the standalone test doesn't touch the real renderer; count calls instead.
static int g_rect_calls = 0, g_text_calls = 0, g_text_width_calls = 0;
void gfx_ui_rect(Gfx *g, float x, float y, float w, float h, Vec4 color) {
    (void)g; (void)x; (void)y; (void)w; (void)h; (void)color;
    g_rect_calls++;
}
void gfx_ui_text(Gfx *g, float x, float y, float scale, Vec4 color, const char *text) {
    (void)g; (void)x; (void)y; (void)scale; (void)color; (void)text;
    g_text_calls++;
}
float gfx_ui_text_width(float scale, const char *text) {
    g_text_width_calls++;
    return scale * 6.0f * (float)strlen(text);
}
#endif

#define UIFX_FONT_H 7.0f  // approximate glyph height at scale 1, for vertical centring

static void draw_centered(Gfx *g, float x, float y, float scale, Vec4 color, const char *text) {
    if (scale <= 0.0f || color.w <= 0.0f || text[0] == '\0') return;
    float w = gfx_ui_text_width(scale, text);
    float tx = x - w * 0.5f;
    float ty = y - UIFX_FONT_H * scale * 0.5f;
    Vec4 shadow = v4(0.0f, 0.0f, 0.0f, color.w * 0.6f);
    gfx_ui_text(g, tx + 2.0f, ty + 2.0f, scale, shadow, text);
    gfx_ui_text(g, tx, ty, scale, color, text);
}

// Alpha multiplier that stays at 1 until `start_frac` of age, then eases to 0.
static float fade_after(float age, float start_frac) {
    if (age <= start_frac) return 1.0f;
    float span = 1.0f - start_frac;
    if (span < 1e-5f) return 0.0f;
    return 1.0f - smoothstep((age - start_frac) / span);
}

static float ease_out_quad(float t) { t = clampf(t, 0.0f, 1.0f); return 1.0f - (1.0f - t) * (1.0f - t); }
static float ease_in_quad(float t) { t = clampf(t, 0.0f, 1.0f); return t * t; }

// Scale-in with overshoot: ramps 0 -> peak over `up` seconds, then settles peak -> 1 by
// `settle` seconds (absolute item age in seconds, not a fraction of dur).
static float overshoot_scale(float t, float peak, float up, float settle) {
    if (t < up) return smoothstep(t / up) * peak;
    if (t < settle) return lerpf(peak, 1.0f, ease_in_out((t - up) / (settle - up)));
    return 1.0f;
}

static void draw_item(const Uifx *u, int i, Gfx *g) {
    const UifxItem *it = &u->items[i];
    float age = clampf(it->t / it->dur, 0.0f, 1.0f);
    float t = it->t;

    float ox = 0.0f, oy = 0.0f, scale_mul = 1.0f, alpha_mul = 1.0f;
    Vec4 color = it->color;

    switch (it->style) {
    case UIFX_DAMAGE: {
        scale_mul = overshoot_scale(t, 1.35f, 0.12f, 0.25f);
        oy = -40.0f * smoothstep(age);
        float wobble_decay = clampf(1.0f - t / 0.3f, 0.0f, 1.0f);
        ox = sinf(t * 8.0f + it->seed * 31.4f) * 3.0f * wobble_decay;
        alpha_mul = fade_after(age, 0.6f);
        break;
    }
    case UIFX_HEAL: {
        scale_mul = overshoot_scale(t, 1.1f, 0.1f, 0.22f);
        oy = -25.0f * age;
        alpha_mul = fade_after(age, 0.6f);
        break;
    }
    case UIFX_BLOCK: {
        scale_mul = overshoot_scale(t, 1.2f, 0.06f, 0.14f);
        oy = -15.0f * age;
        alpha_mul = fade_after(age, 0.5f);
        break;
    }
    case UIFX_PARRY: {
        scale_mul = overshoot_scale(t, 1.6f, 0.12f, 0.3f);
        if (age > 0.7f) scale_mul = lerpf(scale_mul, 0.8f, smoothstep((age - 0.7f) / 0.3f));
        float flash = t < 0.08f ? (1.0f - t / 0.08f) : 0.0f;
        color = v4(lerpf(color.x, 1.0f, flash), lerpf(color.y, 1.0f, flash), lerpf(color.z, 1.0f, flash), color.w);
        alpha_mul = fade_after(age, 0.7f);
        break;
    }
    case UIFX_FAIL: {
        float decay = clampf(1.0f - t / 0.3f, 0.0f, 1.0f);
        ox = sinf(t * 40.0f + it->seed * 23.0f) * 6.0f * decay;
        alpha_mul = fade_after(age, 0.6f);
        break;
    }
    case UIFX_LABEL: {
        alpha_mul = fade_after(age, 0.7f);
        break;
    }
    case UIFX_BANNER:
        return;  // handled separately in uifx_draw
    }

    Vec4 c = v4(color.x, color.y, color.z, color.w * alpha_mul);
    draw_centered(g, it->x + ox, it->y + oy, it->scale * scale_mul, c, it->text);
}

static void draw_banner(const UifxItem *it, Gfx *g) {
    const float open_dur = 0.15f, close_dur = 0.15f, slide_dur = 0.3f, bar_h = 64.0f, bar_w = 1280.0f;
    float open_frac = smoothstep(it->t / open_dur);
    float close_frac = smoothstep((it->dur - it->t) / close_dur);
    float h_frac = fminf(open_frac, close_frac);
    float h = bar_h * h_frac;
    if (h > 0.0f)
        gfx_ui_rect(g, 0.0f, it->y - h * 0.5f, bar_w, h, v4(0.0f, 0.0f, 0.0f, 0.65f));

    // Accent lines track the top/bottom edge of the opening/closing bar.
    Vec4 line_color = v4(it->color.x, it->color.y, it->color.z, it->color.w * h_frac);
    if (h_frac > 0.0f) {
        gfx_ui_rect(g, 0.0f, it->y - h * 0.5f - 2.0f, bar_w, 2.0f, line_color);
        gfx_ui_rect(g, 0.0f, it->y + h * 0.5f, bar_w, 2.0f, line_color);
    }

    float out_start = fmaxf(it->dur - slide_dur, slide_dur);
    float tx;
    if (it->t < slide_dur)
        tx = lerpf(-300.0f, 640.0f, ease_out_quad(it->t / slide_dur));
    else if (it->t < out_start)
        tx = 640.0f;
    else
        tx = lerpf(640.0f, 1580.0f, ease_in_quad((it->t - out_start) / slide_dur));

    Vec4 c = v4(it->color.x, it->color.y, it->color.z, it->color.w * h_frac);
    draw_centered(g, tx, it->y, it->scale, c, it->text);
}

void uifx_draw(const Uifx *u, Gfx *g) {
    for (int i = 0; i < UIFX_MAX; i++) {
        const UifxItem *it = &u->items[i];
        if (!it->alive) continue;
        if (it->style == UIFX_BANNER) draw_banner(it, g);
        else draw_item(u, i, g);
    }
}

// ---------------------------------------------------------------- projection

bool uifx_project(Mat4 view_proj, Vec3 world, float *out_x, float *out_y) {
    const float *m = view_proj.m;
    float cx = m[0] * world.x + m[4] * world.y + m[8] * world.z + m[12];
    float cy = m[1] * world.x + m[5] * world.y + m[9] * world.z + m[13];
    float cw = m[3] * world.x + m[7] * world.y + m[11] * world.z + m[15];
    if (cw <= 0.01f) return false;
    float ndc_x = cx / cw;
    float ndc_y = cy / cw;
    if (out_x) *out_x = (ndc_x * 0.5f + 0.5f) * 1280.0f;
    if (out_y) *out_y = (0.5f - ndc_y * 0.5f) * 800.0f;
    return true;
}

// ---------------------------------------------------------------- standalone test

#ifdef UIFX_TEST
int main(void) {
    Uifx u;
    uifx_init(&u);

    uifx_spawn(&u, UIFX_DAMAGE, 400, 300, "42", v4(1.0f, 0.9f, 0.2f, 1.0f), 3.5f, 1.0f);
    uifx_spawn(&u, UIFX_HEAL, 500, 300, "+18", v4(0.3f, 1.0f, 0.4f, 1.0f), 3.0f, 1.1f);
    uifx_spawn(&u, UIFX_BLOCK, 600, 300, "BLOCK", v4(0.6f, 0.7f, 0.9f, 1.0f), 1.8f, 0.5f);
    uifx_spawn(&u, UIFX_PARRY, 640, 250, "PERFECT", v4(1.0f, 0.8f, 0.1f, 1.0f), 4.0f, 0.7f);
    uifx_spawn(&u, UIFX_FAIL, 640, 350, "LATE", v4(1.0f, 0.2f, 0.2f, 1.0f), 2.5f, 0.6f);
    uifx_spawn(&u, UIFX_BANNER, 0, 120, "YOUR TURN", v4(1.0f, 1.0f, 1.0f, 1.0f), 3.0f, 1.8f);
    uifx_spawn(&u, UIFX_LABEL, 200, 500, "Charging...", v4(0.8f, 0.8f, 0.8f, 1.0f), 1.5f, 1.2f);

    int alive_at_start = 0;
    for (int i = 0; i < UIFX_MAX; i++) if (u.items[i].alive) alive_at_start++;

    Gfx g;
    memset(&g, 0, sizeof(g));
    const float dt = 1.0f / 60.0f;
    for (float t = 0.0f; t < 2.0f; t += dt) {
        uifx_update(&u, dt);
        uifx_draw(&u, &g);
    }

    int alive_at_end = 0;
    for (int i = 0; i < UIFX_MAX; i++) if (u.items[i].alive) alive_at_end++;

    printf("spawned=7 alive_at_start=%d alive_at_end=%d\n", alive_at_start, alive_at_end);
    printf("rect_calls=%d text_calls=%d text_width_calls=%d\n", g_rect_calls, g_text_calls, g_text_width_calls);
    printf("all items expired: %s\n", alive_at_end == 0 ? "yes" : "no");

    return alive_at_end == 0 ? 0 : 1;
}
#endif
