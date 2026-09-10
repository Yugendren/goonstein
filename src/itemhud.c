// Item HUD: the look-at prompt, the carry line and its throw charge bar, the "-$VALUE" flash
// after something breaks, and the boat's hold readout. Called from game.c's draw_hud during
// GS_EXPLORE only (the caller guards the state); draws nothing at all when there is nothing to
// say. Read-only: `g` is non-const only because the Gfx calls need it.
#include <stdio.h>   // pulls in NULL before phys.h (via items.h) uses it
#include <math.h>
#include "items.h"
#include "game.h"

// Local copies of the idioms draw_hud already uses: gfx_ui_text anchors top-left, so centring
// means measuring first, and a "bar" is a black surround over a dark back over a filled front.
static void text_center(Gfx *g, float cx, float y, float scale, Vec4 c, const char *s) {
    float w = gfx_ui_text_width(scale, s);
    gfx_ui_text(g, cx - w * 0.5f, y, scale, c, s);
}

static void bar(Gfx *g, float x, float y, float w, float h, float k, Vec4 back, Vec4 front) {
    gfx_ui_rect(g, x - 1, y - 1, w + 2, h + 2, v4(0, 0, 0, 0.7f));
    gfx_ui_rect(g, x, y, w, h, back);
    gfx_ui_rect(g, x, y, w * clampf(k, 0, 1), h, front);
}

void items_draw_hud(struct Game *g) {
    Gfx *x = &g->gfx;
    const float W = INTERNAL_W, H = INTERNAL_H;
    Items *its = &g->items;
    Carry *carry = &its->carry[g->local];
    Vec4 white = v4(0.9f, 0.88f, 0.85f, 1), dim = v4(0.6f, 0.58f, 0.55f, 1);

    // 1. Look prompt. Only shown with empty hands, so it never fights the carry line for the
    // same patch of screen.
    if (its->look_at >= 0 && carry->item < 0) {
        const ItemDef *def = item_def(its, &its->it[its->look_at]);
        float cy = H * 0.5f + 40;
        const char *hint = "E   hold to grab";
        float w1 = gfx_ui_text_width(1.4f, def->display);
        float w2 = gfx_ui_text_width(1.1f, hint);
        float w = w1 > w2 ? w1 : w2;
        gfx_ui_rect(x, W * 0.5f - w * 0.5f - 10, cy - 6, w + 20, 46, v4(0, 0, 0, 0.55f));
        text_center(x, W * 0.5f, cy, 1.4f, v4(0.95f, 0.9f, 0.8f, 1), def->display);
        text_center(x, W * 0.5f, cy + 22, 1.1f, dim, hint);
    }

    // 2. Carry line, bottom centre: what's in hand, and how to get rid of it. "(two-handed)" is
    // appended in amber, so it can't just ride along inside text_center's single-colour string.
    if (carry->item >= 0) {
        const ItemDef *def = item_def(its, &its->it[carry->item]);
        float cy = H - 92;
        char line1[96];
        snprintf(line1, sizeof line1, "CARRYING  %s", def->display);
        const char *tag = def->two_handed ? "  (two-handed)" : "";
        float w1 = gfx_ui_text_width(1.3f, line1), w2 = def->two_handed ? gfx_ui_text_width(1.3f, tag) : 0;
        float start = W * 0.5f - (w1 + w2) * 0.5f;
        gfx_ui_text(x, start, cy, 1.3f, white, line1);
        if (def->two_handed) gfx_ui_text(x, start + w1, cy, 1.3f, v4(0.9f, 0.65f, 0.25f, 1), tag);
        text_center(x, W * 0.5f, cy + 20, 1.0f, dim, "LMB  hold to throw      RMB / E  drop");
    }

    // 3. Throw charge bar, centred under the carry line, pale yellow warming to orange as it
    // nears the cap.
    if (carry->charging && carry->charge > 0) {
        float k = clampf(carry->charge / ITEM_CHARGE_MAX, 0, 1);
        Vec4 lo = v4(1.0f, 0.95f, 0.6f, 1), hi = v4(1.0f, 0.55f, 0.15f, 1);
        Vec4 front = v4(lo.x + (hi.x - lo.x) * k, lo.y + (hi.y - lo.y) * k, lo.z + (hi.z - lo.z) * k, 1);
        bar(x, W * 0.5f - 90, H - 70 - 4, 180, 8, k, v4(0.15f, 0.12f, 0.08f, 1), front);
    }

    // 4a. "-$VALUE" after a break, fading out over the last second it's shown.
    if (its->lost_t > 0) {
        char s[32]; snprintf(s, sizeof s, "-$%d", its->lost_value);
        float a = fminf(1, its->lost_t);
        text_center(x, W * 0.5f, H * 0.5f - 90, 2.0f, v4(0.85f, 0.15f, 0.15f, a), s);
    }

    // 4b. The boat's hold, while standing near it: what's aboard and what it's worth.
    Vec3 hc;
    if (items_hold_center(g, &hc)) {
        Vec3 p = PLAYER(g).c.pos;
        float dx = p.x - hc.x, dz = p.z - hc.z;
        if (dx * dx + dz * dz <= 18.0f * 18.0f) {
            char s[64]; snprintf(s, sizeof s, "HOLD:  %d items,  $%d", its->hold_count, its->hold_value);
            float w = gfx_ui_text_width(1.3f, s);
            // Below the opening hint lines, which live at y 30 and 44 for the first few seconds.
            gfx_ui_rect(x, W * 0.5f - w * 0.5f - 10, 58 - 6, w + 20, 26, v4(0, 0, 0, 0.55f));
            text_center(x, W * 0.5f, 58, 1.3f, white, s);
        }
    }
}
