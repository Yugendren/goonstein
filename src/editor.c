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
static void plot_line(Editor *e, PixFrame *f, int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) { put_px(e, f, x0, y0, c); if (x0 == x1 && y0 == y1) break; int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } }
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

// ---------------------------------------------------------------- ui geometry

#define PANEL_X 680.0f
static bool inside(float px, float py, float x, float y, float w, float h) { return px >= x && px <= x + w && py >= y && py <= y + h; }
static Vec4 col4(uint32_t c) { return v4((c & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, ((c >> 16) & 255) / 255.0f, ((c >> 24) & 255) / 255.0f); }

typedef struct Btn { float x, y, w, h; } Btn;
static Btn btn_anim(int i) { return (Btn){ PANEL_X, 110 + i * 26.0f, 120, 24 }; }
static Btn btn_tool(int i) { return (Btn){ PANEL_X + 130 + i * 44.0f, 110, 40, 30 }; }
static Btn btn_pal(int i) { return (Btn){ PANEL_X + 130 + (i % 11) * 34.0f, 150 + (i / 11) * 34.0f, 30, 30 }; }
static Btn btn_dir(int i) { return (Btn){ PANEL_X + 130 + i * 60.0f, 262, 56, 26 }; }
static Btn btn_frame(int i) { return (Btn){ PANEL_X + i * 72.0f, 330, 68, 68 }; }
static Btn btn_action(int i) { return (Btn){ PANEL_X + (i % 4) * 140.0f, 420 + (i / 4) * 34.0f, 134, 30 }; }
static const char *ACTIONS[] = { "SAVE  ^S", "UNDO  ^Z", "REDO  ^Y", "PLAY  Spc", "+FRAME N", "DUP   D", "-FRAME X", "CONTACT K",
                                 "ONION O", "MIRROR M", "GRID  H", "COPY>ALL C", "FLIP L<>R F", "+ANIM  A", "-ANIM", "FPS -/+" };
#define N_ACTIONS 16

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
    if (in->key_down[SDL_SCANCODE_O]) e->onion = !e->onion;
    if (in->key_down[SDL_SCANCODE_M]) { e->mirror_x = !e->mirror_x; say(e, e->mirror_x ? "mirror on" : "mirror off"); }
    if (in->key_down[SDL_SCANCODE_H]) e->grid = !e->grid;
    if (in->key_down[SDL_SCANCODE_SPACE]) { e->playing = !e->playing; e->play_t = 0; }
    if (in->key_down[SDL_SCANCODE_LEFTBRACKET] && a) e->frame = (e->frame + a->nframes - 1) % a->nframes;
    if (in->key_down[SDL_SCANCODE_RIGHTBRACKET] && a) e->frame = (e->frame + 1) % a->nframes;
    if (in->key_down[SDL_SCANCODE_N]) do_action(e, 4, in);
    if (in->key_down[SDL_SCANCODE_D]) do_action(e, 5, in);
    if (in->key_down[SDL_SCANCODE_X]) do_action(e, 6, in);
    if (in->key_down[SDL_SCANCODE_K]) do_action(e, 7, in);
    if (in->key_down[SDL_SCANCODE_C]) copy_to_all_dirs(e);
    if (in->key_down[SDL_SCANCODE_F]) flip_into_opposite(e);
    if (in->key_down[SDL_SCANCODE_A] && !in->ctrl) do_action(e, 13, in);
    if (in->key_down[SDL_SCANCODE_1]) e->dir = 0; if (in->key_down[SDL_SCANCODE_2]) e->dir = 1;
    if (in->key_down[SDL_SCANCODE_3]) e->dir = 2; if (in->key_down[SDL_SCANCODE_4]) e->dir = 3;
    if (in->key_down[SDL_SCANCODE_UP]) nudge(e, 0, -1); if (in->key_down[SDL_SCANCODE_DOWN]) nudge(e, 0, 1);
    if (in->key_down[SDL_SCANCODE_LEFT]) nudge(e, -1, 0); if (in->key_down[SDL_SCANCODE_RIGHT]) nudge(e, 1, 0);
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
    PixFrame *f = cur_frame(e);
    bool lmb = in->mouse_held, rmb = in->rmouse_held;
    if (f && e->hover_x >= 0 && (lmb || rmb)) {
        EdTool tool = rmb ? TOOL_ERASER : e->tool;
        uint32_t c = tool == TOOL_ERASER ? 0 : e->color;
        if (!e->stroke_open) {
            e->stroke_open = true;
            if (tool == TOOL_PICK) { uint32_t p = f->px[e->hover_y * PIX_MAX_SIZE + e->hover_x]; if (p) { e->color = p; e->pal = -1; for (int i = 0; i < 33; i++) if (PALETTE[i] == p) e->pal = i; } }
            else if (tool == TOOL_FILL) { snapshot(e); flood(e, f, e->hover_x, e->hover_y, c); e->dirty = true; }
            else if (tool == TOOL_LINE) { if (!e->line_armed) { e->line_armed = true; e->line_x0 = e->hover_x; e->line_y0 = e->hover_y; } else { snapshot(e); plot_line(e, f, e->line_x0, e->line_y0, e->hover_x, e->hover_y, c); e->line_armed = false; e->dirty = true; } }
            else { snapshot(e); put_px(e, f, e->hover_x, e->hover_y, c); e->dirty = true; }
            e->last_x = e->hover_x; e->last_y = e->hover_y;
        } else if (tool == TOOL_PENCIL || tool == TOOL_ERASER) {
            if (e->hover_x != e->last_x || e->hover_y != e->last_y) { plot_line(e, f, e->last_x, e->last_y, e->hover_x, e->hover_y, c); e->last_x = e->hover_x; e->last_y = e->hover_y; e->dirty = true; }
        }
    }
    if (!lmb && !rmb) e->stroke_open = false;

    // Clicks on the panel
    if (in->click) {
        for (int i = 0; i < e->doc.nanims; i++) { Btn b = btn_anim(i); if (inside(mx, my, b.x, b.y, b.w, b.h)) { e->anim = i; e->frame = 0; e->playing = false; } }
        for (int i = 0; i < 5; i++) { Btn b = btn_tool(i); if (inside(mx, my, b.x, b.y, b.w, b.h)) e->tool = (EdTool)i; }
        for (int i = 0; i < 33; i++) { Btn b = btn_pal(i); if (inside(mx, my, b.x, b.y, b.w, b.h)) { e->pal = i; e->color = PALETTE[i]; if (i == 0) e->tool = TOOL_ERASER; else if (e->tool == TOOL_ERASER) e->tool = TOOL_PENCIL; } }
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

static void draw_frame_px(Gfx *g, const PixAnim *a, const PixFrame *f, float x, float y, float z, float alpha) {
    for (int py = 0; py < a->fh; py++) for (int px = 0; px < a->fw; px++) {
        uint32_t c = f->px[py * PIX_MAX_SIZE + px];
        if (!(c >> 24)) continue;
        Vec4 col = col4(c); col.w *= alpha;
        gfx_ui_rect(g, x + px * z, y + py * z, z, z, col);
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
    { char s[200]; snprintf(s, sizeof s, "tool %s%s   B pencil  E eraser  G fill  I pick  L line   [ ] frames   1-4 dirs   arrows nudge   wheel colour   right-drag erases",
        (const char *[]){"PENCIL", "ERASER", "FILL", "PICK", "LINE"}[e->tool], e->mirror_x ? " (mirror)" : "");
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
        draw_frame_px(g, a, pix_frame(a, e->dir, e->frame), cx, cy, z, 1.0f);
        if (e->grid && e->zoom >= 6) {
            for (int px = 0; px <= a->fw; px += 8) gfx_ui_rect(g, cx + px * z - 1, cy, 1, a->fh * z, v4(1, 1, 1, 0.12f));
            for (int py = 0; py <= a->fh; py += 8) gfx_ui_rect(g, cx, cy + py * z - 1, a->fw * z, 1, v4(1, 1, 1, 0.12f));
            gfx_ui_rect(g, cx + a->fw / 2 * z - 1, cy, 1, a->fh * z, v4(1, 0.8f, 0.3f, 0.25f));
        }
        if (e->hover_x >= 0) {
            gfx_ui_rect(g, cx + e->hover_x * z, cy + e->hover_y * z, z, 2, v4(1, 1, 1, 0.8f)); gfx_ui_rect(g, cx + e->hover_x * z, cy + (e->hover_y + 1) * z - 2, z, 2, v4(1, 1, 1, 0.8f));
            gfx_ui_rect(g, cx + e->hover_x * z, cy + e->hover_y * z, 2, z, v4(1, 1, 1, 0.8f)); gfx_ui_rect(g, cx + (e->hover_x + 1) * z - 2, cy + e->hover_y * z, 2, z, v4(1, 1, 1, 0.8f));
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
    for (int i = 0; i < 5; i++) { Btn b = btn_tool(i); bool sel = (int)e->tool == i;
        gfx_ui_rect(g, b.x, b.y, b.w, b.h, sel ? v4(0.3f, 0.27f, 0.2f, 1) : v4(0.16f, 0.16f, 0.19f, 1));
        gfx_ui_text(g, b.x + 6, b.y + 10, 1.1f, sel ? acc : white, (const char *[]){"PEN", "ERS", "FIL", "PIK", "LIN"}[i]); }
    // Palette
    for (int i = 0; i < 33; i++) { Btn b = btn_pal(i);
        if (i == 0) { gfx_ui_rect(g, b.x, b.y, b.w, b.h, v4(0.2f, 0.2f, 0.23f, 1)); gfx_ui_rect(g, b.x + 6, b.y + 13, 18, 4, v4(0.8f, 0.3f, 0.3f, 1)); }
        else gfx_ui_rect(g, b.x, b.y, b.w, b.h, col4(PALETTE[i]));
        if (i == e->pal) { gfx_ui_rect(g, b.x - 3, b.y - 3, b.w + 6, 3, white); gfx_ui_rect(g, b.x - 3, b.y + b.h, b.w + 6, 3, white); gfx_ui_rect(g, b.x - 3, b.y - 3, 3, b.h + 6, white); gfx_ui_rect(g, b.x + b.w, b.y - 3, 3, b.h + 6, white); } }
    // Directions
    if (a) for (int i = 0; i < a->ndirs; i++) { Btn b = btn_dir(i); bool sel = i == e->dir;
        gfx_ui_rect(g, b.x, b.y, b.w, b.h, sel ? v4(0.3f, 0.27f, 0.2f, 1) : v4(0.16f, 0.16f, 0.19f, 1));
        gfx_ui_text(g, b.x + 8, b.y + 8, 1.1f, sel ? acc : white, (const char *[]){"DOWN 1", "UP 2", "LEFT 3", "RIGHT 4"}[i]); }
    // Frames strip
    gfx_ui_text(g, PANEL_X, 312, 1.0f, dim, "FRAMES   ( N new  D dup  X delete  K contact )");
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
        float px = PANEL_X, py = 560;
        gfx_ui_text(g, px, py - 16, 1.0f, dim, "PREVIEW  (Space plays)");
        gfx_ui_rect(g, px, py, a->fw * 4.0f + 8, a->fh * 4.0f + 8, v4(0.12f, 0.12f, 0.15f, 1));
        draw_frame_px(g, a, pix_frame(a, e->dir, pf), px + 4, py + 4, 4, 1.0f);
        gfx_ui_rect(g, px + a->fw * 4.0f + 20, py, a->fw + 8.0f, a->fh + 8.0f, v4(0.12f, 0.12f, 0.15f, 1));
        draw_frame_px(g, a, pix_frame(a, e->dir, pf), px + a->fw * 4.0f + 24, py + 4, 1, 1.0f);
        // all four directions of this frame, small
        for (int d = 0; d < a->ndirs; d++) draw_frame_px(g, a, pix_frame(a, d, pf), px + a->fw * 4.0f + 60 + d * (a->fw * 2.0f + 6), py + 4, 2, 1.0f);
    }
    if (e->msg_t > 0) gfx_ui_text(g, 40, 760, 1.3f, acc, e->msg);
    gfx_ui_text(g, 40, 780, 1.0f, dim, "Ctrl+S save   Ctrl+Z undo   Ctrl+Y redo   C copy frame to all dirs   F mirror left<>right   A add anim   -/+ fps   Esc quit");
}
