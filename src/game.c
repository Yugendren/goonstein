#include "game.h"
#include "gfx.h"
#include <string.h>
static Gfx gfx; static Texture checker;
void game_init(Game *g) { memset(g, 0, sizeof *g); }
bool game_init_gfx(Game *g, Platform *pf) {
    (void)g;
    if (!gfx_init(&gfx, pf, 640, 400)) return false;
    unsigned char px[16*16*4];
    for (int i = 0; i < 256; i++) { int c = ((i & 8) ^ ((i >> 4) & 8)) ? 200 : 90; px[i*4]=c; px[i*4+1]=c; px[i*4+2]=c; px[i*4+3]=255; }
    checker = gfx_texture_create(&gfx, px, 16, 16);
    return true;
}
void game_tick(Game *g, const Input *in, double dt) { g->time += dt; g->tick++; g->px += in->move_x * 3.0f * (float)dt; g->py += in->move_y * 3.0f * (float)dt; }
void game_render(Game *g, Platform *pf, float alpha) {
    (void)alpha;
    Mat4 proj = m4_perspective(60 * DEG2RAD, 640.0f / 400.0f, 0.1f, 100.0f);
    Mat4 view = m4_look_at(v3(4, 3, 6), v3(0, 0.5f, 0), v3(0, 1, 0));
    FrameParams fp = { .view_proj = m4_mul(proj, view), .fog_color = v3(0.05f, 0.04f, 0.06f), .fog_near = 4, .fog_far = 14,
                       .light_dir = v3_norm(v3(-0.4f, -1, -0.3f)), .ambient = 0.25f, .light_color = v3(0.9f, 0.85f, 0.8f) };
    gfx_begin(&gfx, pf, &fp);
    gfx_draw_box(&gfx, &checker, v3(0, -0.5f, 0), v3(12, 1, 12), 0, v4(0.6f, 0.6f, 0.7f, 1), 1);
    gfx_draw_box(&gfx, &checker, v3(g->px, 0.5f, g->py), v3(1, 1, 1), (float)g->time, v4(1, 0.6f, 0.5f, 1), 0);
    gfx_draw_box_wire(&gfx, v3(2, 0.5f, -2), v3(1, 1, 1), v4(0, 1, 0, 1));
    gfx_ui_rect(&gfx, 10, 10, 200, 8, v4(0.6f, 0.1f, 0.1f, 1));
    gfx_ui_text(&gfx, 10, 24, 1.0f, v4(1, 1, 1, 1), "hollow smoke test");
    gfx_end(&gfx, pf, &(PostParams){ .grain = 0.06f, .vignette = 0.5f, .fade = 1.0f }, g->time);
}
void game_shutdown(Game *g) { (void)g; gfx_texture_destroy(&gfx, &checker); gfx_shutdown(&gfx); }
void game_screenshot(Game *g, const char *path) { (void)g; gfx_screenshot(&gfx, path); }
