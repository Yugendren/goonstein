#include "leveled.h"
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void say(LevelEd *e, const char *s) { snprintf(e->msg, sizeof e->msg, "%s", s); e->msg_t = 3.0f; }

// ---------------------------------------------------------------- kit

bool leveled_init(LevelEd *e, const char *kit_path) {
    memset(e, 0, sizeof *e);
    e->ghost_scale = 1; e->snap = false; e->cam_speed = 8; e->sel_prop = e->sel_light = e->sel_emitter = -1; e->tool = LT_PIECE;
    e->light_color = v3(1.0f, 0.8f, 0.5f); e->light_radius = 7; e->light_intensity = 3;
    size_t n; char *text = SDL_LoadFile(kit_path, &n);
    if (!text) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "kit missing: %s", kit_path); return false; }
    char *cur = text;
    while (*cur) {
        char *line = cur; char *nl = strchr(cur, '\n'); if (nl) { *nl = 0; cur = nl + 1; } else cur += strlen(cur);
        char *hash = strchr(line, '#'); if (hash) *hash = 0;
        char *tok[24]; int nt = 0; char *save = NULL;
        for (char *t = SDL_strtok_r(line, " \t\r", &save); t && nt < 24; t = SDL_strtok_r(NULL, " \t\r", &save)) tok[nt++] = t;
        if (nt < 9 || strcmp(tok[0], "piece") != 0) continue;
        if (e->nkit >= KIT_MAX) break;
        KitPiece *k = &e->kit[e->nkit++];
        memset(k, 0, sizeof *k);
        snprintf(k->category, sizeof k->category, "%s", tok[1]); snprintf(k->name, sizeof k->name, "%s", tok[2]); snprintf(k->file, sizeof k->file, "%s", tok[3]);
        k->scale = (float)atof(tok[4]); k->collide = (float)atof(tok[5]); k->glow = v3((float)atof(tok[6]), (float)atof(tok[7]), (float)atof(tok[8]));
        if (nt >= 16 && !strcmp(tok[9], "light")) { k->has_light = true; k->light_color = v3((float)atof(tok[10]), (float)atof(tok[11]), (float)atof(tok[12])); k->light_radius = (float)atof(tok[13]); k->light_intensity = (float)atof(tok[14]); k->light_flicker = (float)atof(tok[15]); }
        bool known = false; for (int i = 0; i < e->ncat; i++) if (!strcmp(e->categories[i], k->category)) known = true;
        if (!known && e->ncat < 10) snprintf(e->categories[e->ncat++], 16, "%s", k->category);
    }
    SDL_free(text);
    e->piece = e->nkit > 0 ? 0 : -1;
    if (e->nkit > 0) { e->ghost_scale = e->kit[0].scale; e->ghost_collide = e->kit[0].collide > 0; }
    return e->nkit > 0;
}

void leveled_shutdown(LevelEd *e) { for (int i = 0; i < e->undo_n; i++) free(e->undo[i]); e->undo_n = 0; }

void leveled_open(LevelEd *e, const Level *lv, const Camera *cam) {
    (void)lv;
    e->open = true;
    e->cam_pos = cam->eye;
    Vec3 f = v3_norm(v3_sub(cam->target, cam->eye));
    e->cam_yaw = atan2f(f.x, f.z); e->cam_pitch = asinf(clampf(f.y, -1, 1));
    e->sel_prop = e->sel_light = e->sel_emitter = -1; e->dragging = false;
    say(e, "editor: WASD fly, hold right mouse to look, left click places, R rotates, [ ] scale, X deletes, Ctrl+Z undo, Ctrl+S save, F6 leaves");
}

// ---------------------------------------------------------------- undo

static void push_undo(LevelEd *e, const Level *lv) {
    if (e->undo_n == ED_UNDO_LEVELS) { free(e->undo[0]); memmove(e->undo, e->undo + 1, (ED_UNDO_LEVELS - 1) * sizeof *e->undo); e->undo_n--; }
    Level *copy = malloc(sizeof *copy);
    if (!copy) return;
    *copy = *lv; e->undo[e->undo_n++] = copy;
    e->dirty = true;
}
static void pop_undo(LevelEd *e, Level *lv) {
    if (e->undo_n == 0) { say(e, "nothing to undo"); return; }
    Level *copy = e->undo[--e->undo_n];
    *lv = *copy; free(copy);
    e->sel_prop = e->sel_light = e->sel_emitter = -1;
    say(e, "undone");
}

// ---------------------------------------------------------------- helpers

static Vec3 fly_forward(const LevelEd *e) { return v3(sinf(e->cam_yaw) * cosf(e->cam_pitch), sinf(e->cam_pitch), cosf(e->cam_yaw) * cosf(e->cam_pitch)); }

static bool ground_hit(const Camera *cam, float mx, float my, Vec3 *out) {
    Mat4 inv = m4_inverse(camera_view_proj(cam, 1280.0f / 800.0f));
    float nx = mx / 1280.0f * 2 - 1, ny = 1 - my / 800.0f * 2;
    // clip -> world for near and far points
    float a[4] = { nx, ny, 0, 1 }, b[4] = { nx, ny, 1, 1 };
    float pa[4], pb[4];
    for (int r = 0; r < 4; r++) { pa[r] = inv.m[r] * a[0] + inv.m[4 + r] * a[1] + inv.m[8 + r] * a[2] + inv.m[12 + r] * a[3]; pb[r] = inv.m[r] * b[0] + inv.m[4 + r] * b[1] + inv.m[8 + r] * b[2] + inv.m[12 + r] * b[3]; }
    Vec3 p0 = v3(pa[0] / pa[3], pa[1] / pa[3], pa[2] / pa[3]), p1 = v3(pb[0] / pb[3], pb[1] / pb[3], pb[2] / pb[3]);
    Vec3 d = v3_sub(p1, p0);
    if (fabsf(d.y) < 1e-5f) return false;
    float t = -p0.y / d.y;
    if (t < 0 || t > 1) return false;
    *out = v3_add(p0, v3_scale(d, t));
    return true;
}

static void add_collider_for(Level *lv, const Prop *p) {
    if (p->collide <= 0 || lv->nblocks >= LEVEL_MAX_BLOCKS) return;
    Block *b = &lv->blocks[lv->nblocks++];
    memset(b, 0, sizeof *b);
    b->center = v3(p->pos.x, p->pos.y + 1.5f, p->pos.z); b->size = v3(p->collide * 2, 3.0f, p->collide * 2); b->tex = -1; b->solid = true; b->tint = v4(1, 1, 1, 0);
}
static void remove_collider_for(Level *lv, const Prop *p) {
    if (p->collide <= 0) return;
    for (int i = 0; i < lv->nblocks; i++) {
        Block *b = &lv->blocks[i];
        if (b->tex == -1 && fabsf(b->center.x - p->pos.x) < 1e-3f && fabsf(b->center.z - p->pos.z) < 1e-3f && fabsf(b->size.x - p->collide * 2) < 1e-3f) {
            for (int k = i; k < lv->nblocks - 1; k++) lv->blocks[k] = lv->blocks[k + 1];
            lv->nblocks--; return;
        }
    }
}

static int pick_prop(const Level *lv, Vec3 at) {
    int best = -1; float bd = 1.2f;
    for (int i = 0; i < lv->nprops; i++) {
        const Prop *p = &lv->props[i];
        float d = hypotf(p->pos.x - at.x, p->pos.z - at.z) / fmaxf(0.5f, p->scale * 0.8f);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
static int pick_light(const Level *lv, Vec3 at) {
    int best = -1; float bd = 1.5f;
    for (int i = 0; i < lv->nlights; i++) { float d = hypotf(lv->lights[i].pos.x - at.x, lv->lights[i].pos.z - at.z); if (d < bd) { bd = d; best = i; } }
    return best;
}
static int pick_emitter(const Level *lv, Vec3 at) {
    int best = -1; float bd = 2.0f;
    for (int i = 0; i < lv->nemitters; i++) { float d = hypotf(lv->emitters[i].pos.x - at.x, lv->emitters[i].pos.z - at.z); if (d < bd) { bd = d; best = i; } }
    return best;
}

static Vec3 snap3(const LevelEd *e, Vec3 p) { if (!e->snap) return p; return v3(roundf(p.x * 2) / 2, p.y, roundf(p.z * 2) / 2); }

static void place_piece(LevelEd *e, Level *lv, Vec3 at) {
    if (e->piece < 0 || lv->nprops >= LEVEL_MAX_PROPS) return;
    const KitPiece *k = &e->kit[e->piece];
    push_undo(e, lv);
    Prop *p = &lv->props[lv->nprops++];
    memset(p, 0, sizeof *p);
    snprintf(p->file, sizeof p->file, "%s", k->file);
    p->pos = at; p->yaw = e->ghost_yaw; p->scale = e->ghost_scale; p->tint = v4(1, 1, 1, 1); p->glow = k->glow; p->collide = e->ghost_collide ? (k->collide > 0 ? k->collide : 0.5f) : 0;
    add_collider_for(lv, p);
    if (k->has_light && lv->nlights < LEVEL_MAX_LIGHTS) {
        LevelLight *l = &lv->lights[lv->nlights++];
        memset(l, 0, sizeof *l);
        l->pos = v3(at.x, at.y + 1.6f * e->ghost_scale, at.z); l->color = k->light_color; l->radius = k->light_radius; l->intensity = k->light_intensity; l->flicker = k->light_flicker;
    }
    e->sel_prop = lv->nprops - 1;
    dbg_log("editor: placed %s at %.1f %.1f (yaw %.0f scale %.2f)", k->name, at.x, at.z, e->ghost_yaw / DEG2RAD, e->ghost_scale);
}

static void delete_selected(LevelEd *e, Level *lv) {
    if (e->sel_prop >= 0 && e->sel_prop < lv->nprops) {
        push_undo(e, lv);
        remove_collider_for(lv, &lv->props[e->sel_prop]);
        for (int k = e->sel_prop; k < lv->nprops - 1; k++) lv->props[k] = lv->props[k + 1];
        lv->nprops--; e->sel_prop = -1; say(e, "prop removed"); return;
    }
    if (e->sel_light >= 0 && e->sel_light < lv->nlights) {
        push_undo(e, lv);
        for (int k = e->sel_light; k < lv->nlights - 1; k++) lv->lights[k] = lv->lights[k + 1];
        lv->nlights--; e->sel_light = -1; say(e, "light removed"); return;
    }
    if (e->sel_emitter >= 0 && e->sel_emitter < lv->nemitters) {
        push_undo(e, lv);
        for (int k = e->sel_emitter; k < lv->nemitters - 1; k++) lv->emitters[k] = lv->emitters[k + 1];
        lv->nemitters--; e->sel_emitter = -1; say(e, "emitter removed"); return;
    }
}

// ---------------------------------------------------------------- tick (game window)

void leveled_tick(LevelEd *e, Level *lv, Camera *cam, const Input *in, float mx, float my, float dt, Gfx *g, PropCache *pc) {
    (void)g; (void)pc;
    if (e->msg_t > 0) e->msg_t -= dt;
    // Fly camera
    if (in->rmouse_held) { e->cam_yaw -= in->look_x * 0.0025f; e->cam_pitch = clampf(e->cam_pitch - in->look_y * 0.0025f, -1.4f, 1.4f); }
    Vec3 f = fly_forward(e), r = v3(-f.z, 0, f.x); r = v3_norm(r);
    float sp = e->cam_speed * (in->shift_held ? 3.0f : 1.0f) * dt;
    Vec3 mv = v3_add(v3_scale(f, -in->move_y * sp), v3_scale(r, in->move_x * sp));
    if (in->key_held[SDL_SCANCODE_E]) mv.y += sp; if (in->key_held[SDL_SCANCODE_Q]) mv.y -= sp;
    e->cam_pos = v3_add(e->cam_pos, mv);
    if (e->cam_pos.y < 0.5f) e->cam_pos.y = 0.5f;
    if (in->wheel != 0 && !in->ctrl) e->cam_speed = clampf(e->cam_speed * (in->wheel > 0 ? 1.2f : 0.85f), 1, 60);
    camera_set_scene(cam, e->cam_pos, v3_add(e->cam_pos, f), 50, true);

    // Ground point under the mouse
    Vec3 hit; e->ghost_valid = ground_hit(cam, mx, my, &hit);
    if (e->ghost_valid) e->ghost_pos = snap3(e, hit);

    // Keys
    bool over_tool_window = false; (void)over_tool_window;
    if (in->key_down[SDL_SCANCODE_R]) { float step = 15 * DEG2RAD; if (e->sel_prop >= 0 && e->tool == LT_SELECT) { push_undo(e, lv); lv->props[e->sel_prop].yaw += step; } else e->ghost_yaw += step; }
    if (in->key_down[SDL_SCANCODE_LEFTBRACKET])  { if (e->sel_prop >= 0 && e->tool == LT_SELECT) { push_undo(e, lv); lv->props[e->sel_prop].scale *= 0.9f; } else e->ghost_scale *= 0.9f; }
    if (in->key_down[SDL_SCANCODE_RIGHTBRACKET]) { if (e->sel_prop >= 0 && e->tool == LT_SELECT) { push_undo(e, lv); lv->props[e->sel_prop].scale *= 1.1f; } else e->ghost_scale *= 1.1f; }
    if (in->key_down[SDL_SCANCODE_X] || in->key_down[SDL_SCANCODE_DELETE] || in->key_down[SDL_SCANCODE_BACKSPACE]) delete_selected(e, lv);
    if (in->key_down[SDL_SCANCODE_G] && e->sel_prop >= 0 && lv->nprops < LEVEL_MAX_PROPS) {
        push_undo(e, lv); Prop copy = lv->props[e->sel_prop]; copy.pos.x += 1.5f; lv->props[lv->nprops++] = copy; add_collider_for(lv, &copy); e->sel_prop = lv->nprops - 1; say(e, "duplicated");
    }
    if (in->key_down[SDL_SCANCODE_ESCAPE]) { e->sel_prop = e->sel_light = e->sel_emitter = -1; e->tool = LT_SELECT; }
    if (in->key_down[SDL_SCANCODE_1]) e->tool = LT_SELECT; if (in->key_down[SDL_SCANCODE_2]) e->tool = LT_PIECE;
    if (in->key_down[SDL_SCANCODE_3]) e->tool = LT_LIGHT;  if (in->key_down[SDL_SCANCODE_4]) e->tool = LT_EMITTER;
    if (in->ctrl && in->key_down[SDL_SCANCODE_Z]) pop_undo(e, lv);
    if (in->ctrl && in->key_down[SDL_SCANCODE_S]) leveled_save(e, lv);
    if (in->key_down[SDL_SCANCODE_F] && e->sel_prop >= 0) { e->cam_pos = v3_add(lv->props[e->sel_prop].pos, v3(-f.x * 8, 5, -f.z * 8)); }

    // Mouse in the world
    if (!e->ghost_valid) { if (!in->mouse_held) e->dragging = false; return; }
    if (in->click && !in->rmouse_held) {
        switch (e->tool) {
        case LT_PIECE: place_piece(e, lv, e->ghost_pos); break;
        case LT_LIGHT: if (lv->nlights < LEVEL_MAX_LIGHTS) { push_undo(e, lv); LevelLight *l = &lv->lights[lv->nlights++]; memset(l, 0, sizeof *l); l->pos = v3(e->ghost_pos.x, 1.5f, e->ghost_pos.z); l->color = e->light_color; l->radius = e->light_radius; l->intensity = e->light_intensity; l->flicker = 0.3f; e->sel_light = lv->nlights - 1; } break;
        case LT_EMITTER: if (lv->nemitters < LEVEL_MAX_EMITTERS) { push_undo(e, lv); LevelEmitter *m = &lv->emitters[lv->nemitters++]; memset(m, 0, sizeof *m); snprintf(m->type, sizeof m->type, "firefly"); m->pos = v3(e->ghost_pos.x, 1.2f, e->ghost_pos.z); m->extent = v3(3, 1, 3); m->rate = 2; m->color = v3(2.5f, 3.0f, 1.2f); m->size = 0.1f; m->life = 8; e->sel_emitter = lv->nemitters - 1; } break;
        case LT_SELECT: {
            int p = pick_prop(lv, e->ghost_pos), l = pick_light(lv, e->ghost_pos), m = pick_emitter(lv, e->ghost_pos);
            e->sel_prop = p; e->sel_light = p < 0 ? l : -1; e->sel_emitter = (p < 0 && l < 0) ? m : -1;
            if (p >= 0) { push_undo(e, lv); e->dragging = true; e->drag_offset = v3_sub(lv->props[p].pos, e->ghost_pos); }
            else if (l >= 0) { push_undo(e, lv); e->dragging = true; e->drag_offset = v3_sub(lv->lights[l].pos, e->ghost_pos); }
            else if (m >= 0) { push_undo(e, lv); e->dragging = true; e->drag_offset = v3_sub(lv->emitters[m].pos, e->ghost_pos); }
        } break;
        }
    }
    if (e->dragging && in->mouse_held) {
        Vec3 np = v3_add(e->ghost_pos, e->drag_offset);
        if (e->sel_prop >= 0) { Prop *p = &lv->props[e->sel_prop]; remove_collider_for(lv, p); p->pos.x = np.x; p->pos.z = np.z; add_collider_for(lv, p); e->dirty = true; }
        else if (e->sel_light >= 0) { lv->lights[e->sel_light].pos.x = np.x; lv->lights[e->sel_light].pos.z = np.z; e->dirty = true; }
        else if (e->sel_emitter >= 0) { lv->emitters[e->sel_emitter].pos.x = np.x; lv->emitters[e->sel_emitter].pos.z = np.z; e->dirty = true; }
    }
    if (!in->mouse_held) e->dragging = false;
}

// ---------------------------------------------------------------- draw (game window)

void leveled_draw_world(LevelEd *e, const Level *lv, Gfx *g, PropCache *pc) {
    // ground cursor and a small grid
    if (e->ghost_valid) {
        Vec3 p = e->ghost_pos;
        gfx_draw_box_wire(g, v3(p.x, 0.02f, p.z), v3(0.5f, 0.02f, 0.5f), v4(1, 1, 0.6f, 1));
        for (int i = -4; i <= 4; i++) {
            gfx_draw_box(g, &g->white, v3(roundf(p.x) + i, 0.01f, roundf(p.z)), v3(0.02f, 0.01f, 9), 0, v4(1, 1, 1, 0.15f), 0);
            gfx_draw_box(g, &g->white, v3(roundf(p.x), 0.01f, roundf(p.z) + i), v3(9, 0.01f, 0.02f), 0, v4(1, 1, 1, 0.15f), 0);
        }
        if (e->tool == LT_PIECE && e->piece >= 0) {
            const KitPiece *k = &e->kit[e->piece];
            props_draw_one(g, pc, k->file, p, e->ghost_yaw, e->ghost_scale, v4(0.7f, 1.0f, 0.8f, 1), v3(0.15f, 0.3f, 0.2f));
        }
        if (e->tool == LT_LIGHT) gfx_draw_box_wire(g, v3(p.x, 1.5f, p.z), v3(0.4f, 0.4f, 0.4f), v4(e->light_color.x, e->light_color.y, e->light_color.z, 1));
        if (e->tool == LT_EMITTER) gfx_draw_box_wire(g, v3(p.x, 1.2f, p.z), v3(6, 2, 6), v4(0.6f, 1, 0.8f, 1));
    }
    // gizmos for lights and emitters
    for (int i = 0; i < lv->nlights; i++) { const LevelLight *l = &lv->lights[i]; gfx_draw_box_wire(g, l->pos, v3(0.3f, 0.3f, 0.3f), i == e->sel_light ? v4(1, 1, 1, 1) : v4(l->color.x, l->color.y, l->color.z, 1)); }
    for (int i = 0; i < lv->nemitters; i++) { const LevelEmitter *m = &lv->emitters[i]; gfx_draw_box_wire(g, m->pos, v3_scale(m->extent, 2), i == e->sel_emitter ? v4(1, 1, 1, 1) : v4(0.4f, 0.8f, 0.6f, 0.6f)); }
    // selection box
    if (e->sel_prop >= 0 && e->sel_prop < lv->nprops) {
        const Prop *p = &lv->props[e->sel_prop]; Vec3 bmin, bmax;
        if (props_bounds(g, pc, p->file, &bmin, &bmax)) {
            Vec3 size = v3_scale(v3_sub(bmax, bmin), p->scale), c = v3_add(p->pos, v3_scale(v3_add(bmin, bmax), 0.5f * p->scale));
            float m = fmaxf(size.x, size.z); size.x = size.z = m;   // yaw-independent box
            gfx_draw_box_wire(g, c, size, v4(1, 0.85f, 0.4f, 1));
        }
    }
}

// ---------------------------------------------------------------- panel (tool window)

void leveled_panel(LevelEd *e, Level *lv, Ui *ui, float w, float h) {
    (void)h;
    float x = 12, y = 10;
    ui_header(ui, x, y, e->dirty ? "ENVIRONMENT EDITOR  *unsaved" : "ENVIRONMENT EDITOR"); y += 22;
    if (ui_button(ui, x, y, 90, 26, "PLACE")) e->tab = 0;
    if (ui_button(ui, x + 96, y, 90, 26, "LOOK")) e->tab = 1;
    if (ui_button(ui, w - 200, y, 90, 26, "UNDO ^Z")) { pop_undo(e, lv); }
    if (ui_button(ui, w - 104, y, 92, 26, "SAVE ^S")) leveled_save(e, lv);
    y += 34;
    if (e->tab == 0) {
        ui_label(ui, x, y, "TOOL   1 select/move   2 piece   3 light   4 emitter", v4(0.6f, 0.58f, 0.55f, 1)); y += 14;
        const char *tools[] = { "SELECT", "PIECE", "LIGHT", "EMITTER" };
        for (int i = 0; i < 4; i++) { bool on = (int)e->tool == i; if (ui_toggle(ui, x + i * 110, y, 104, 26, tools[i], &on) && on) e->tool = (EdTool2)i; }
        y += 34;
        // categories
        for (int i = 0; i < e->ncat; i++) { bool on = e->cat == i; if (ui_toggle(ui, x + (i % 6) * 115, y + (i / 6) * 28, 110, 24, e->categories[i], &on) && on) { e->cat = i; e->list_sel = 0; } }
        y += 28 * ((e->ncat + 5) / 6) + 6;
        // list of pieces in the category
        static const char *names[KIT_MAX]; static int map[KIT_MAX]; int n = 0;
        for (int i = 0; i < e->nkit; i++) if (!strcmp(e->kit[i].category, e->categories[e->cat])) { names[n] = e->kit[i].name; map[n] = i; n++; }
        if (e->list_sel >= n) e->list_sel = n > 0 ? n - 1 : 0;
        if (ui_list(ui, 0, x, y, 300, 260, names, n, &e->list_sel) || (n > 0 && e->piece != map[e->list_sel])) {
            if (n > 0) { e->piece = map[e->list_sel]; e->ghost_scale = e->kit[e->piece].scale; e->ghost_collide = e->kit[e->piece].collide > 0; e->tool = LT_PIECE; }
        }
        // ghost settings
        float gx = x + 320, gy = y;
        ui_label(ui, gx, gy, "NEW PIECE", v4(1, 0.85f, 0.4f, 1)); gy += 16;
        ui_stepper(ui, gx, gy, "scale", &e->ghost_scale, 0.1f, 0.1f, 10); gy += 26;
        float yaw_deg = e->ghost_yaw / DEG2RAD; if (ui_stepper(ui, gx, gy, "yaw", &yaw_deg, 15, -360, 360)) e->ghost_yaw = yaw_deg * DEG2RAD; gy += 26;
        ui_toggle(ui, gx, gy, 150, 24, "collides", &e->ghost_collide); ui_toggle(ui, gx + 160, gy, 150, 24, "snap 0.5m", &e->snap); gy += 34;
        if (e->tool == LT_LIGHT) { ui_color(ui, gx, gy, 300, "light colour", &e->light_color, 1.5f); gy += 70; ui_slider(ui, gx, gy, 300, "radius", &e->light_radius, 1, 20); gy += 22; ui_slider(ui, gx, gy, 300, "intensity", &e->light_intensity, 0, 8); gy += 26; }
        y += 270;
        // selected item
        if (e->sel_prop >= 0 && e->sel_prop < lv->nprops) {
            Prop *p = &lv->props[e->sel_prop]; char s[200];
            snprintf(s, sizeof s, "SELECTED PROP  %s", strrchr(p->file, '/') ? strrchr(p->file, '/') + 1 : p->file); ui_label(ui, x, y, s, v4(1, 0.85f, 0.4f, 1)); y += 16;
            snprintf(s, sizeof s, "at %.1f %.1f %.1f   (drag in the world to move, F to fly to it)", p->pos.x, p->pos.y, p->pos.z); ui_label(ui, x, y, s, v4(0.85f, 0.85f, 0.8f, 1)); y += 16;
            if (ui_stepper(ui, x, y, "scale", &p->scale, 0.1f, 0.1f, 10)) e->dirty = true;
            float yd = p->yaw / DEG2RAD; if (ui_stepper(ui, x + 230, y, "yaw", &yd, 15, -360, 360)) { p->yaw = yd * DEG2RAD; e->dirty = true; }
            if (ui_stepper(ui, x + 460, y, "y", &p->pos.y, 0.25f, -5, 20)) e->dirty = true; y += 26;
            bool col = p->collide > 0; if (ui_toggle(ui, x, y, 130, 24, "collides", &col)) { remove_collider_for(lv, p); p->collide = col ? 0.6f : 0; add_collider_for(lv, p); e->dirty = true; }
            if (ui_color(ui, x + 150, y, 260, "glow", &p->glow, 1.5f)) e->dirty = true;
            if (ui_button(ui, x + 440, y, 110, 24, "DELETE X")) delete_selected(e, lv);
            if (ui_button(ui, x + 560, y, 130, 24, "DUPLICATE G")) { push_undo(e, lv); Prop copy = *p; copy.pos.x += 1.5f; if (lv->nprops < LEVEL_MAX_PROPS) { lv->props[lv->nprops++] = copy; add_collider_for(lv, &copy); e->sel_prop = lv->nprops - 1; } }
            y += 76;
        } else if (e->sel_light >= 0 && e->sel_light < lv->nlights) {
            LevelLight *l = &lv->lights[e->sel_light];
            ui_label(ui, x, y, "SELECTED LIGHT", v4(1, 0.85f, 0.4f, 1)); y += 16;
            if (ui_color(ui, x, y, 300, "colour", &l->color, 1.5f)) e->dirty = true;
            if (ui_slider(ui, x + 330, y, 300, "radius", &l->radius, 1, 25)) e->dirty = true;
            if (ui_slider(ui, x + 330, y + 22, 300, "intensity", &l->intensity, 0, 10)) e->dirty = true;
            if (ui_slider(ui, x + 330, y + 44, 300, "flicker", &l->flicker, 0, 1)) e->dirty = true;
            y += 70;
            if (ui_stepper(ui, x, y, "height", &l->pos.y, 0.25f, 0, 20)) e->dirty = true;
            if (ui_button(ui, x + 300, y, 110, 24, "DELETE X")) delete_selected(e, lv);
            y += 30;
        } else if (e->sel_emitter >= 0 && e->sel_emitter < lv->nemitters) {
            LevelEmitter *m = &lv->emitters[e->sel_emitter];
            static const char *types[] = { "firefly", "mist", "ember", "spore", "leaf", "smoke", "spark" };
            ui_label(ui, x, y, "SELECTED EMITTER", v4(1, 0.85f, 0.4f, 1)); y += 16;
            for (int i = 0; i < 7; i++) { bool on = !strcmp(m->type, types[i]); if (ui_toggle(ui, x + i * 98, y, 94, 22, types[i], &on) && on) { snprintf(m->type, sizeof m->type, "%s", types[i]); e->dirty = true; } }
            y += 28;
            if (ui_slider(ui, x, y, 300, "rate", &m->rate, 0, 10)) e->dirty = true;
            if (ui_slider(ui, x + 330, y, 300, "size", &m->size, 0.02f, 3)) e->dirty = true; y += 22;
            if (ui_slider(ui, x, y, 300, "life", &m->life, 0.5f, 15)) e->dirty = true;
            if (ui_slider(ui, x + 330, y, 300, "extent x", &m->extent.x, 0.2f, 30)) e->dirty = true; y += 22;
            if (ui_color(ui, x, y, 300, "colour", &m->color, 3)) e->dirty = true;
            if (ui_slider(ui, x + 330, y, 300, "extent z", &m->extent.z, 0.2f, 30)) e->dirty = true;
            if (ui_button(ui, x + 330, y + 26, 110, 24, "DELETE X")) delete_selected(e, lv);
            y += 74;
        }
    } else {
        Look *k = &lv->look; float cw = (w - 36) / 2;
        // sun as yaw/pitch for easy tuning
        static float sun_yaw = 0, sun_pitch = 0; static bool init = false;
        if (!init) { sun_yaw = atan2f(k->sun_dir.x, k->sun_dir.z) / DEG2RAD; sun_pitch = asinf(clampf(-k->sun_dir.y / fmaxf(v3_len(k->sun_dir), 1e-4f), -1, 1)) / DEG2RAD; init = true; }
        bool ch = false;
        ch |= ui_slider(ui, x, y, cw, "sun yaw", &sun_yaw, -180, 180);
        ch |= ui_slider(ui, x + cw + 12, y, cw, "sun pitch", &sun_pitch, 0, 89); y += 22;
        if (ch) { float p = sun_pitch * DEG2RAD, yw = sun_yaw * DEG2RAD; k->sun_dir = v3(sinf(yw) * cosf(p), -sinf(p), cosf(yw) * cosf(p)); e->dirty = true; }
        if (ui_slider(ui, x, y, cw, "sun power", &k->sun_intensity, 0, 3)) e->dirty = true;
        if (ui_slider(ui, x + cw + 12, y, cw, "fog density", &k->fog_density, 0, 0.12f)) e->dirty = true; y += 26;
        if (ui_color(ui, x, y, cw, "sun colour", &k->sun_color, 2)) e->dirty = true;
        if (ui_color(ui, x + cw + 12, y, cw, "fog colour", &k->fog_color, 1)) e->dirty = true; y += 74;
        if (ui_color(ui, x, y, cw, "sky ambient", &k->sky_ambient, 1)) e->dirty = true;
        if (ui_color(ui, x + cw + 12, y, cw, "ground ambient", &k->ground_ambient, 1)) e->dirty = true; y += 74;
        if (ui_color(ui, x, y, cw, "sky zenith", &k->sky_zenith, 1)) e->dirty = true;
        if (ui_color(ui, x + cw + 12, y, cw, "sky horizon", &k->sky_horizon, 1)) e->dirty = true; y += 74;
        if (ui_slider(ui, x, y, cw, "fog base y", &k->fog_base, -5, 10)) e->dirty = true;
        if (ui_slider(ui, x + cw + 12, y, cw, "fog falloff", &k->fog_falloff, 0, 1)) e->dirty = true; y += 22;
        if (ui_slider(ui, x, y, cw, "fog start", &k->fog_start, 0, 30)) e->dirty = true;
        if (ui_slider(ui, x + cw + 12, y, cw, "sun scatter", &k->fog_scatter, 0, 2)) e->dirty = true; y += 26;
        if (ui_slider(ui, x, y, cw, "exposure", &k->exposure, 0.2f, 3)) e->dirty = true;
        if (ui_slider(ui, x + cw + 12, y, cw, "saturation", &k->saturation, 0, 2)) e->dirty = true; y += 22;
        if (ui_slider(ui, x, y, cw, "contrast", &k->contrast, 0.5f, 1.8f)) e->dirty = true;
        if (ui_slider(ui, x + cw + 12, y, cw, "bloom", &k->bloom, 0, 1.5f)) e->dirty = true; y += 22;
        if (ui_slider(ui, x, y, cw, "bloom threshold", &k->bloom_threshold, 0.2f, 3)) e->dirty = true;
        if (ui_slider(ui, x + cw + 12, y, cw, "stars", &k->stars, 0, 3)) e->dirty = true; y += 22;
        if (ui_slider(ui, x, y, cw, "toon softness", &k->toon_softness, 0.01f, 0.4f)) e->dirty = true;
        if (ui_slider(ui, x + cw + 12, y, cw, "shadow floor", &k->shadow_floor, 0, 0.6f)) e->dirty = true; y += 22;
        if (ui_slider(ui, x, y, cw, "rim power", &k->rim_power, 1, 8)) e->dirty = true; y += 26;
    }
    if (e->msg_t > 0) ui_label(ui, x, h - 40, e->msg, v4(1, 0.85f, 0.4f, 1));
    ui_label(ui, x, h - 22, "game window: WASD+QE fly, right-drag look, wheel speed, click place/select, drag move, R rotate, [ ] scale, X delete, G dup, F fly to", v4(0.55f, 0.55f, 0.5f, 1));
}

bool leveled_save(LevelEd *e, const Level *lv) {
    bool ok = level_save(lv, lv->path);
    say(e, ok ? "saved level" : "save failed, see log");
    if (ok) e->dirty = false;
    dbg_log("editor: save %s -> %s", lv->path, ok ? "ok" : "FAILED");
    return ok;
}
