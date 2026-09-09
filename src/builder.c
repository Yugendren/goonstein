// See builder.h. The panel is immediate-mode (widgets.c); the game owns the live CharModel and
// applies the flags this returns.
#include "builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void borrow_scan(Builder *b);

static void say(Builder *b, const char *s) { snprintf(b->msg, sizeof b->msg, "%s", s); b->msg_t = 3.0f; }

// ---------------------------------------------------------------- model discovery

static SDL_EnumerationResult scan_cb(void *ud, const char *dirname, const char *fname) {
    Builder *b = ud;
    size_t n = strlen(fname);
    if (!(n > 4 && !strcmp(fname + n - 4, ".glb")) || b->nfiles >= BLD_MAX_FILES) return SDL_ENUM_CONTINUE;
    char full[1024]; snprintf(full, sizeof full, "%s%s", dirname, fname);
    const char *rel = strstr(full, "/models/"); if (!rel) return SDL_ENUM_CONTINUE;
    snprintf(b->files[b->nfiles], sizeof b->files[0], "%s", rel + 1);
    char nm[64]; snprintf(nm, sizeof nm, "%s", fname); char *dot = strchr(nm, '.'); if (dot) *dot = 0;
    snprintf(b->names[b->nfiles], sizeof b->names[0], "%s", nm);
    b->nfiles++;
    return SDL_ENUM_CONTINUE;
}

static SDL_EnumerationResult scan_parts_cb(void *ud, const char *dirname, const char *fname) {
    Builder *b = ud;
    size_t n = strlen(fname);
    bool ok = (n > 4 && !strcmp(fname + n - 4, ".obj")) || (n > 4 && !strcmp(fname + n - 4, ".glb")) || (n > 5 && !strcmp(fname + n - 5, ".gltf")) || (n > 5 && !strcmp(fname + n - 5, ".part"));
    if (!ok || b->npart_files >= BLD_MAX_FILES) return SDL_ENUM_CONTINUE;
    char full[1024]; snprintf(full, sizeof full, "%s%s", dirname, fname);
    const char *rel = strstr(full, "/models/"); if (!rel) return SDL_ENUM_CONTINUE;
    snprintf(b->part_files[b->npart_files], sizeof b->part_files[0], "%s", rel + 1);
    char nm[64]; snprintf(nm, sizeof nm, "%s", fname); char *dot = strchr(nm, '.'); if (dot) *dot = 0;
    snprintf(b->part_names[b->npart_files], sizeof b->part_names[0], "%s", nm);
    b->npart_files++;
    return SDL_ENUM_CONTINUE;
}

void builder_init(Builder *b) {
    memset(b, 0, sizeof *b);
    // parts you can attach to a bone: your own exports in import/ and parts/
    { const char *pd[] = { "models/import", "models/parts", "models/own" }; for (size_t i = 0; i < 3; i++) { char d[640]; snprintf(d, sizeof d, "%s/%s/", HOLLOW_ASSET_DIR, pd[i]); SDL_EnumerateDirectory(d, scan_parts_cb, b); } }
    b->bone_sel = -1; b->attach_sel = -1; b->borrow_file_sel = -1;
    // rigged characters: the kaykit root and assets/models/characters (yours)
    const char *dirs[] = { "models/kaykit", "models/characters", "models/import" };
    for (size_t i = 0; i < sizeof dirs / sizeof *dirs; i++) { char d[640]; snprintf(d, sizeof d, "%s/%s/", HOLLOW_ASSET_DIR, dirs[i]); SDL_EnumerateDirectory(d, scan_cb, b); }
    b->style = 0; b->clip_sel = -1; b->pal_sel = -1;
    snprintf(b->name, sizeof b->name, "%s", "my_hero");
}

void builder_open(Builder *b, const CharSpec *current, const char *name) {
    if (current && current->model[0]) b->spec = *current;
    else { memset(&b->spec, 0, sizeof b->spec); b->spec.scale = 1; b->spec.tex_size = 256; for (int a = 0; a < ANIM_COUNT; a++) b->spec.anims[a].contact = -1, b->spec.anims[a].rate = 1; if (b->nfiles) snprintf(b->spec.model, sizeof b->spec.model, "%s", b->files[0]); }
    b->file_sel = -1;
    for (int i = 0; i < b->nfiles; i++) if (!strcmp(b->files[i], b->spec.model)) b->file_sel = i;
    if (name && name[0]) snprintf(b->name, sizeof b->name, "%s", name);
    b->tab = 0; b->pal_sel = -1; b->clip_sel = -1; b->scroll = 0; b->name_focus = false; b->attach_sel = -1; b->part_file_sel = b->npart_files ? 0 : -1;
    b->borrow_file_sel = -1; b->nborrow_parts = 0;
    for (int i = 0; i < b->nfiles && b->spec.nborrow; i++) if (!strcmp(b->files[i], b->spec.borrow[0].file)) b->borrow_file_sel = i;   // show the donor already in use
    borrow_scan(b);
    say(b, "click parts to hide them, pick a colour to repaint it, then SAVE");
}

void builder_model_loaded(Builder *b, const Model *m) {
    b->nparts = m ? model_part_names(m, b->parts, BLD_MAX_PARTS) : 0;
    b->npal = m ? model_palette(m, b->pal, BLD_MAX_PAL) : 0;
    if (b->pal_sel >= b->npal) b->pal_sel = -1;
}

// ---------------------------------------------------------------- helpers

static bool is_hidden(const CharSpec *sp, const char *part) { for (int i = 0; i < sp->nhidden; i++) if (!strcmp(sp->hidden[i], part)) return true; return false; }
static void set_hidden(CharSpec *sp, const char *part, bool hide) {
    for (int i = 0; i < sp->nhidden; i++) if (!strcmp(sp->hidden[i], part)) { if (!hide) { for (int k = i; k < sp->nhidden - 1; k++) memcpy(sp->hidden[k], sp->hidden[k + 1], 48); sp->nhidden--; } return; }
    if (hide && sp->nhidden < SPEC_MAX_HIDDEN) snprintf(sp->hidden[sp->nhidden++], 48, "%s", part);
}
// Borrowed parts: mesh nodes taken from another rigged file. Its part names come out of the file
// itself, so nothing has to be on the GPU to list them.
static void borrow_scan(Builder *b) {
    b->nborrow_parts = 0;
    if (b->borrow_file_sel < 0 || b->borrow_file_sel >= b->nfiles) return;
    char path[640]; snprintf(path, sizeof path, "%s/%s", HOLLOW_ASSET_DIR, b->files[b->borrow_file_sel]);
    b->nborrow_parts = model_file_part_names(path, b->borrow_parts, BLD_MAX_PARTS);
}
static int borrow_index(const CharSpec *sp, const char *file, const char *node) {
    for (int i = 0; i < sp->nborrow; i++) if (!strcmp(sp->borrow[i].file, file) && !strcmp(sp->borrow[i].node, node)) return i;
    return -1;
}
static void borrow_remove(CharSpec *sp, int i) { for (int k = i; k < sp->nborrow - 1; k++) sp->borrow[k] = sp->borrow[k + 1]; sp->nborrow--; }

static int recolor_index(const CharSpec *sp, const unsigned char *from) { for (int i = 0; i < sp->nrecolor; i++) if (!memcmp(sp->rc_from[i], from, 3)) return i; return -1; }
static Vec3 mapped_color(const CharSpec *sp, const ModelColor *c) {
    int i = recolor_index(sp, c->rgb);
    const unsigned char *rgb = i >= 0 ? sp->rc_to[i] : c->rgb;
    return v3(rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f);
}

// Minimal text field for the character name: letters, digits, underscore, backspace.
static void name_keys(Builder *b, const Input *in) {
    if (!b->name_focus) return;
    size_t n = strlen(b->name);
    for (int sc = SDL_SCANCODE_A; sc <= SDL_SCANCODE_Z; sc++) if (in->key_down[sc] && n < sizeof b->name - 1) { b->name[n++] = (char)('a' + (sc - SDL_SCANCODE_A)); b->name[n] = 0; }
    for (int sc = SDL_SCANCODE_1; sc <= SDL_SCANCODE_0; sc++) if (in->key_down[sc] && n < sizeof b->name - 1) { b->name[n++] = sc == SDL_SCANCODE_0 ? '0' : (char)('1' + (sc - SDL_SCANCODE_1)); b->name[n] = 0; }
    if ((in->key_down[SDL_SCANCODE_MINUS] || in->key_down[SDL_SCANCODE_SPACE]) && n < sizeof b->name - 1) { b->name[n++] = '_'; b->name[n] = 0; }
    if (in->key_down[SDL_SCANCODE_BACKSPACE] && n > 0) b->name[--n] = 0;
    if (in->key_down[SDL_SCANCODE_RETURN] || in->key_down[SDL_SCANCODE_ESCAPE]) b->name_focus = false;
}

// ---------------------------------------------------------------- panel

#define M 12.0f
#define G 6.0f
#define ROW 30.0f

int builder_panel(Builder *b, Ui *ui, const Input *keys, float w, float h, const Model *m) {
    int flags = 0;
    if (b->msg_t > 0) b->msg_t -= 1.0f / 60.0f;
    name_keys(b, keys);
    const float head_h = M + 32 + ROW + 10 + ROW + 8, foot_h = 56;
    float view_h = h - head_h - foot_h;
    UiInput saved = ui->in;
    bool in_content = ui->in.my >= head_h && ui->in.my < h - foot_h;
    if (in_content && ui->in.wheel != 0) b->scroll = fmaxf(0, b->scroll - ui->in.wheel * 40);
    if (!in_content) { ui->in.mx = ui->in.my = -1e6f; ui->in.pressed = false; ui->in.wheel = 0; }
    float x = M, y = head_h - b->scroll, top = y;
    float cw = (w - 2 * M - G) / 2;
    bool two = w >= 640;
    CharSpec *sp = &b->spec;

    if (b->tab == 0) {
        // ---- BODY: base model, scale
        ui_label(ui, x, y, "BASE MODEL   rigged .glb files in assets/models/kaykit, characters, import", v4(1, 0.85f, 0.4f, 1)); y += 24;
        { int cols = w >= 900 ? 4 : w >= 640 ? 3 : 2; float bw = (w - 2 * M - (cols - 1) * G) / cols;
          for (int i = 0; i < b->nfiles; i++) { bool on = b->file_sel == i; float bx = x + (i % cols) * (bw + G), by = y + (i / cols) * (ROW + G);
              if (ui_toggle(ui, bx, by, bw, ROW, b->names[i], &on) && on) { b->file_sel = i; snprintf(sp->model, sizeof sp->model, "%s", b->files[i]); sp->nhidden = 0; sp->nrecolor = 0; sp->nborrow = 0; flags |= BLD_RELOAD; b->pal_sel = -1; if (b->borrow_file_sel == i) { b->borrow_file_sel = -1; b->nborrow_parts = 0; } } }
          y += (ROW + G) * ((b->nfiles + cols - 1) / cols) + 8; }
        if (ui_slider(ui, x, y, cw, "scale", &sp->scale, 0.3f, 3)) flags |= BLD_HIDE;
        { float yo = sp->yaw_offset_deg; if (ui_slider(ui, two ? x + cw + G : x, two ? y : y + 26, cw, "facing offset", &yo, -180, 180)) { sp->yaw_offset_deg = roundf(yo / 15) * 15; flags |= BLD_HIDE; } }
        y += two ? 32 : 58;
        // ---- PARTS
        ui_label(ui, x, y, "PARTS   click to hide or show (weapons, shields, hats, capes)", v4(1, 0.85f, 0.4f, 1)); y += 24;
        if (!m || b->nparts == 0) { ui_label(ui, x, y, "no model loaded", v4(0.6f, 0.58f, 0.55f, 1)); y += 24; }
        else { int cols = w >= 900 ? 4 : w >= 640 ? 3 : 2; float bw = (w - 2 * M - (cols - 1) * G) / cols;
            for (int i = 0; i < b->nparts; i++) { bool on = !is_hidden(sp, b->parts[i]); float bx = x + (i % cols) * (bw + G), by = y + (i / cols) * (ROW + G);
                if (ui_toggle(ui, bx, by, bw, ROW, b->parts[i], &on)) { set_hidden(sp, b->parts[i], !on); flags |= BLD_HIDE; } }
            y += (ROW + G) * ((b->nparts + cols - 1) / cols) + 8; }
        // ---- BORROW: a mesh part of another rigged file, drawn on this skeleton
        ui_label(ui, x, y, "BORROW PARTS   mix in a part from another rigged file (they share the skeleton)", v4(1, 0.85f, 0.4f, 1)); y += 24;
        ui_label_fit(ui, x, y, w - 2 * M, "hide the part it replaces above yourself: hide Knight_Helmet, then borrow Mage_Hat", v4(0.6f, 0.58f, 0.55f, 1)); y += 22;
        { int cols = w >= 900 ? 4 : w >= 640 ? 3 : 2; float bw = (w - 2 * M - (cols - 1) * G) / cols; int shown = 0;
          for (int i = 0; i < b->nfiles; i++) {
              if (!strcmp(b->files[i], sp->model)) continue;                    // the body itself is not a donor
              bool on = b->borrow_file_sel == i; float bx = x + (shown % cols) * (bw + G), by = y + (shown / cols) * (ROW + G);
              if (ui_toggle(ui, bx, by, bw, ROW, b->names[i], &on)) { b->borrow_file_sel = on ? i : -1; borrow_scan(b); }
              shown++;
          }
          y += (ROW + G) * ((shown + cols - 1) / cols) + 6; }
        if (b->borrow_file_sel >= 0) {
            const char *src = b->files[b->borrow_file_sel];
            if (b->nborrow_parts == 0) { ui_label(ui, x, y, "no mesh parts in that file", v4(0.6f, 0.58f, 0.55f, 1)); y += 24; }
            else { int cols = w >= 900 ? 4 : w >= 640 ? 3 : 2; float bw = (w - 2 * M - (cols - 1) * G) / cols;
                for (int i = 0; i < b->nborrow_parts; i++) {
                    bool on = borrow_index(sp, src, b->borrow_parts[i]) >= 0; float bx = x + (i % cols) * (bw + G), by = y + (i / cols) * (ROW + G);
                    if (!ui_toggle(ui, bx, by, bw, ROW, b->borrow_parts[i], &on)) continue;
                    int k = borrow_index(sp, src, b->borrow_parts[i]);
                    if (on && k < 0) {
                        if (sp->nborrow >= SPEC_MAX_BORROW) { say(b, "that is as many borrowed parts as a character can hold"); continue; }
                        k = sp->nborrow++; snprintf(sp->borrow[k].file, 128, "%s", src); snprintf(sp->borrow[k].node, 48, "%s", b->borrow_parts[i]);
                    } else if (!on && k >= 0) borrow_remove(sp, k);
                    flags |= BLD_RELOAD;
                }
                y += (ROW + G) * ((b->nborrow_parts + cols - 1) / cols) + 6; }
        }
        for (int i = 0; i < sp->nborrow; i++) {
            char s[200]; const char *fn = strrchr(sp->borrow[i].file, '/'); snprintf(s, sizeof s, "%s  from  %s", sp->borrow[i].node, fn ? fn + 1 : sp->borrow[i].file);
            ui_label_fit(ui, x, y + 7, cw, s, v4(0.85f, 0.85f, 0.8f, 1));
            if (ui_button(ui, x + cw + G, y, fminf(120, cw), ROW, "REMOVE")) { borrow_remove(sp, i); flags |= BLD_RELOAD; y += ROW + G; continue; }
            y += ROW + G;
        }
        y += 4;
        // ---- ATTACHMENTS: your own parts on a bone
        ui_label(ui, x, y, "ATTACH   your OBJ from assets/models/import on a bone (helmet on head, weapon on handslot.r)", v4(1, 0.85f, 0.4f, 1)); y += 24;
        if (b->npart_files == 0) { ui_label(ui, x, y, "no part files: export OBJ from your CAD tool into assets/models/import", v4(0.6f, 0.58f, 0.55f, 1)); y += 24; }
        else {
            int cols = w >= 900 ? 4 : w >= 640 ? 3 : 2; float bw = (w - 2 * M - (cols - 1) * G) / cols;
            for (int i = 0; i < b->npart_files; i++) { bool on = b->part_file_sel == i; if (ui_toggle(ui, x + (i % cols) * (bw + G), y + (i / cols) * (ROW + G), bw, ROW, b->part_names[i], &on) && on) b->part_file_sel = i; }
            y += (ROW + G) * ((b->npart_files + cols - 1) / cols) + 6;
            if (m && m->njoints > 0) {
                ui_label(ui, x, y, "bone", v4(0.7f, 0.68f, 0.65f, 1)); y += 22;
                int bc = w >= 900 ? 6 : w >= 640 ? 4 : 3; float bbw = (w - 2 * M - (bc - 1) * G) / bc; int shown = 0;
                for (int j = 0; j < m->njoints; j++) {
                    const char *nm = m->nodes[m->joints[j]].name;
                    if (strstr(nm, "IK") || strstr(nm, "control") || !strcmp(nm, "root")) continue;   // rig helpers are not places for parts
                    bool on = b->bone_sel == j; if (ui_toggle(ui, x + (shown % bc) * (bbw + G), y + (shown / bc) * (26 + 4), bbw, 26, nm, &on) && on) b->bone_sel = j;
                    shown++;
                }
                y += (26 + 4) * ((shown + bc - 1) / bc) + 6;
            }
            if (ui_button(ui, x, y, cw, ROW, "ADD PART TO BONE") && m && b->part_file_sel >= 0 && b->bone_sel >= 0 && b->bone_sel < m->njoints && sp->nattach < SPEC_MAX_ATTACH) {
                int i = sp->nattach++;
                snprintf(sp->attach[i].file, 128, "%s", b->part_files[b->part_file_sel]); snprintf(sp->attach[i].bone, 48, "%s", m->nodes[m->joints[b->bone_sel]].name);
                sp->attach[i].pos = v3(0, 0, 0); sp->attach[i].yaw = sp->attach[i].pitch = sp->attach[i].roll = 0; sp->attach[i].scale = 1;
                b->attach_sel = i; flags |= BLD_ATTACH;
            }
            y += ROW + 10;
        }
        for (int i = 0; i < sp->nattach; i++) {
            char s[200]; const char *fn = strrchr(sp->attach[i].file, '/'); snprintf(s, sizeof s, "%s  on  %s", fn ? fn + 1 : sp->attach[i].file, sp->attach[i].bone);
            bool on = b->attach_sel == i; if (ui_toggle(ui, x, y, cw, ROW, s, &on)) b->attach_sel = on ? i : -1;
            if (ui_button(ui, x + cw + G, y, fminf(120, cw), ROW, "REMOVE")) { for (int k = i; k < sp->nattach - 1; k++) sp->attach[k] = sp->attach[k + 1]; sp->nattach--; if (b->attach_sel == i) b->attach_sel = -1; flags |= BLD_ATTACH; y += ROW + G; continue; }
            y += ROW + G;
            if (b->attach_sel == i) {
                float c2x = two ? x + cw + G : x;
                if (ui_slider(ui, x, y, cw, "x", &sp->attach[i].pos.x, -0.5f, 0.5f)) flags |= BLD_ATTACH;
                if (ui_slider(ui, c2x, two ? y : y + 26, cw, "yaw", &sp->attach[i].yaw, -180, 180)) flags |= BLD_ATTACH; y += two ? 26 : 52;
                if (ui_slider(ui, x, y, cw, "y", &sp->attach[i].pos.y, -0.5f, 0.5f)) flags |= BLD_ATTACH;
                if (ui_slider(ui, c2x, two ? y : y + 26, cw, "pitch", &sp->attach[i].pitch, -180, 180)) flags |= BLD_ATTACH; y += two ? 26 : 52;
                if (ui_slider(ui, x, y, cw, "z", &sp->attach[i].pos.z, -0.5f, 0.5f)) flags |= BLD_ATTACH;
                if (ui_slider(ui, c2x, two ? y : y + 26, cw, "roll", &sp->attach[i].roll, -180, 180)) flags |= BLD_ATTACH; y += two ? 26 : 52;
                if (ui_slider(ui, x, y, cw, "size", &sp->attach[i].scale, 0.1f, 3)) flags |= BLD_ATTACH; y += 32;
            }
        }
    } else if (b->tab == 1) {
        // ---- COLOURS
        ui_label(ui, x, y, "PAINT   the model's colours, most used first. Pick one, then move the sliders.", v4(1, 0.85f, 0.4f, 1)); y += 24;
        if (b->npal == 0) { ui_label(ui, x, y, "no colours found (model has no texture)", v4(0.6f, 0.58f, 0.55f, 1)); y += 24; }
        { int cols = (int)((w - 2 * M + G) / (44 + G)); if (cols < 4) cols = 4; float sw = (w - 2 * M - (cols - 1) * G) / cols;
          for (int i = 0; i < b->npal; i++) {
              float bx = x + (i % cols) * (sw + G), by = y + (i / cols) * (44 + G);
              Vec3 c = mapped_color(sp, &b->pal[i]); bool changed = recolor_index(sp, b->pal[i].rgb) >= 0;
              bool inside = saved.mx >= bx && saved.mx < bx + sw && saved.my >= by && saved.my < by + 44 && in_content;
              gfx_ui_rect(ui->g, bx, by, sw, 44, b->pal_sel == i ? v4(1, 0.85f, 0.4f, 1) : inside ? v4(0.6f, 0.6f, 0.6f, 1) : v4(0.3f, 0.3f, 0.33f, 1));
              gfx_ui_rect(ui->g, bx + 3, by + 3, sw - 6, 38, v4(c.x, c.y, c.z, 1));
              if (changed) gfx_ui_rect(ui->g, bx + sw - 12, by + 4, 8, 8, v4(1, 1, 1, 1));
              if (inside && saved.pressed) { b->pal_sel = i; }
          }
          y += (44 + G) * ((b->npal + cols - 1) / cols) + 8; }
        if (b->pal_sel >= 0 && b->pal_sel < b->npal) {
            const ModelColor *pc = &b->pal[b->pal_sel];
            Vec3 c = mapped_color(sp, pc);
            char s[96]; snprintf(s, sizeof s, "SELECTED   original %d %d %d   used by %d texels", pc->rgb[0], pc->rgb[1], pc->rgb[2], pc->count);
            ui_label(ui, x, y, s, v4(0.85f, 0.85f, 0.8f, 1)); y += 24;
            if (ui_color(ui, x, y, two ? cw : w - 2 * M, "new", &c, 1)) {
                int i = recolor_index(sp, pc->rgb);
                if (i < 0 && sp->nrecolor < SPEC_MAX_RECOLOR) { i = sp->nrecolor++; memcpy(sp->rc_from[i], pc->rgb, 3); }
                if (i >= 0) { sp->rc_to[i][0] = (unsigned char)(c.x * 255 + 0.5f); sp->rc_to[i][1] = (unsigned char)(c.y * 255 + 0.5f); sp->rc_to[i][2] = (unsigned char)(c.z * 255 + 0.5f); flags |= BLD_RECOLOR; }
            }
            float bx = two ? x + cw + G : x, by = two ? y : y + 3 * 26 + 8;
            if (ui_button(ui, bx, by, two ? cw : (w - 2 * M - G) / 2, ROW, "RESET THIS COLOUR")) { int i = recolor_index(sp, pc->rgb); if (i >= 0) { for (int k = i; k < sp->nrecolor - 1; k++) { memcpy(sp->rc_from[k], sp->rc_from[k + 1], 3); memcpy(sp->rc_to[k], sp->rc_to[k + 1], 3); } sp->nrecolor--; flags |= BLD_RECOLOR; } }
            if (ui_button(ui, two ? bx : x + (w - 2 * M - G) / 2 + G, two ? by + ROW + G : by, two ? cw : (w - 2 * M - G) / 2, ROW, "RESET ALL COLOURS")) { sp->nrecolor = 0; flags |= BLD_RECOLOR; }
            y += two ? 3 * 26 + 10 : 3 * 26 + 8 + ROW + 10;
        } else if (b->npal) { ui_label(ui, x, y, "pick a swatch above", v4(0.6f, 0.58f, 0.55f, 1)); y += 24; }
    } else {
        // ---- ANIMATION
        ui_label(ui, x, y, "FIGHTING STYLE   picks the attack clips; AUTO BIND fills every action from the clip names", v4(1, 0.85f, 0.4f, 1)); y += 24;
        { const char *styles[] = { "one-handed", "two-handed", "spellcaster", "unarmed" }; float bw = (w - 2 * M - 3 * G) / 4;
          for (int i = 0; i < 4; i++) { bool on = b->style == i; if (ui_toggle(ui, x + i * (bw + G), y, bw, ROW, styles[i], &on) && on) b->style = i; }
          y += ROW + G; }
        if (ui_button(ui, x, y, cw, ROW, "AUTO BIND ACTIONS") && m) { int n = charmodel_spec_autobind(sp, m, b->style); char s[96]; snprintf(s, sizeof s, "bound %d actions from the clip names", n); say(b, s); flags |= BLD_RELOAD; }
        y += ROW + 10;
        ui_label(ui, x, y, "ACTIONS   what the game plays for each state", v4(1, 0.85f, 0.4f, 1)); y += 24;
        for (int a = 0; a < ANIM_COUNT; a++) {
            char s[140]; snprintf(s, sizeof s, "%-10s  %s%s%s", anim_name((Anim)a), sp->anims[a].set ? sp->anims[a].clip : "-", sp->anims[a].loop ? "  loop" : "", sp->anims[a].contact >= 0 ? "  (contact)" : "");
            ui_label_fit(ui, x, y, w - 2 * M, s, sp->anims[a].set ? v4(0.85f, 0.85f, 0.8f, 1) : v4(0.5f, 0.48f, 0.45f, 1)); y += 20;
        }
        y += 6;
        ui_label(ui, x, y, "CLIPS IN THIS MODEL   click to preview on the hero", v4(1, 0.85f, 0.4f, 1)); y += 24;
        if (m) { int cols = w >= 900 ? 3 : 2; float bw = (w - 2 * M - (cols - 1) * G) / cols;
            for (int i = 0; i < m->nclips; i++) { bool on = b->clip_sel == i; float bx = x + (i % cols) * (bw + G), by = y + (i / cols) * (26 + 4);
                if (ui_toggle(ui, bx, by, bw, 26, m->clips[i].name, &on) && on) { b->clip_sel = i; flags |= BLD_PLAY_CLIP; } }
            y += (26 + 4) * ((m->nclips + cols - 1) / cols) + 8; }
    }
    float content_h = y - top;
    float max_scroll = fmaxf(0, content_h - view_h); b->scroll = fminf(b->scroll, max_scroll);
    ui->in = saved;
    if (in_content) { ui->in.mx = ui->in.my = -1e6f; ui->in.pressed = false; }

    // ---- header: title, name field, tabs, save
    Vec4 bg = v4(0.05f, 0.05f, 0.07f, 1);
    gfx_ui_rect(ui->g, 0, 0, w, head_h - 4, bg);
    y = M;
    ui_header(ui, x, y, "CHARACTER BUILDER"); y += 32;
    {
        float nw = fminf(260, w * 0.4f);
        bool inside = saved.mx >= x && saved.mx < x + nw && saved.my >= y && saved.my < y + ROW;
        if (saved.pressed) b->name_focus = inside;
        gfx_ui_rect(ui->g, x, y, nw, ROW, b->name_focus ? v4(0.24f, 0.23f, 0.28f, 1) : v4(0.16f, 0.16f, 0.19f, 1));
        gfx_ui_rect(ui->g, x, y + ROW - 2, nw, 2, b->name_focus ? v4(1, 0.85f, 0.4f, 1) : v4(0.36f, 0.35f, 0.40f, 1));
        char s[64]; snprintf(s, sizeof s, "%s%s", b->name, b->name_focus ? "_" : "");
        gfx_ui_text(ui->g, x + 8, y + ROW * 0.5f - gfx_ui_line_h(1.1f) * 0.5f, 1.1f, v4(0.9f, 0.9f, 0.88f, 1), s);
        float bx = x + nw + G, bw = (w - M - bx - G) / 2;
        if (ui_button(ui, bx, y, bw, ROW, "SAVE ^S") || (keys->ctrl && keys->key_down[SDL_SCANCODE_S])) flags |= BLD_SAVE;
        if (ui_button(ui, bx + bw + G, y, bw, ROW, "SAVE + USE AS HERO")) flags |= BLD_SAVE | BLD_USE;
        y += ROW + 8;
    }
    { const char *tabs[] = { "BODY & PARTS", "COLOURS", "ANIMATION" }; float bw = (w - 2 * M - 2 * G) / 3;
      for (int i = 0; i < 3; i++) { char s[40]; snprintf(s, sizeof s, b->tab == i ? "[ %s ]" : "%s", tabs[i]); if (ui_button(ui, x + i * (bw + G), y, bw, ROW, s)) { b->tab = i; b->scroll = 0; } } }
    if (max_scroll > 0) {
        float bh = fmaxf(16, view_h * view_h / content_h), by = head_h + (view_h - bh) * (b->scroll / max_scroll);
        gfx_ui_rect(ui->g, w - 6, head_h, 4, view_h, v4(0.12f, 0.12f, 0.14f, 1));
        gfx_ui_rect(ui->g, w - 6, by, 4, bh, v4(0.45f, 0.43f, 0.40f, 1));
    }
    // ---- footer
    gfx_ui_rect(ui->g, 0, h - foot_h, w, foot_h, bg);
    if (b->msg_t > 0) ui_label_fit(ui, x, h - 50, w - 2 * M, b->msg, v4(1, 0.85f, 0.4f, 1));
    ui_label_fit(ui, x, h - 28, w - 2 * M, "saves to assets/characters/NAME.txt   the hero in the game window shows every change live", v4(0.5f, 0.48f, 0.45f, 1));
    ui->in = saved;
    return flags;
}
