// See parted.h.
#include "parted.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void say(PartEd *e, const char *s) { snprintf(e->msg, sizeof e->msg, "%s", s); e->msg_t = 3.0f; }

typedef struct ScanCtx { PartEd *e; bool parts; } ScanCtx;
static SDL_EnumerationResult scan_cb(void *ud, const char *dirname, const char *fname) {
    ScanCtx *c = ud; PartEd *e = c->e;
    size_t n = strlen(fname);
    bool is_obj = n > 4 && !strcmp(fname + n - 4, ".obj"), is_part = n > 5 && !strcmp(fname + n - 5, ".part");
    if (!is_obj && !is_part) return SDL_ENUM_CONTINUE;
    char full[1024]; snprintf(full, sizeof full, "%s%s", dirname, fname);
    const char *rel = strstr(full, "/models/"); if (!rel) return SDL_ENUM_CONTINUE;
    char nm[64]; snprintf(nm, sizeof nm, "%s", fname); char *dot = strrchr(nm, '.'); if (dot) *dot = 0;
    if (is_obj && e->nobj < PE_MAX_FILES) { snprintf(e->obj_files[e->nobj], 160, "%s", rel + 1); snprintf(e->obj_names[e->nobj], 48, "%s", nm); e->nobj++; }
    if (is_part && e->nparts < PE_MAX_FILES) { snprintf(e->part_files[e->nparts], 160, "%s", rel + 1); snprintf(e->part_names[e->nparts], 48, "%s", nm); e->nparts++; }
    return SDL_ENUM_CONTINUE;
}

void parted_init_files(PartEd *e) {
    e->nobj = e->nparts = 0;
    const char *dirs[] = { "models/import", "models/parts", "models/own" };
    for (size_t i = 0; i < 3; i++) { char d[640]; snprintf(d, sizeof d, "%s/%s/", HOLLOW_ASSET_DIR, dirs[i]); ScanCtx c = { e, false }; SDL_EnumerateDirectory(d, scan_cb, &c); }
}

void parted_init(PartEd *e) {
    memset(e, 0, sizeof *e);
    parted_init_files(e);
    e->sel = -1; e->snap = true;
    snprintf(e->name, sizeof e->name, "%s", "my_part");
}

void parted_open(PartEd *e) {
    e->scroll = 0; e->name_focus = false; e->dragging = false;
    if (e->doc.n == 0) { Shape *s = &e->doc.shapes[e->doc.n++]; s->kind = SH_BOX; s->pos = v3(0, 0, 0); s->size = v3(0.6f, 0.6f, 0.6f); s->color = v3(0.7f, 0.55f, 0.35f); s->tinted = true; e->sel = 0; }
    say(e, "drag the highlighted shape in the game window; size, turn and colour it here");
}

void parted_push_undo(PartEd *e) {
    if (e->undo_n == PE_UNDO) { memmove(e->undo, e->undo + 1, (PE_UNDO - 1) * sizeof *e->undo); e->undo_n--; }
    e->undo[e->undo_n++] = e->doc; e->dirty = true;
}
static void pop_undo(PartEd *e) {
    if (e->undo_n == 0) { say(e, "nothing to undo"); return; }
    e->doc = e->undo[--e->undo_n]; if (e->sel >= e->doc.n) e->sel = e->doc.n - 1; say(e, "undone");
}

static float snapf(float v, bool on) { return on ? roundf(v / 0.05f) * 0.05f : v; }

static void add_shape(PartEd *e, ShapeKind k, const char *file) {
    if (e->doc.n >= PART_MAX_SHAPES) { say(e, "part is full (64 shapes)"); return; }
    parted_push_undo(e);
    Shape *s = &e->doc.shapes[e->doc.n]; memset(s, 0, sizeof *s);
    s->kind = k; s->size = v3(0.5f, 0.5f, 0.5f); s->color = v3(0.7f, 0.7f, 0.7f); s->tinted = k != SH_OBJ;
    if (k == SH_OBJ) { snprintf(s->file, sizeof s->file, "%s", file); s->size = v3(1, 1, 1); }
    if (e->sel >= 0 && e->sel < e->doc.n) { const Shape *p = &e->doc.shapes[e->sel]; s->pos = v3(p->pos.x, p->pos.y + (p->kind == SH_OBJ ? 0 : p->size.y), p->pos.z); if (k != SH_OBJ) s->color = p->color; }
    e->sel = e->doc.n++;
}

// Minimal text field: letters, digits, underscore, backspace.
static void name_keys(PartEd *e, const Input *in) {
    if (!e->name_focus) return;
    size_t n = strlen(e->name);
    for (int sc = SDL_SCANCODE_A; sc <= SDL_SCANCODE_Z; sc++) if (in->key_down[sc] && n < sizeof e->name - 1) { e->name[n++] = (char)('a' + (sc - SDL_SCANCODE_A)); e->name[n] = 0; }
    for (int sc = SDL_SCANCODE_1; sc <= SDL_SCANCODE_0; sc++) if (in->key_down[sc] && n < sizeof e->name - 1) { e->name[n++] = sc == SDL_SCANCODE_0 ? '0' : (char)('1' + (sc - SDL_SCANCODE_1)); e->name[n] = 0; }
    if ((in->key_down[SDL_SCANCODE_MINUS] || in->key_down[SDL_SCANCODE_SPACE]) && n < sizeof e->name - 1) { e->name[n++] = '_'; e->name[n] = 0; }
    if (in->key_down[SDL_SCANCODE_BACKSPACE] && n > 0) e->name[--n] = 0;
    if (in->key_down[SDL_SCANCODE_RETURN] || in->key_down[SDL_SCANCODE_ESCAPE]) e->name_focus = false;
}

int parted_world(PartEd *e, const Input *in, Vec3 hit, bool hit_ok) {
    int flags = 0;
    if (!hit_ok) { if (!in->mouse_held) e->dragging = false; return 0; }
    Shape *s = e->sel >= 0 && e->sel < e->doc.n ? &e->doc.shapes[e->sel] : NULL;
    if (in->click) {
        // pick: the shape whose footprint contains the hit, nearest to the cursor; else keep the selection
        int best = -1; float bd = 1e9f;
        for (int i = 0; i < e->doc.n; i++) {
            const Shape *q = &e->doc.shapes[i];
            float rx = fmaxf(fabsf(q->size.x), 0.1f) * 0.5f + 0.05f, rz = fmaxf(fabsf(q->size.z), 0.1f) * 0.5f + 0.05f;
            float dx = hit.x - q->pos.x, dz = hit.z - q->pos.z;
            if (fabsf(dx) <= rx && fabsf(dz) <= rz) { float d = dx * dx + dz * dz - q->pos.y * 0.001f; if (d < bd) { bd = d; best = i; } }
        }
        if (best >= 0) { e->sel = best; s = &e->doc.shapes[best]; }
        if (s && best >= 0) { parted_push_undo(e); e->dragging = true; e->drag_off = v3(s->pos.x - hit.x, 0, s->pos.z - hit.z); }
    }
    if (e->dragging && s && in->mouse_held) {
        float nx = snapf(hit.x + e->drag_off.x, e->snap), nz = snapf(hit.z + e->drag_off.z, e->snap);
        if (nx != s->pos.x || nz != s->pos.z) { s->pos.x = nx; s->pos.z = nz; flags |= PE_REBUILD; e->dirty = true; }
    }
    if (!in->mouse_held) e->dragging = false;
    return flags;
}

#define M 12.0f
#define G 6.0f
#define ROW 30.0f

int parted_panel(PartEd *e, Ui *ui, const Input *keys, float w, float h) {
    int flags = 0;
    if (e->msg_t > 0) e->msg_t -= 1.0f / 60.0f;
    name_keys(e, keys);
    if (!e->name_focus) {
        if (keys->ctrl && keys->key_down[SDL_SCANCODE_Z]) { pop_undo(e); flags |= PE_REBUILD; }
        if (keys->ctrl && keys->key_down[SDL_SCANCODE_S]) flags |= PE_SAVE;
        if (!keys->ctrl && keys->key_down[SDL_SCANCODE_X] && e->sel >= 0 && e->sel < e->doc.n) { parted_push_undo(e); for (int k = e->sel; k < e->doc.n - 1; k++) e->doc.shapes[k] = e->doc.shapes[k + 1]; e->doc.n--; if (e->sel >= e->doc.n) e->sel = e->doc.n - 1; flags |= PE_REBUILD; }
        if (!keys->ctrl && keys->key_down[SDL_SCANCODE_D] && e->sel >= 0 && e->sel < e->doc.n && e->doc.n < PART_MAX_SHAPES) { parted_push_undo(e); e->doc.shapes[e->doc.n] = e->doc.shapes[e->sel]; e->doc.shapes[e->doc.n].pos.x += 0.3f; e->sel = e->doc.n++; flags |= PE_REBUILD; }
    }
    const float head_h = M + 32 + ROW + 10, foot_h = 56;
    float view_h = h - head_h - foot_h;
    UiInput saved = ui->in;
    bool in_content = ui->in.my >= head_h && ui->in.my < h - foot_h;
    if (in_content && ui->in.wheel != 0) e->scroll = fmaxf(0, e->scroll - ui->in.wheel * 40);
    if (!in_content) { ui->in.mx = ui->in.my = -1e6f; ui->in.pressed = false; ui->in.wheel = 0; }
    float x = M, y = head_h - e->scroll, top = y;
    float cw = (w - 2 * M - G) / 2; bool two = w >= 640;

    // ---- add shapes
    ui_label(ui, x, y, "ADD   a new shape lands on top of the selected one", v4(1, 0.85f, 0.4f, 1)); y += 24;
    { const char *k[] = { "BOX", "CYLINDER", "SPHERE", "WEDGE" }; float bw = (w - 2 * M - 3 * G) / 4;
      for (int i = 0; i < 4; i++) if (ui_button(ui, x + i * (bw + G), y, bw, ROW, k[i])) { add_shape(e, (ShapeKind)i, NULL); flags |= PE_REBUILD; }
      y += ROW + G; }
    if (e->nobj) {
        ui_label(ui, x, y, "your OBJ imports", v4(0.7f, 0.68f, 0.65f, 1)); y += 22;
        int cols = w >= 900 ? 4 : w >= 640 ? 3 : 2; float bw = (w - 2 * M - (cols - 1) * G) / cols;
        for (int i = 0; i < e->nobj; i++) if (ui_button(ui, x + (i % cols) * (bw + G), y + (i / cols) * (ROW + G), bw, ROW, e->obj_names[i])) { add_shape(e, SH_OBJ, e->obj_files[i]); flags |= PE_REBUILD; }
        y += (ROW + G) * ((e->nobj + cols - 1) / cols);
    }
    y += 8;
    // ---- shape list
    ui_label(ui, x, y, "SHAPES   click to select (or click it in the game window)", v4(1, 0.85f, 0.4f, 1)); y += 24;
    { int cols = w >= 900 ? 4 : w >= 640 ? 3 : 2; float bw = (w - 2 * M - (cols - 1) * G) / cols;
      for (int i = 0; i < e->doc.n; i++) { const Shape *s = &e->doc.shapes[i]; char nm[64]; const char *fn = strrchr(s->file, '/');
          snprintf(nm, sizeof nm, "%d %s", i + 1, s->kind == SH_OBJ ? (fn ? fn + 1 : s->file) : shape_kind_name(s->kind));
          bool on = e->sel == i; float bx = x + (i % cols) * (bw + G), by = y + (i / cols) * (ROW + G);
          if (ui_toggle(ui, bx, by, bw, ROW, nm, &on)) e->sel = on ? i : -1;
          if (s->tinted) gfx_ui_rect(ui->g, bx + bw - 24, by + 7, 16, ROW - 14, v4(s->color.x, s->color.y, s->color.z, 1)); }
      y += (ROW + G) * ((e->doc.n + cols - 1) / cols) + 8; }
    // ---- selected shape
    if (e->sel >= 0 && e->sel < e->doc.n) {
        Shape *s = &e->doc.shapes[e->sel];
        { float bw = (w - 2 * M - 3 * G) / 4;
          if (ui_button(ui, x, y, bw, ROW, "DUPLICATE D")) { parted_push_undo(e); e->doc.shapes[e->doc.n] = *s; e->doc.shapes[e->doc.n].pos.x += 0.3f; e->sel = e->doc.n++; flags |= PE_REBUILD; }
          if (ui_button(ui, x + bw + G, y, bw, ROW, "MIRROR X")) { parted_push_undo(e); if (e->doc.n < PART_MAX_SHAPES) { Shape c = *s; c.pos.x = -c.pos.x; c.yaw = -c.yaw; c.roll = -c.roll; c.size.x = -c.size.x; e->doc.shapes[e->doc.n] = c; e->sel = e->doc.n++; } flags |= PE_REBUILD; }
          if (ui_button(ui, x + 2 * (bw + G), y, bw, ROW, "DELETE X")) { parted_push_undo(e); for (int k = e->sel; k < e->doc.n - 1; k++) e->doc.shapes[k] = e->doc.shapes[k + 1]; e->doc.n--; if (e->sel >= e->doc.n) e->sel = e->doc.n - 1; flags |= PE_REBUILD; }
          if (ui_toggle(ui, x + 3 * (bw + G), y, bw, ROW, "snap 5 cm", &e->snap)) {}
          y += ROW + 10; }
        if (e->sel >= 0) {
            s = &e->doc.shapes[e->sel];
            char t[96]; snprintf(t, sizeof t, "SHAPE %d   %s   position (m), size (m), rotation (deg)", e->sel + 1, s->kind == SH_OBJ ? s->file : shape_kind_name(s->kind));
            ui_label_fit(ui, x, y, w - 2 * M, t, v4(1, 0.85f, 0.4f, 1)); y += 24;
            float c2 = two ? x + cw + G : x;
            #define SL(col, lbl, ptr, lo, hi) do { float _y = (col) && two ? y : y; if (ui_slider(ui, (col) ? c2 : x, _y + ((col) && !two ? 26 : 0), cw, lbl, ptr, lo, hi)) { flags |= PE_REBUILD; e->dirty = true; } } while (0)
            SL(0, "x", &s->pos.x, -2, 2); SL(1, "size x", &s->size.x, -3, 3); y += two ? 26 : 52;
            SL(0, "y", &s->pos.y, -1, 3); SL(1, "size y", &s->size.y, 0.02f, 3); y += two ? 26 : 52;
            SL(0, "z", &s->pos.z, -2, 2); SL(1, "size z", &s->size.z, -3, 3); y += two ? 26 : 52;
            SL(0, "yaw", &s->yaw, -180, 180); SL(1, "pitch", &s->pitch, -180, 180); y += two ? 26 : 52;
            SL(0, "roll", &s->roll, -180, 180); y += 30;
            #undef SL
            if (e->snap) { s->pos.x = snapf(s->pos.x, true); s->pos.y = snapf(s->pos.y, true); s->pos.z = snapf(s->pos.z, true); s->size.x = snapf(s->size.x, true); s->size.y = fmaxf(0.05f, snapf(s->size.y, true)); s->size.z = snapf(s->size.z, true); s->yaw = roundf(s->yaw / 5) * 5; s->pitch = roundf(s->pitch / 5) * 5; s->roll = roundf(s->roll / 5) * 5; }
            if (s->kind == SH_OBJ) { if (ui_toggle(ui, x, y, cw, ROW, "recolour this import", &s->tinted)) flags |= PE_REBUILD; y += ROW + 6; }
            if (s->tinted) { if (ui_color(ui, x, y, two ? cw : w - 2 * M, "colour", &s->color, 1)) { flags |= PE_REBUILD; e->dirty = true; } y += 3 * 26 + 10; }
        }
    } else { ui_label(ui, x, y, "no shape selected", v4(0.6f, 0.58f, 0.55f, 1)); y += 24; }
    // ---- open a saved part
    if (e->nparts) {
        ui_label(ui, x, y, "OPEN   a saved part (replaces the bench)", v4(1, 0.85f, 0.4f, 1)); y += 24;
        int cols = w >= 900 ? 4 : w >= 640 ? 3 : 2; float bw = (w - 2 * M - (cols - 1) * G) / cols;
        for (int i = 0; i < e->nparts; i++) if (ui_button(ui, x + (i % cols) * (bw + G), y + (i / cols) * (ROW + G), bw, ROW, e->part_names[i])) {
            char path[640]; snprintf(path, sizeof path, "%s/%s", HOLLOW_ASSET_DIR, e->part_files[i]);
            parted_push_undo(e); if (part_load(&e->doc, path)) { snprintf(e->name, sizeof e->name, "%s", e->part_names[i]); e->sel = e->doc.n ? 0 : -1; e->dirty = false; say(e, "opened"); } flags |= PE_REBUILD;
        }
        y += (ROW + G) * ((e->nparts + cols - 1) / cols) + 8;
    }
    float content_h = y - top, max_scroll = fmaxf(0, content_h - view_h); e->scroll = fminf(e->scroll, max_scroll);
    ui->in = saved;
    if (in_content) { ui->in.mx = ui->in.my = -1e6f; ui->in.pressed = false; }
    // ---- header
    Vec4 bg = v4(0.05f, 0.05f, 0.07f, 1);
    gfx_ui_rect(ui->g, 0, 0, w, head_h - 4, bg);
    y = M;
    ui_header(ui, x, y, e->dirty ? "PART EDITOR  * unsaved" : "PART EDITOR"); y += 32;
    {
        float nw = fminf(240, w * 0.38f);
        bool inside = saved.mx >= x && saved.mx < x + nw && saved.my >= y && saved.my < y + ROW;
        if (saved.pressed) e->name_focus = inside;
        gfx_ui_rect(ui->g, x, y, nw, ROW, e->name_focus ? v4(0.24f, 0.23f, 0.28f, 1) : v4(0.16f, 0.16f, 0.19f, 1));
        gfx_ui_rect(ui->g, x, y + ROW - 2, nw, 2, e->name_focus ? v4(1, 0.85f, 0.4f, 1) : v4(0.36f, 0.35f, 0.40f, 1));
        char s[64]; snprintf(s, sizeof s, "%s%s", e->name, e->name_focus ? "_" : "");
        gfx_ui_text(ui->g, x + 8, y + ROW * 0.5f - gfx_ui_line_h(1.1f) * 0.5f, 1.1f, v4(0.9f, 0.9f, 0.88f, 1), s);
        float bx = x + nw + G, bw = (w - M - bx - 2 * G) / 3;
        if (ui_button(ui, bx, y, bw, ROW, "SAVE ^S")) flags |= PE_SAVE;
        if (ui_button(ui, bx + bw + G, y, bw, ROW, "UNDO ^Z")) { pop_undo(e); flags |= PE_REBUILD; }
        if (ui_button(ui, bx + 2 * (bw + G), y, bw, ROW, "NEW")) { parted_push_undo(e); e->doc.n = 0; e->sel = -1; parted_open(e); flags |= PE_REBUILD; }
    }
    if (max_scroll > 0) { float bh = fmaxf(16, view_h * view_h / content_h), by = head_h + (view_h - bh) * (e->scroll / max_scroll); gfx_ui_rect(ui->g, w - 6, head_h, 4, view_h, v4(0.12f, 0.12f, 0.14f, 1)); gfx_ui_rect(ui->g, w - 6, by, 4, bh, v4(0.45f, 0.43f, 0.40f, 1)); }
    gfx_ui_rect(ui->g, 0, h - foot_h, w, foot_h, bg);
    if (e->msg_t > 0) ui_label_fit(ui, x, h - 50, w - 2 * M, e->msg, v4(1, 0.85f, 0.4f, 1));
    ui_label_fit(ui, x, h - 28, w - 2 * M, "game window: click a shape, drag it on the bench   here: X delete, D duplicate, Ctrl+Z undo, Ctrl+S save", v4(0.5f, 0.48f, 0.45f, 1));
    ui->in = saved;
    return flags;
}
