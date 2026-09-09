#include "leveled.h"
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void say(LevelEd *e, const char *s) { snprintf(e->msg, sizeof e->msg, "%s", s); e->msg_t = 3.0f; }
static const struct { const char *name; Vec3 c; } BIOME[] = { {"grass", {0.20f, 0.34f, 0.16f}}, {"forest floor", {0.11f, 0.17f, 0.10f}}, {"rock", {0.36f, 0.34f, 0.35f}}, {"snow", {0.88f, 0.90f, 0.95f}}, {"dirt", {0.30f, 0.22f, 0.15f}}, {"path", {0.55f, 0.50f, 0.44f}}, {"water", {0.10f, 0.22f, 0.32f}}, {"moss", {0.28f, 0.42f, 0.20f}}, {"sand", {0.62f, 0.56f, 0.40f}} };


// ---------------------------------------------------------------- kit

// ---------------------------------------------------------------- model folder scan

typedef struct ScanCtx { LevelEd *e; const char *category; } ScanCtx;
static bool kit_has_file(const LevelEd *e, const char *rel) { for (int i = 0; i < e->nkit; i++) if (!strcmp(e->kit[i].file, rel)) return true; return false; }
static void scan_models(LevelEd *e, const char *dir, const char *category);
static SDL_EnumerationResult scan_cb(void *ud, const char *dirname, const char *fname) {
    ScanCtx *c = ud; LevelEd *e = c->e;
    char full[1024]; snprintf(full, sizeof full, "%s%s", dirname, fname);
    SDL_PathInfo info; if (!SDL_GetPathInfo(full, &info)) return SDL_ENUM_CONTINUE;
    if (info.type == SDL_PATHTYPE_DIRECTORY) { if (strcmp(c->category, "models") == 0 && !strcmp(fname, "kaykit")) scan_models(e, full, "kaykit"); else scan_models(e, full, fname); return SDL_ENUM_CONTINUE; }
    size_t n = strlen(fname);
    bool is_model = (n > 4 && !strcmp(fname + n - 4, ".glb")) || (n > 5 && !strcmp(fname + n - 5, ".gltf")) || (n > 4 && !strcmp(fname + n - 4, ".obj")) || (n > 5 && !strcmp(fname + n - 5, ".part"));
    if (!is_model) return SDL_ENUM_CONTINUE;
    if (!strcmp(c->category, "kaykit")) return SDL_ENUM_CONTINUE;   // rigged characters live here
    const char *rel = strstr(full, "/models/"); if (!rel) return SDL_ENUM_CONTINUE; rel += 1;
    if (kit_has_file(e, rel) || e->nkit >= KIT_MAX) return SDL_ENUM_CONTINUE;
    KitPiece *k = &e->kit[e->nkit++]; memset(k, 0, sizeof *k);
    snprintf(k->category, sizeof k->category, "%s", c->category);
    char nm[64]; snprintf(nm, sizeof nm, "%s", fname); char *dot = strchr(nm, '.'); if (dot) *dot = 0;
    snprintf(k->name, sizeof k->name, "%s", nm);
    snprintf(k->file, sizeof k->file, "%s", rel);
    k->scale = 1; k->collide = 0;
    bool known = false; for (int i = 0; i < e->ncat; i++) if (!strcmp(e->categories[i], k->category)) known = true;
    if (!known) { if (e->ncat < LEVELED_MAX_CATS) snprintf(e->categories[e->ncat++], 16, "%s", k->category); else snprintf(k->category, sizeof k->category, "%s", e->categories[e->ncat - 1]); }
    return SDL_ENUM_CONTINUE;
}
static void scan_models(LevelEd *e, const char *dir, const char *category) {
    char d[1024]; snprintf(d, sizeof d, "%s/", dir);
    ScanCtx c = { e, category };
    SDL_EnumerateDirectory(d, scan_cb, &c);
}

static SDL_EnumerationResult hm_scan_cb(void *ud, const char *dirname, const char *fname);
bool leveled_init(LevelEd *e, const char *kit_path) {
    memset(e, 0, sizeof *e);
    e->ghost_scale = 1; e->snap = false; e->cam_speed = 8; e->sel_prop = e->sel_light = e->sel_emitter = -1; e->tool = LT_PIECE;
    e->light_color = v3(1.0f, 0.8f, 0.5f); e->light_radius = 7; e->light_intensity = 3;
    e->tradius = 6; e->tstrength = 6; e->tpaint = v3(0.22f, 0.36f, 0.18f); e->snow_h = 14; e->rock_slope = 0.45f; e->scatter_density = 0.6f;
    e->seed = 7; e->g_mountains = 0.6f; e->g_hills = 0.5f; e->g_rough = 0.4f; e->g_forest = 0.5f; e->g_rocks = 0.3f; e->g_water = -1000; e->g_snow = 22; e->hm_range = 40;
    { char d[640]; snprintf(d, sizeof d, "%s/heightmaps/", HOLLOW_ASSET_DIR); SDL_EnumerateDirectory(d, hm_scan_cb, e); }
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
        if (!known && e->ncat < LEVELED_MAX_CATS) snprintf(e->categories[e->ncat++], 16, "%s", k->category);
    }
    SDL_free(text);
    // Every model file under assets/models that kit.txt does not mention becomes a piece too, in a
    // category named after its folder, so adding an asset is a file copy. The kaykit root holds
    // rigged characters (character builder), so it is skipped.
    { char root[640]; snprintf(root, sizeof root, "%s/models", HOLLOW_ASSET_DIR); int before = e->nkit; scan_models(e, root, "models"); if (e->nkit > before) SDL_Log("kit: %d model files added from assets/models", e->nkit - before); }
    e->piece = e->nkit > 0 ? 0 : -1;
    if (e->nkit > 0) { e->ghost_scale = e->kit[0].scale; e->ghost_collide = e->kit[0].collide > 0; }
    return e->nkit > 0;
}

typedef struct TerrainSnap { float height[TERRAIN_N * TERRAIN_N]; Vec3 color[TERRAIN_N * TERRAIN_N]; } TerrainSnap;
void leveled_shutdown(LevelEd *e) { for (int i = 0; i < e->undo_n; i++) { free(e->undo[i]); free(e->tundo[i]); } e->undo_n = 0; }

void leveled_open(LevelEd *e, const Level *lv, const Camera *cam) {
    { const char *t = SDL_getenv("HOLLOW_LEVELED_TAB"); if (t) e->tab = atoi(t); }   // headless captures of a given tab
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
    if (e->undo_n == ED_UNDO_LEVELS) { free(e->undo[0]); free(e->tundo[0]); memmove(e->undo, e->undo + 1, (ED_UNDO_LEVELS - 1) * sizeof *e->undo); memmove(e->tundo, e->tundo + 1, (ED_UNDO_LEVELS - 1) * sizeof *e->tundo); e->undo_n--; }
    Level *copy = malloc(sizeof *copy);
    if (!copy) return;
    *copy = *lv; e->undo[e->undo_n] = copy;
    TerrainSnap *ts = NULL;
    if (e->tr && e->tr->present && (ts = malloc(sizeof *ts))) { memcpy(ts->height, e->tr->height, sizeof ts->height); memcpy(ts->color, e->tr->color, sizeof ts->color); }
    e->tundo[e->undo_n++] = ts;
    e->dirty = true;
}
static void pop_undo(LevelEd *e, Level *lv) {
    if (e->undo_n == 0) { say(e, "nothing to undo"); return; }
    Level *copy = e->undo[--e->undo_n];
    TerrainSnap *ts = e->tundo[e->undo_n]; e->tundo[e->undo_n] = NULL;
    if (ts && e->tr && e->tr->present) { memcpy(e->tr->height, ts->height, sizeof ts->height); memcpy(e->tr->color, ts->color, sizeof ts->color); e->tr->mesh_dirty = true; }
    free(ts);
    *lv = *copy; free(copy);
    e->sel_prop = e->sel_light = e->sel_emitter = -1;
    say(e, "undone");
}

// ---------------------------------------------------------------- helpers

static Vec3 fly_forward(const LevelEd *e) { return v3(sinf(e->cam_yaw) * cosf(e->cam_pitch), sinf(e->cam_pitch), cosf(e->cam_yaw) * cosf(e->cam_pitch)); }

bool leveled_ground_hit(const Camera *cam, const Terrain *tr, float mx, float my, Vec3 *out) {
    Mat4 inv = m4_inverse(camera_view_proj(cam, 1280.0f / 800.0f));
    float nx = mx / 1280.0f * 2 - 1, ny = 1 - my / 800.0f * 2;
    // clip -> world for near and far points
    float a[4] = { nx, ny, 0, 1 }, b[4] = { nx, ny, 1, 1 };
    float pa[4], pb[4];
    for (int r = 0; r < 4; r++) { pa[r] = inv.m[r] * a[0] + inv.m[4 + r] * a[1] + inv.m[8 + r] * a[2] + inv.m[12 + r] * a[3]; pb[r] = inv.m[r] * b[0] + inv.m[4 + r] * b[1] + inv.m[8 + r] * b[2] + inv.m[12 + r] * b[3]; }
    Vec3 p0 = v3(pa[0] / pa[3], pa[1] / pa[3], pa[2] / pa[3]), p1 = v3(pb[0] / pb[3], pb[1] / pb[3], pb[2] / pb[3]);
    Vec3 d = v3_sub(p1, p0);
    if (tr && tr->present) return terrain_ray(tr, p0, p1, out);
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
    p->pos = at; p->yaw = e->ghost_yaw; p->scale = e->ghost_scale; p->stretch = v3(1, 1, 1); p->tint = v4(1, 1, 1, 1); p->glow = k->glow; p->collide = e->ghost_collide ? (k->collide > 0 ? k->collide : 0.5f) : 0;
    if (e->tr && e->tr->present && p->collide > 0 && strcmp(k->category, "trees") != 0) terrain_flatten_pad(e->tr, at, fmaxf(p->collide * e->ghost_scale, 0.8f) + 0.4f, at.y);   // structures get level ground
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

static void part_name_keys(LevelEd *e, const Input *in);
void leveled_tick(LevelEd *e, Level *lv, Terrain *tr, Camera *cam, const Input *in, float mx, float my, float dt, Gfx *g, PropCache *pc) {
    (void)g; (void)pc; e->tr = tr;
    part_name_keys(e, in);
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
    Vec3 hit; e->ghost_valid = leveled_ground_hit(cam, tr, mx, my, &hit);
    if (e->ghost_valid) e->ghost_pos = snap3(e, hit);

    // Keys
    bool over_tool_window = false; (void)over_tool_window;
    if (in->key_down[SDL_SCANCODE_R] && !in->ctrl) { float step = 15 * DEG2RAD; if (e->sel_prop >= 0 && e->tool == LT_SELECT) { push_undo(e, lv); lv->props[e->sel_prop].yaw += step; } else e->ghost_yaw += step; }
    if (in->key_down[SDL_SCANCODE_LEFTBRACKET])  { if (e->sel_prop >= 0 && e->tool == LT_SELECT) { push_undo(e, lv); lv->props[e->sel_prop].scale *= 0.9f; } else e->ghost_scale *= 0.9f; }
    if (in->key_down[SDL_SCANCODE_RIGHTBRACKET]) { if (e->sel_prop >= 0 && e->tool == LT_SELECT) { push_undo(e, lv); lv->props[e->sel_prop].scale *= 1.1f; } else e->ghost_scale *= 1.1f; }
    if (in->key_down[SDL_SCANCODE_X] || in->key_down[SDL_SCANCODE_DELETE] || in->key_down[SDL_SCANCODE_BACKSPACE]) delete_selected(e, lv);
    if (in->key_down[SDL_SCANCODE_G] && !in->ctrl && e->sel_prop >= 0 && lv->nprops < LEVEL_MAX_PROPS) {
        push_undo(e, lv); Prop copy = lv->props[e->sel_prop]; copy.pos.x += 1.5f; lv->props[lv->nprops++] = copy; add_collider_for(lv, &copy); e->sel_prop = lv->nprops - 1; say(e, "duplicated");
    }
    if (in->key_down[SDL_SCANCODE_ESCAPE]) { e->sel_prop = e->sel_light = e->sel_emitter = -1; e->tool = LT_SELECT; }
    if (in->key_down[SDL_SCANCODE_1]) e->tool = LT_SELECT; if (in->key_down[SDL_SCANCODE_2]) e->tool = LT_PIECE;
    if (in->key_down[SDL_SCANCODE_3]) e->tool = LT_LIGHT;  if (in->key_down[SDL_SCANCODE_4]) e->tool = LT_EMITTER;
    if (in->ctrl && in->key_down[SDL_SCANCODE_Z]) pop_undo(e, lv);
    if (in->ctrl && in->key_down[SDL_SCANCODE_S]) leveled_save(e, lv, tr);
    if (in->key_down[SDL_SCANCODE_F] && !in->ctrl && e->sel_prop >= 0) { e->cam_pos = v3_add(lv->props[e->sel_prop].pos, v3(-f.x * 8, 5, -f.z * 8)); }

    // Terrain tab: brushes act while the left button is held
    if (e->tab == 2 && tr && tr->present) {
        if (e->ghost_valid && in->mouse_held && !in->rmouse_held) {
            if (!e->sculpting) { push_undo(e, lv); e->sculpting = true; }
            Vec3 at = e->ghost_pos;
            switch (e->tbrush) {
            case TB_RAISE: terrain_brush(tr, in->shift_held ? TB_LOWER : TB_RAISE, at, e->tradius, e->tstrength, dt, e->tpaint, 0); break;
            case TB_LOWER: terrain_brush(tr, TB_LOWER, at, e->tradius, e->tstrength, dt, e->tpaint, 0); break;
            case TB_SMOOTH: terrain_brush(tr, TB_SMOOTH, at, e->tradius, e->tstrength, dt, e->tpaint, 0); break;
            case TB_FLATTEN: terrain_brush(tr, TB_FLATTEN, at, e->tradius, e->tstrength, dt, e->tpaint, at.y); break;
            case TB_PAINT: terrain_brush(tr, TB_PAINT, at, e->tradius, e->tstrength, dt, e->tpaint, 0); break;
            case 5: {   // scatter pieces of the chosen category
                e->scatter_accum += e->scatter_density * dt * 3.0f;
                while (e->scatter_accum >= 1.0f && lv->nprops < LEVEL_MAX_PROPS) {
                    e->scatter_accum -= 1.0f;
                    int cands[KIT_MAX]; int nc = 0;
                    for (int i = 0; i < e->nkit; i++) if (!strcmp(e->kit[i].category, e->categories[e->scatter_cat])) cands[nc++] = i;
                    if (nc == 0) break;
                    const KitPiece *k = &e->kit[cands[rand() % nc]];
                    float ang = (float)(rand() % 360) * DEG2RAD, rr = e->tradius * sqrtf((float)(rand() % 1000) / 1000.0f);
                    Vec3 p = v3(at.x + cosf(ang) * rr, 0, at.z + sinf(ang) * rr);
                    if (!terrain_inside(tr, p.x, p.z)) continue;
                    p.y = terrain_height(tr, p.x, p.z);
                    bool crowded = false; for (int i = 0; i < lv->nprops; i++) if (hypotf(lv->props[i].pos.x - p.x, lv->props[i].pos.z - p.z) < 1.2f) crowded = true;
                    if (crowded) continue;
                    Prop *pr = &lv->props[lv->nprops++]; memset(pr, 0, sizeof *pr);
                    snprintf(pr->file, sizeof pr->file, "%s", k->file);
                    pr->pos = p; pr->yaw = (float)(rand() % 360) * DEG2RAD; pr->scale = k->scale * (0.8f + 0.4f * (float)(rand() % 100) / 100.0f); pr->tint = v4(1, 1, 1, 1); pr->glow = k->glow; pr->collide = k->collide;
                    add_collider_for(lv, pr);
                    e->dirty = true;
                }
            } break;
            case 7: {   // path: level and paint along the drag
                if (!e->path_started) { e->path_last = at; e->path_started = true; }
                if (hypotf(at.x - e->path_last.x, at.z - e->path_last.z) > 0.4f) { terrain_path(tr, e->path_last, at, fmaxf(e->tradius * 0.4f, 1.0f), BIOME[5].c); e->path_last = at; e->dirty = true; }
            } break;
            case 6: {   // clear props inside the brush
                for (int i = lv->nprops - 1; i >= 0; i--) if (hypotf(lv->props[i].pos.x - at.x, lv->props[i].pos.z - at.z) < e->tradius) {
                    remove_collider_for(lv, &lv->props[i]);
                    for (int k = i; k < lv->nprops - 1; k++) lv->props[k] = lv->props[k + 1];
                    lv->nprops--; e->dirty = true;
                }
            } break;
            }
            if (e->tbrush <= TB_FLATTEN) {   // keep props on the surface after sculpting
                for (int i = 0; i < lv->nprops; i++) if (hypotf(lv->props[i].pos.x - at.x, lv->props[i].pos.z - at.z) < e->tradius + 2) { Prop *pr = &lv->props[i]; remove_collider_for(lv, pr); pr->pos.y = terrain_height(tr, pr->pos.x, pr->pos.z); add_collider_for(lv, pr); }
                for (int i = 0; i < lv->nlights; i++) if (hypotf(lv->lights[i].pos.x - at.x, lv->lights[i].pos.z - at.z) < e->tradius + 2) lv->lights[i].pos.y = fmaxf(lv->lights[i].pos.y, terrain_height(tr, lv->lights[i].pos.x, lv->lights[i].pos.z) + 1.0f);
                e->dirty = true;
            }
        } else { e->sculpting = false; e->path_started = false; }
        if (in->ctrl && in->wheel != 0) e->tradius = clampf(e->tradius * (in->wheel > 0 ? 1.15f : 0.87f), 1, 40);
        return;
    }

    // Mouse in the world
    if (!e->ghost_valid) { if (!in->mouse_held) e->dragging = false; return; }
    if (in->click && !in->rmouse_held) {
        switch (e->tool) {
        case LT_PIECE: place_piece(e, lv, e->ghost_pos); break;
        case LT_LIGHT: if (lv->nlights < LEVEL_MAX_LIGHTS) { push_undo(e, lv); LevelLight *l = &lv->lights[lv->nlights++]; memset(l, 0, sizeof *l); l->pos = v3(e->ghost_pos.x, 1.5f, e->ghost_pos.z); l->color = e->light_color; l->radius = e->light_radius; l->intensity = e->light_intensity; l->flicker = 0.3f; e->sel_light = lv->nlights - 1; } break;
        case LT_EMITTER: if (lv->nemitters < LEVEL_MAX_EMITTERS) { push_undo(e, lv); LevelEmitter *m = &lv->emitters[lv->nemitters++]; memset(m, 0, sizeof *m); snprintf(m->type, sizeof m->type, "firefly"); m->pos = v3(e->ghost_pos.x, 1.2f, e->ghost_pos.z); m->extent = v3(3, 1, 3); m->rate = 2; m->color = v3(2.5f, 3.0f, 1.2f); m->size = 0.1f; m->life = 8; e->sel_emitter = lv->nemitters - 1; } break;
        case LT_SELECT: {
            int p = pick_prop(lv, e->ghost_pos), l = pick_light(lv, e->ghost_pos), m = pick_emitter(lv, e->ghost_pos);
            if (in->shift_held && p >= 0) {   // group selection
                bool found = false; for (int i = 0; i < e->nmulti; i++) if (e->multi[i] == p) { for (int k = i; k < e->nmulti - 1; k++) e->multi[k] = e->multi[k + 1]; e->nmulti--; found = true; break; }
                if (!found && e->nmulti < 64) e->multi[e->nmulti++] = p;
                if (e->sel_prop >= 0 && e->sel_prop != p) { bool has = false; for (int i = 0; i < e->nmulti; i++) if (e->multi[i] == e->sel_prop) has = true; if (!has && e->nmulti < 64) e->multi[e->nmulti++] = e->sel_prop; }
                e->sel_prop = p; e->sel_light = e->sel_emitter = -1; break;
            }
            e->nmulti = 0;
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
    if (e->tab == 2 && e->ghost_valid) {
        // brush ring: 32 short bars around the cursor, lifted to the surface height
        Vec3 p = e->ghost_pos; int n = 40;
        Vec4 col = e->tbrush == TB_PAINT ? v4(e->tpaint.x * 2, e->tpaint.y * 2, e->tpaint.z * 2, 1) : e->tbrush == 5 ? v4(0.5f, 1, 0.6f, 1) : e->tbrush == 6 ? v4(1, 0.4f, 0.3f, 1) : v4(1, 0.9f, 0.5f, 1);
        Material m = material_default(); m.emissive = v3(col.x, col.y, col.z); gfx_set_material(g, &m);
        for (int i = 0; i < n; i++) {
            float a = (float)i / n * 2 * PI; Vec3 q = v3(p.x + cosf(a) * e->tradius, p.y + 0.15f, p.z + sinf(a) * e->tradius);
            gfx_draw_box(g, &g->white, q, v3(0.25f, 0.08f, 0.25f), 0, col, 0);
        }
        gfx_set_material(g, NULL);
        return;
    }
    for (int i = 0; i < e->nmulti; i++) if (e->multi[i] >= 0 && e->multi[i] < lv->nprops) { const Prop *p = &lv->props[e->multi[i]]; gfx_draw_box_wire(g, v3(p->pos.x, p->pos.y + 0.6f, p->pos.z), v3(0.9f, 1.2f, 0.9f), v4(0.5f, 0.9f, 1, 1)); }
    // ground cursor and a small grid
    if (e->ghost_valid) {
        Vec3 p = e->ghost_pos;
        gfx_draw_box_wire(g, v3(p.x, p.y + 0.02f, p.z), v3(0.5f, 0.02f, 0.5f), v4(1, 1, 0.6f, 1));
        for (int i = -4; i <= 4; i++) {
            gfx_draw_box(g, &g->white, v3(roundf(p.x) + i, p.y + 0.01f, roundf(p.z)), v3(0.02f, 0.01f, 9), 0, v4(1, 1, 1, 0.15f), 0);
            gfx_draw_box(g, &g->white, v3(roundf(p.x), p.y + 0.01f, roundf(p.z) + i), v3(9, 0.01f, 0.02f), 0, v4(1, 1, 1, 0.15f), 0);
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
    for (int i = 0; i < lv->nemitters; i++) { const LevelEmitter *m = &lv->emitters[i]; if (i == e->sel_emitter) gfx_draw_box_wire(g, m->pos, v3_scale(m->extent, 2), v4(1, 1, 1, 1)); else gfx_draw_box_wire(g, m->pos, v3(0.4f, 0.4f, 0.4f), v4(0.4f, 0.8f, 0.6f, 1)); }
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

// ---------------------------------------------------------------- world generation

static SDL_EnumerationResult hm_scan_cb(void *ud, const char *dirname, const char *fname) {
    LevelEd *e = ud; size_t n = strlen(fname);
    if (!(n > 4 && !strcmp(fname + n - 4, ".png")) || e->nhm >= 16) return SDL_ENUM_CONTINUE;
    snprintf(e->hm_files[e->nhm], 160, "%s%s", dirname, fname);
    char nm[64]; snprintf(nm, sizeof nm, "%s", fname); char *dot = strrchr(nm, '.'); if (dot) *dot = 0; snprintf(e->hm_names[e->nhm], 48, "%s", nm);
    e->nhm++;
    return SDL_ENUM_CONTINUE;
}


static void ensure_terrain(LevelEd *e, Level *lv, Terrain *tr) {
    if (tr->present) return;
    terrain_init(tr, 1.5f, v3(-96, 0, -96), 0, BIOME[0].c);
    snprintf(tr->file, sizeof tr->file, "%s", "levels/terrain_new");
    if (lv->path[0]) { const char *slash = strrchr(lv->path, '/'); const char *base = slash ? slash + 1 : lv->path; char nm[96]; snprintf(nm, sizeof nm, "%s", base); char *dot = strrchr(nm, '.'); if (dot) *dot = 0; snprintf(tr->file, sizeof tr->file, "levels/%s_terrain", nm); }
    e->dirty = true;
}

static void remove_scattered(Level *lv, LevelEd *e) {   // trees and rocks from a previous generation go; everything else stays
    for (int i = lv->nprops - 1; i >= 0; i--) {
        const char *cat = NULL; for (int k = 0; k < e->nkit; k++) if (!strcmp(e->kit[k].file, lv->props[i].file)) { cat = e->kit[k].category; break; }
        if (cat && (!strcmp(cat, "trees") || !strcmp(cat, "rocks"))) { remove_collider_for(lv, &lv->props[i]); for (int k = i; k < lv->nprops - 1; k++) lv->props[k] = lv->props[k + 1]; lv->nprops--; }
    }
}

// Seed in, world out: heights, biome colours, a flat spawn and arena pad, a path between them, water,
// and trees and rocks placed by the biome rules. Sculpt and paint over it afterwards.
void leveled_generate_world(LevelEd *e, Level *lv, Terrain *tr) {
    ensure_terrain(e, lv, tr);
    push_undo(e, lv);
    TerrainGen p = { .seed = e->seed, .mountains = e->g_mountains, .hills = e->g_hills, .roughness = e->g_rough, .snow_h = e->g_snow, .water_h = e->g_water };
    p.flat[0] = lv->spawn; p.flat_r[0] = 7; p.flat[1] = lv->boss_spawn; p.flat_r[1] = 11; p.nflat = 2;
    terrain_generate(tr, &p, BIOME[0].c, BIOME[2].c, BIOME[3].c, BIOME[4].c, BIOME[8].c);
    // path from the spawn to the arena, wandering a little
    { Vec3 a = lv->spawn, b = lv->boss_spawn; float len = hypotf(b.x - a.x, b.z - a.z); int steps = (int)(len / 4) + 1; Vec3 prev = a;
      for (int i = 1; i <= steps; i++) { float k = (float)i / steps; Vec3 q = v3(lerpf(a.x, b.x, k), 0, lerpf(a.z, b.z, k));
          float wob = (terrain_noise(e->seed + 5, q.x, q.z, 12) - 0.5f) * 10 * sinf(k * PI); Vec3 side = v3(-(b.z - a.z) / fmaxf(len, 1), 0, (b.x - a.x) / fmaxf(len, 1));
          q = v3_add(q, v3_scale(side, wob)); terrain_path(tr, prev, q, 2.2f, BIOME[5].c); prev = q; } }
    // props: trees where it is grassy and not too steep, rocks on slopes and high ground
    remove_scattered(lv, e);
    int trees[KIT_MAX], rocks[KIT_MAX]; int nt = 0, nr = 0;
    for (int i = 0; i < e->nkit; i++) { if (!strcmp(e->kit[i].category, "trees")) trees[nt++] = i; else if (!strcmp(e->kit[i].category, "rocks")) rocks[nr++] = i; }
    srand(e->seed);
    float span = (TERRAIN_N - 1) * tr->cell;
    for (float z = tr->origin.z + 3; z < tr->origin.z + span - 3 && lv->nprops < LEVEL_MAX_PROPS - 48; z += 3.0f)
        for (float x = tr->origin.x + 3; x < tr->origin.x + span - 3 && lv->nprops < LEVEL_MAX_PROPS - 48; x += 3.0f) {
            Vec3 q = v3(x + ((rand() % 100) / 100.0f - 0.5f) * 2.4f, 0, z + ((rand() % 100) / 100.0f - 0.5f) * 2.4f);
            q.y = terrain_height(tr, q.x, q.z);
            if (q.y < e->g_water + 0.8f) continue;
            float slope = 1 - terrain_normal(tr, q.x, q.z).y;
            float forest = terrain_noise(e->seed + 99, q.x, q.z, 22) * e->g_forest * 1.6f;   // clumps
            bool near_pad = hypotf(q.x - lv->spawn.x, q.z - lv->spawn.z) < 9 || hypotf(q.x - lv->boss_spawn.x, q.z - lv->boss_spawn.z) < 13;
            // stay off the path: painted path colour is a good enough marker
            Vec3 c = tr->color[(int)((q.z - tr->origin.z) / tr->cell) * TERRAIN_N + (int)((q.x - tr->origin.x) / tr->cell)];
            bool on_path = fabsf(c.x - BIOME[5].c.x) < 0.08f && fabsf(c.y - BIOME[5].c.y) < 0.08f;
            if (near_pad || on_path) continue;
            float r = (rand() % 1000) / 1000.0f;
            int pick = -1;
            if (nt && slope < 0.32f && q.y < e->g_snow - 2 && r < forest * 0.55f) pick = trees[rand() % nt];
            else if (nr && (slope > 0.3f || q.y > e->g_snow - 4) && r < e->g_rocks * 0.5f) pick = rocks[rand() % nr];
            else if (nr && r < e->g_rocks * 0.03f) pick = rocks[rand() % nr];
            if (pick < 0) continue;
            const KitPiece *k = &e->kit[pick];
            Prop *pr = &lv->props[lv->nprops++]; memset(pr, 0, sizeof *pr);
            snprintf(pr->file, sizeof pr->file, "%s", k->file);
            pr->pos = q; pr->yaw = (float)(rand() % 360) * DEG2RAD; pr->scale = k->scale * (0.8f + 0.4f * (rand() % 100) / 100.0f); pr->stretch = v3(1, 1, 1); pr->tint = v4(1, 1, 1, 1); pr->glow = k->glow; pr->collide = k->collide;
            add_collider_for(lv, pr);
        }
    // everything else placed stands on the new ground
    for (int i = 0; i < lv->nprops; i++) { Prop *pr = &lv->props[i]; if (terrain_inside(tr, pr->pos.x, pr->pos.z)) { remove_collider_for(lv, pr); pr->pos.y = terrain_height(tr, pr->pos.x, pr->pos.z); add_collider_for(lv, pr); } }
    for (int i = 0; i < lv->nlights; i++) if (terrain_inside(tr, lv->lights[i].pos.x, lv->lights[i].pos.z)) lv->lights[i].pos.y = fmaxf(lv->lights[i].pos.y, terrain_height(tr, lv->lights[i].pos.x, lv->lights[i].pos.z) + 1.0f);
    e->dirty = true;
    char msg[160]; snprintf(msg, sizeof msg, "world %u: %d pieces placed; sculpt and paint over it, Ctrl+S saves", e->seed, lv->nprops); say(e, msg);
}

// ---------------------------------------------------------------- grouping into parts

// The selected pieces become one part file (positions relative to the first piece, on the ground)
// and are replaced in the level by a single prop that references it.
static void group_as_part(LevelEd *e, Level *lv) {
    int idx[65]; int n = 0;
    if (e->sel_prop >= 0 && e->sel_prop < lv->nprops) idx[n++] = e->sel_prop;
    for (int i = 0; i < e->nmulti && n < 65; i++) { bool dup = false; for (int k = 0; k < n; k++) if (idx[k] == e->multi[i]) dup = true; if (!dup && e->multi[i] >= 0 && e->multi[i] < lv->nprops) idx[n++] = e->multi[i]; }
    if (n == 0) { say(e, "select a piece first"); return; }
    PartDoc d; memset(&d, 0, sizeof d);
    Vec3 origin = lv->props[idx[0]].pos; float oyaw = lv->props[idx[0]].yaw;
    float c = cosf(-oyaw), sn = sinf(-oyaw);
    for (int i = 0; i < n && d.n < PART_MAX_PIECES; i++) {
        const Prop *p = &lv->props[idx[i]]; Piece *pc = &d.pieces[d.n++];
        snprintf(pc->file, sizeof pc->file, "%s", p->file);
        Vec3 rel = v3_sub(p->pos, origin);
        pc->pos = v3(rel.x * c - rel.z * sn, rel.y, rel.x * sn + rel.z * c);   // undo the anchor's yaw
        Vec3 st = p->stretch.x == 0 && p->stretch.y == 0 && p->stretch.z == 0 ? v3(1, 1, 1) : p->stretch;
        pc->size = v3(p->scale * st.x, p->scale * st.y, p->scale * st.z);
        pc->yaw = (p->yaw - oyaw) / DEG2RAD; pc->tint = p->tint;
    }
    char dir[640]; snprintf(dir, sizeof dir, "%s/models/own", HOLLOW_ASSET_DIR); SDL_CreateDirectory(dir);
    char path[700]; snprintf(path, sizeof path, "%s/%s.part", dir, e->part_name);
    if (!part_save(&d, path)) { say(e, "part save failed (see hollow.log)"); return; }
    push_undo(e, lv);
    // remove the pieces (highest index first), add the group prop
    for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) if (idx[b] > idx[a]) { int t = idx[a]; idx[a] = idx[b]; idx[b] = t; }
    for (int i = 0; i < n; i++) { remove_collider_for(lv, &lv->props[idx[i]]); for (int k = idx[i]; k < lv->nprops - 1; k++) lv->props[k] = lv->props[k + 1]; lv->nprops--; }
    Prop *g2 = &lv->props[lv->nprops++]; memset(g2, 0, sizeof *g2);
    snprintf(g2->file, sizeof g2->file, "models/own/%s.part", e->part_name); g2->pos = origin; g2->yaw = oyaw; g2->scale = 1; g2->stretch = v3(1, 1, 1); g2->tint = v4(1, 1, 1, 1);
    e->sel_prop = lv->nprops - 1; e->nmulti = 0; e->dirty = true;
    // the part joins the palette right away
    char rel[160]; snprintf(rel, sizeof rel, "models/own/%s.part", e->part_name);
    bool known = false; for (int i = 0; i < e->nkit; i++) if (!strcmp(e->kit[i].file, rel)) known = true;
    if (!known && e->nkit < KIT_MAX) { KitPiece *k = &e->kit[e->nkit++]; memset(k, 0, sizeof *k); snprintf(k->category, sizeof k->category, "own"); snprintf(k->name, sizeof k->name, "%s", e->part_name); snprintf(k->file, sizeof k->file, "%s", rel); k->scale = 1;
        bool kc = false; for (int i = 0; i < e->ncat; i++) if (!strcmp(e->categories[i], "own")) kc = true; if (!kc && e->ncat < LEVELED_MAX_CATS) snprintf(e->categories[e->ncat++], 16, "own"); }
    e->props_stale = true;
    char msg[160]; snprintf(msg, sizeof msg, "saved models/own/%s.part (%d pieces); it is in the palette under own", e->part_name, n); say(e, msg);
}

// A placed part comes apart into its pieces (the file stays).
static void ungroup(LevelEd *e, Level *lv) {
    if (e->sel_prop < 0 || e->sel_prop >= lv->nprops) return;
    Prop group = lv->props[e->sel_prop];
    char path[640]; snprintf(path, sizeof path, "%s/%s", HOLLOW_ASSET_DIR, group.file);
    PartDoc d; if (!part_load(&d, path)) { say(e, "cannot read that part"); return; }
    push_undo(e, lv);
    remove_collider_for(lv, &lv->props[e->sel_prop]);
    for (int k = e->sel_prop; k < lv->nprops - 1; k++) lv->props[k] = lv->props[k + 1]; lv->nprops--;
    float c = cosf(group.yaw), sn = sinf(group.yaw);
    e->nmulti = 0;
    for (int i = 0; i < d.n && lv->nprops < LEVEL_MAX_PROPS; i++) {
        const Piece *pc = &d.pieces[i]; Prop *p = &lv->props[lv->nprops++]; memset(p, 0, sizeof *p);
        snprintf(p->file, sizeof p->file, "%s", pc->file);
        Vec3 rel = v3(pc->pos.x * group.scale, pc->pos.y * group.scale, pc->pos.z * group.scale);
        p->pos = v3(group.pos.x + rel.x * c + rel.z * sn, group.pos.y + rel.y, group.pos.z - rel.x * sn + rel.z * c);
        p->yaw = group.yaw + pc->yaw * DEG2RAD; p->scale = group.scale; p->stretch = pc->size; p->tint = pc->tint;
        if (e->nmulti < 64) e->multi[e->nmulti++] = lv->nprops - 1;
    }
    e->sel_prop = lv->nprops - 1; e->dirty = true;
    say(e, "ungrouped; the pieces are selected");
}

static void part_name_keys(LevelEd *e, const Input *in) {
    if (!e->part_name_focus) return;
    size_t n = strlen(e->part_name);
    for (int sc = SDL_SCANCODE_A; sc <= SDL_SCANCODE_Z; sc++) if (in->tool_key_down[sc] && n < sizeof e->part_name - 1) { e->part_name[n++] = (char)('a' + (sc - SDL_SCANCODE_A)); e->part_name[n] = 0; }
    for (int sc = SDL_SCANCODE_1; sc <= SDL_SCANCODE_0; sc++) if (in->tool_key_down[sc] && n < sizeof e->part_name - 1) { e->part_name[n++] = sc == SDL_SCANCODE_0 ? '0' : (char)('1' + (sc - SDL_SCANCODE_1)); e->part_name[n] = 0; }
    if ((in->tool_key_down[SDL_SCANCODE_MINUS] || in->tool_key_down[SDL_SCANCODE_SPACE]) && n < sizeof e->part_name - 1) { e->part_name[n++] = '_'; e->part_name[n] = 0; }
    if (in->tool_key_down[SDL_SCANCODE_BACKSPACE] && n > 0) e->part_name[--n] = 0;
    if (in->tool_key_down[SDL_SCANCODE_RETURN] || in->tool_key_down[SDL_SCANCODE_ESCAPE]) e->part_name_focus = false;
}

// Panel layout flows with the window width: rows of equal buttons, two slider columns when
// there is room, one otherwise. All sizes in points.
#define P_M 12.0f          // margin
#define P_G 6.0f           // gap
#define P_ROW 30.0f        // button / toggle height
static float row_w(float w, int n) { return (w - 2 * P_M - (n - 1) * P_G) / n; }
static int cols_for(float w, float min_w) { int c = (int)((w - 2 * P_M + P_G) / (min_w + P_G)); return c < 1 ? 1 : c; }

void leveled_panel(LevelEd *e, Level *lv, Terrain *tr, Ui *ui, float w, float h) {
    float x = P_M, y = P_M; e->tr = tr;
    const float head_h = P_M + 32 + P_ROW + 10, foot_h = 76;
    // The tab content scrolls with the wheel when it is taller than the window; header and footer
    // stay put and are drawn last so scrolled content slides under them.
    int tab = e->tab < 0 || e->tab > 2 ? 0 : e->tab;
    float view_h = h - head_h - foot_h;
    float max_scroll = fmaxf(0, e->pcontent[tab] - view_h);
    UiInput saved_in = ui->in;
    bool in_content = ui->in.my >= head_h && ui->in.my < h - foot_h;
    if (in_content && ui->in.wheel != 0 && !(tab == 0 && e->wheel_in_list)) e->pscroll[tab] = clampf(e->pscroll[tab] - ui->in.wheel * 40, 0, max_scroll);
    e->pscroll[tab] = clampf(e->pscroll[tab], 0, max_scroll);
    if (!in_content) { ui->in.mx = -1e6f; ui->in.my = -1e6f; ui->in.pressed = false; ui->in.wheel = 0; }   // header/footer own the mouse there
    y = head_h - e->pscroll[tab];
    float content_top = y;
    e->wheel_in_list = false;
    bool two_col = w >= 640;
    float cw = two_col ? (w - 2 * P_M - P_G) / 2 : w - 2 * P_M;   // column width
    float c2 = x + cw + P_G;                                        // second column x

    if (e->tab == 0) {
        // tool row
        const char *tools[] = { "1 SELECT", "2 PIECE", "3 LIGHT", "4 EMITTER" };
        { float bw = row_w(w, 4); for (int i = 0; i < 4; i++) { bool on = (int)e->tool == i; if (ui_toggle(ui, x + i * (bw + P_G), y, bw, P_ROW, tools[i], &on) && on) e->tool = (EdTool2)i; } }
        y += P_ROW + 10;
        // categories
        { int cols = cols_for(w, 96); if (cols > e->ncat) cols = e->ncat; float bw = row_w(w, cols);
          for (int i = 0; i < e->ncat; i++) { bool on = e->cat == i; if (ui_toggle(ui, x + (i % cols) * (bw + P_G), y + (i / cols) * (P_ROW + P_G), bw, P_ROW, e->categories[i], &on) && on) { e->cat = i; e->list_sel = 0; } }
          y += (P_ROW + P_G) * ((e->ncat + cols - 1) / cols) + 6; }
        // pieces list (left) and new-piece settings (right)
        static const char *names[KIT_MAX]; static int map[KIT_MAX]; int n = 0;
        for (int i = 0; i < e->nkit; i++) if (!strcmp(e->kit[i].category, e->categories[e->cat])) { names[n] = e->kit[i].name; map[n] = i; n++; }
        if (e->list_sel >= n) e->list_sel = n > 0 ? n - 1 : 0;
        float list_h = two_col ? fmaxf(220, fminf(420, h - head_h - foot_h - 250)) : 220;
        if (saved_in.mx >= x && saved_in.mx < x + cw && saved_in.my >= y && saved_in.my < y + list_h) e->wheel_in_list = true;
        if (ui_list(ui, 0, x, y, cw, list_h, names, n, &e->list_sel) || (n > 0 && e->piece != map[e->list_sel])) {
            if (n > 0) { e->piece = map[e->list_sel]; e->ghost_scale = e->kit[e->piece].scale; e->ghost_collide = e->kit[e->piece].collide > 0; e->tool = LT_PIECE; }
        }
        float gx = two_col ? c2 : x, gy = two_col ? y : y + list_h + 10, gw = cw;
        ui_label(ui, gx, gy, e->tool == LT_LIGHT ? "NEW LIGHT" : e->tool == LT_EMITTER ? "NEW EMITTER" : "NEW PIECE", v4(1, 0.85f, 0.4f, 1)); gy += 24;
        if (e->tool == LT_LIGHT) {
            ui_color(ui, gx, gy, gw, "colour", &e->light_color, 1.5f); gy += 3 * 26 + 8;
            ui_slider(ui, gx, gy, gw, "radius", &e->light_radius, 1, 20); gy += 28;
            ui_slider(ui, gx, gy, gw, "intensity", &e->light_intensity, 0, 10); gy += 30;
        } else if (e->tool == LT_EMITTER) {
            ui_label(ui, gx, gy, "left click places a firefly emitter; edit it once selected", v4(0.7f, 0.68f, 0.65f, 1)); gy += 26;
        } else {
            ui_stepper(ui, gx, gy, "scale", &e->ghost_scale, 0.1f, 0.1f, 10); gy += 32;
            float yaw_deg = e->ghost_yaw / DEG2RAD; if (ui_stepper(ui, gx, gy, "yaw", &yaw_deg, 15, -360, 360)) e->ghost_yaw = yaw_deg * DEG2RAD; gy += 32;
            float hw = (gw - P_G) / 2;
            ui_toggle(ui, gx, gy, hw, P_ROW, "collides", &e->ghost_collide); ui_toggle(ui, gx + hw + P_G, gy, hw, P_ROW, "snap 0.5 m", &e->snap); gy += P_ROW + 10;
            ui_label(ui, gx, gy, "left click places   R rotates   [ ] scale", v4(0.7f, 0.68f, 0.65f, 1)); gy += 24;
        }
        y = fmaxf(y + list_h, gy) + 12;
        // selected item
        if (e->sel_prop >= 0 && e->sel_prop < lv->nprops) {
            Prop *p = &lv->props[e->sel_prop]; char s[200];
            snprintf(s, sizeof s, "SELECTED PIECE  %s", strrchr(p->file, '/') ? strrchr(p->file, '/') + 1 : p->file); ui_label(ui, x, y, s, v4(1, 0.85f, 0.4f, 1)); y += 22;
            snprintf(s, sizeof s, "at %.1f %.1f %.1f    drag in the world to move, F flies to it", p->pos.x, p->pos.y, p->pos.z); ui_label_fit(ui, x, y, w - 2 * P_M, s, v4(0.7f, 0.68f, 0.65f, 1)); y += 26;
            float sx = x;
            if (ui_stepper(ui, sx, y, "scale", &p->scale, 0.1f, 0.1f, 10)) e->dirty = true; sx += ui_stepper_w("scale") + 16;
            float yd = p->yaw / DEG2RAD;
            if (sx + ui_stepper_w("yaw") > w - P_M) { sx = x; y += 32; }
            if (ui_stepper(ui, sx, y, "yaw", &yd, 15, -360, 360)) { p->yaw = yd * DEG2RAD; e->dirty = true; } sx += ui_stepper_w("yaw") + 16;
            if (sx + ui_stepper_w("height") > w - P_M) { sx = x; y += 32; }
            if (ui_stepper(ui, sx, y, "height", &p->pos.y, 0.25f, -5, 20)) e->dirty = true; y += 34;
            bool col = p->collide > 0; if (ui_toggle(ui, x, y, cw, P_ROW, "collides", &col)) { remove_collider_for(lv, p); p->collide = col ? 0.6f : 0; add_collider_for(lv, p); e->dirty = true; }
            y += P_ROW + 6;
            if (p->stretch.x == 0 && p->stretch.y == 0 && p->stretch.z == 0) p->stretch = v3(1, 1, 1);
            if (ui_slider(ui, x, y, cw, "stretch x", &p->stretch.x, 0.1f, 4)) e->dirty = true; y += 26;
            if (ui_slider(ui, x, y, cw, "stretch y", &p->stretch.y, 0.1f, 4)) e->dirty = true; y += 26;
            if (ui_slider(ui, x, y, cw, "stretch z", &p->stretch.z, 0.1f, 4)) e->dirty = true; y += 30;
            { Vec3 t = v3(p->tint.x, p->tint.y, p->tint.z); if (ui_color(ui, x, y, cw, "colour", &t, 1.5f)) { p->tint = v4(t.x, t.y, t.z, 1); e->dirty = true; } }
            if (ui_color(ui, two_col ? c2 : x, two_col ? y : y + 3 * 26 + 6, cw, "glow", &p->glow, 1.5f)) e->dirty = true;
            y += two_col ? 3 * 26 + 6 : 2 * (3 * 26 + 6);
            float hw = (cw - P_G) / 2;
            if (ui_button(ui, x, y, hw, P_ROW, "DELETE  X")) delete_selected(e, lv);
            if (ui_button(ui, x + hw + P_G, y, hw, P_ROW, "DUPLICATE  G")) { push_undo(e, lv); Prop copy = *p; copy.pos.x += 1.5f; if (lv->nprops < LEVEL_MAX_PROPS) { lv->props[lv->nprops++] = copy; add_collider_for(lv, &copy); e->sel_prop = lv->nprops - 1; } }
            y += P_ROW + 10;
            // grouping: shift-click more pieces, then save them as one part
            { size_t L = strlen(p->file); bool is_part = L > 5 && !strcmp(p->file + L - 5, ".part");
              char s2[120]; snprintf(s2, sizeof s2, "GROUP   %d piece%s selected (shift-click adds more)", e->nmulti > 1 ? e->nmulti : 1, e->nmulti > 1 ? "s" : ""); ui_label_fit(ui, x, y, w - 2 * P_M, s2, v4(1, 0.85f, 0.4f, 1)); y += 24;
              float nw = fminf(200, cw);
              bool inside = saved_in.mx >= x && saved_in.mx < x + nw && saved_in.my >= y && saved_in.my < y + P_ROW && in_content;
              if (saved_in.pressed) e->part_name_focus = inside;
              if (!e->part_name[0]) snprintf(e->part_name, sizeof e->part_name, "%s", "my_part");
              gfx_ui_rect(ui->g, x, y, nw, P_ROW, e->part_name_focus ? v4(0.24f, 0.23f, 0.28f, 1) : v4(0.16f, 0.16f, 0.19f, 1));
              gfx_ui_rect(ui->g, x, y + P_ROW - 2, nw, 2, e->part_name_focus ? v4(1, 0.85f, 0.4f, 1) : v4(0.36f, 0.35f, 0.40f, 1));
              char nm[64]; snprintf(nm, sizeof nm, "%s%s", e->part_name, e->part_name_focus ? "_" : ""); gfx_ui_text(ui->g, x + 8, y + P_ROW * 0.5f - gfx_ui_line_h(1.1f) * 0.5f, 1.1f, v4(0.9f, 0.9f, 0.88f, 1), nm);
              if (ui_button(ui, x + nw + P_G, y, fminf(220, w - x - nw - 2 * P_G - P_M), P_ROW, "SAVE AS PART")) group_as_part(e, lv);
              if (is_part && ui_button(ui, x + nw + P_G + fminf(220, w - x - nw - 2 * P_G - P_M) + P_G, y, 120, P_ROW, "UNGROUP")) ungroup(e, lv);
              y += P_ROW + 10; }
        } else if (e->sel_light >= 0 && e->sel_light < lv->nlights) {
            LevelLight *l = &lv->lights[e->sel_light];
            ui_label(ui, x, y, "SELECTED LIGHT", v4(1, 0.85f, 0.4f, 1)); y += 24;
            if (ui_color(ui, x, y, cw, "colour", &l->color, 1.5f)) e->dirty = true;
            float sx2 = two_col ? c2 : x, sy = two_col ? y : y + 3 * 26 + 6;
            if (ui_slider(ui, sx2, sy, cw, "radius", &l->radius, 1, 25)) e->dirty = true; sy += 26;
            if (ui_slider(ui, sx2, sy, cw, "intensity", &l->intensity, 0, 10)) e->dirty = true; sy += 26;
            if (ui_slider(ui, sx2, sy, cw, "flicker", &l->flicker, 0, 1)) e->dirty = true; sy += 30;
            y = fmaxf(y + 3 * 26 + 6, sy);
            if (ui_stepper(ui, x, y, "height", &l->pos.y, 0.25f, 0, 20)) e->dirty = true;
            if (ui_button(ui, x + ui_stepper_w("height") + 16, y, 130, P_ROW, "DELETE  X")) delete_selected(e, lv);
            y += P_ROW + 10;
        } else if (e->sel_emitter >= 0 && e->sel_emitter < lv->nemitters) {
            LevelEmitter *m = &lv->emitters[e->sel_emitter];
            static const char *types[] = { "firefly", "mist", "ember", "spore", "leaf", "smoke", "spark" };
            ui_label(ui, x, y, "SELECTED EMITTER", v4(1, 0.85f, 0.4f, 1)); y += 24;
            { int cols = cols_for(w, 88); if (cols > 7) cols = 7; float bw = row_w(w, cols);
              for (int i = 0; i < 7; i++) { bool on = !strcmp(m->type, types[i]); if (ui_toggle(ui, x + (i % cols) * (bw + P_G), y + (i / cols) * (P_ROW + P_G), bw, P_ROW, types[i], &on) && on) { snprintf(m->type, sizeof m->type, "%s", types[i]); e->dirty = true; } }
              y += (P_ROW + P_G) * ((7 + cols - 1) / cols) + 6; }
            float sx2 = two_col ? c2 : x, sy = y;
            if (ui_slider(ui, x, sy, cw, "rate", &m->rate, 0, 10)) e->dirty = true;
            if (ui_slider(ui, sx2, two_col ? sy : sy + 26, cw, "size", &m->size, 0.02f, 3)) e->dirty = true; sy += two_col ? 26 : 52;
            if (ui_slider(ui, x, sy, cw, "life", &m->life, 0.5f, 15)) e->dirty = true;
            if (ui_slider(ui, sx2, two_col ? sy : sy + 26, cw, "extent x", &m->extent.x, 0.2f, 30)) e->dirty = true; sy += two_col ? 26 : 52;
            if (ui_slider(ui, x, sy, cw, "extent z", &m->extent.z, 0.2f, 30)) e->dirty = true; sy += 30;
            if (ui_color(ui, x, sy, cw, "colour", &m->color, 3)) e->dirty = true;
            if (ui_button(ui, two_col ? c2 : x, two_col ? sy : sy + 3 * 26 + 6, 130, P_ROW, "DELETE  X")) delete_selected(e, lv);
            y = sy + 3 * 26 + (two_col ? 10 : P_ROW + 16);
        } else {
            ui_label_fit(ui, x, y, w - 2 * P_M, "Click a placed piece, light or emitter in the game window to edit it here.", v4(0.6f, 0.58f, 0.55f, 1)); y += 24;
        }
    } else if (e->tab == 2) {
        // ---- generator: one seed, one world; then paint over it
        ui_label(ui, x, y, "GENERATE   a landmass from a seed, then sculpt and paint over it", v4(1, 0.85f, 0.4f, 1)); y += 24;
        { float sd = (float)e->seed; if (ui_stepper(ui, x, y, "seed", &sd, 1, 1, 99999)) e->seed = (unsigned)sd;
          float sw = ui_stepper_w("seed") + 12;
          if (ui_button(ui, x + sw, y, 120, 26, "NEW SEED")) e->seed = (unsigned)(SDL_GetTicks() % 99989) + 1;
          if (ui_button(ui, x + sw + 126, y, fminf(220, w - x - sw - 126 - P_M), 26, "GENERATE WORLD")) leveled_generate_world(e, lv, tr);
          y += 34; }
        ui_slider(ui, x, y, cw, "mountains", &e->g_mountains, 0, 1); ui_slider(ui, two_col ? c2 : x, two_col ? y : y + 26, cw, "hills", &e->g_hills, 0, 1); y += two_col ? 26 : 52;
        ui_slider(ui, x, y, cw, "roughness", &e->g_rough, 0, 1); ui_slider(ui, two_col ? c2 : x, two_col ? y : y + 26, cw, "forest", &e->g_forest, 0, 1); y += two_col ? 26 : 52;
        ui_slider(ui, x, y, cw, "rocks", &e->g_rocks, 0, 1); ui_slider(ui, two_col ? c2 : x, two_col ? y : y + 26, cw, "snow line", &e->g_snow, 5, 60); y += two_col ? 26 : 52;
        { float wl = e->g_water < -900 ? -3 : e->g_water; if (ui_slider(ui, x, y, cw, "water level", &wl, -3, 12)) { e->g_water = wl <= -2.9f ? -1000 : wl; if (tr->present) { tr->water = e->g_water; e->dirty = true; } } y += 30; }
        if (e->nhm) {
            ui_label_fit(ui, x, y, w - 2 * P_M, "IMPORT HEIGHTMAP   any PNG in assets/heightmaps (real-world or drawn); stretched to the range", v4(1, 0.85f, 0.4f, 1)); y += 24;
            ui_slider(ui, x, y, cw, "height range", &e->hm_range, 5, 120); y += 30;
            int cols = cols_for(w, 120); if (cols > e->nhm) cols = e->nhm; float bw = row_w(w, cols);
            for (int i = 0; i < e->nhm; i++) if (ui_button(ui, x + (i % cols) * (bw + P_G), y + (i / cols) * (P_ROW + P_G), bw, P_ROW, e->hm_names[i])) {
                ensure_terrain(e, lv, tr); push_undo(e, lv);
                if (terrain_import_heightmap(tr, e->hm_files[i], e->hm_range)) { terrain_auto_biome(tr, e->g_snow, e->rock_slope, BIOME[0].c, BIOME[2].c, BIOME[3].c, BIOME[4].c); tr->water = e->g_water; e->dirty = true; say(e, "heightmap imported; GENERATE WORLD keeps its own heights, so scatter with the brush"); }
                else say(e, "could not read that heightmap");
            }
            y += (P_ROW + P_G) * ((e->nhm + cols - 1) / cols) + 6;
        }
        if (!tr->present) {
            ui_label_fit(ui, x, y, w - 2 * P_M, "No terrain yet: GENERATE WORLD or import a heightmap to start.", v4(0.85f, 0.85f, 0.8f, 1)); y += 30;
        } else {
            ui_label_fit(ui, x, y, w - 2 * P_M, "BRUSH   hold the left mouse button on the ground.  Shift + Raise lowers.  Ctrl + wheel = radius", v4(0.6f, 0.58f, 0.55f, 1)); y += 22;
            const char *br[] = { "RAISE", "LOWER", "SMOOTH", "FLATTEN", "PAINT", "SCATTER", "CLEAR", "PATH" };
            { int cols = cols_for(w, 100); if (cols > 8) cols = 8; float bw = row_w(w, cols);
              for (int i = 0; i < 8; i++) { bool on = e->tbrush == i; if (ui_toggle(ui, x + (i % cols) * (bw + P_G), y + (i / cols) * (P_ROW + P_G), bw, P_ROW, br[i], &on) && on) e->tbrush = i; }
              y += (P_ROW + P_G) * ((8 + cols - 1) / cols) + 6; }
            ui_slider(ui, x, y, cw, "radius", &e->tradius, 1, 40);
            ui_slider(ui, two_col ? c2 : x, two_col ? y : y + 26, cw, "strength", &e->tstrength, 0.5f, 30); y += two_col ? 32 : 58;
            static const struct { const char *name; Vec3 c; } B[] = { {"grass", {0.20f, 0.34f, 0.16f}}, {"forest floor", {0.11f, 0.17f, 0.10f}}, {"rock", {0.36f, 0.34f, 0.35f}}, {"snow", {0.88f, 0.90f, 0.95f}}, {"dirt", {0.30f, 0.22f, 0.15f}}, {"path", {0.55f, 0.50f, 0.44f}}, {"water", {0.10f, 0.22f, 0.32f}}, {"moss", {0.28f, 0.42f, 0.20f}} };
            ui_label(ui, x, y, "PAINT COLOUR", v4(1, 0.85f, 0.4f, 1)); y += 24;
            { int cols = cols_for(w, 110); if (cols > 8) cols = 8; float bw = row_w(w, cols);
              for (int i = 0; i < 8; i++) { bool on = e->tpaint_sel == i && e->tbrush == TB_PAINT; float bx = x + (i % cols) * (bw + P_G), by = y + (i / cols) * (P_ROW + P_G);
                  if (ui_toggle(ui, bx, by, bw, P_ROW, B[i].name, &on) && on) { e->tpaint_sel = i; e->tpaint = B[i].c; e->tbrush = TB_PAINT; }
                  gfx_ui_rect(ui->g, bx + bw - 26, by + 6, 18, P_ROW - 12, v4(B[i].c.x, B[i].c.y, B[i].c.z, 1)); }
              y += (P_ROW + P_G) * ((8 + cols - 1) / cols) + 6; }
            if (ui_color(ui, x, y, cw, "custom", &e->tpaint, 1)) { e->tbrush = TB_PAINT; e->tpaint_sel = -1; }
            float sx2 = two_col ? c2 : x, sy = two_col ? y : y + 3 * 26 + 8;
            ui_label(ui, sx2, sy, "SCATTER   kit category", v4(1, 0.85f, 0.4f, 1)); sy += 24;
            { int cols = cw >= 300 ? 3 : 2; float bw = (cw - (cols - 1) * P_G) / cols;
              for (int i = 0; i < e->ncat && i < 6; i++) { bool on = e->scatter_cat == i && e->tbrush == 5; if (ui_toggle(ui, sx2 + (i % cols) * (bw + P_G), sy + (i / cols) * (P_ROW + P_G), bw, P_ROW, e->categories[i], &on) && on) { e->scatter_cat = i; e->tbrush = 5; } }
              sy += (P_ROW + P_G) * ((e->ncat < 6 ? e->ncat : 6) + cols - 1) / cols + 4; }
            ui_slider(ui, sx2, sy, cw, "density", &e->scatter_density, 0.05f, 3); sy += 32;
            y = fmaxf(y + 3 * 26 + 8, sy);
            ui_label_fit(ui, x, y, w - 2 * P_M, "AUTO BIOME   grass, rock on slopes, snow above a height", v4(1, 0.85f, 0.4f, 1)); y += 24;
            ui_slider(ui, x, y, cw, "snow height", &e->snow_h, 0, 60);
            ui_slider(ui, two_col ? c2 : x, two_col ? y : y + 26, cw, "rock slope", &e->rock_slope, 0.1f, 0.9f); y += two_col ? 32 : 58;
            { float bw = row_w(w, two_col ? 2 : 1), step = two_col ? bw + P_G : 0; float by = y;
              if (ui_button(ui, x, by, bw, P_ROW, "APPLY AUTO BIOME")) { push_undo(e, lv); terrain_auto_biome(tr, e->snow_h, e->rock_slope, B[0].c, B[2].c, B[3].c, B[4].c); e->dirty = true; }
              if (!two_col) by += P_ROW + P_G;
              if (ui_button(ui, x + step, by, bw, P_ROW, "MOUNTAIN FOREST LOOK")) {
                  Look *k = &lv->look;
                  k->sun_dir = v3(0.35f, -0.55f, 0.45f); k->sun_intensity = 1.1f; k->sun_color = v3(1.0f, 0.92f, 0.8f);
                  k->sky_ambient = v3(0.35f, 0.45f, 0.65f); k->ground_ambient = v3(0.10f, 0.12f, 0.10f);
                  k->fog_color = v3(0.55f, 0.66f, 0.80f); k->fog_density = 0.012f; k->fog_base = 0; k->fog_falloff = 0.04f; k->fog_scatter = 0.5f; k->fog_start = 10;
                  k->sky_zenith = v3(0.18f, 0.35f, 0.70f); k->sky_horizon = v3(0.70f, 0.80f, 0.92f); k->sky_ground = v3(0.25f, 0.30f, 0.35f); k->sun_glow = 0.5f; k->stars = 0; k->sky_fog_blend = 0.6f;
                  k->exposure = 1.05f; k->saturation = 1.1f; k->contrast = 1.05f; k->bloom = 0.25f; k->bloom_threshold = 1.1f; k->lift = v3(0.01f, 0.01f, 0.02f); k->gain = v3(1, 1, 1);
                  e->dirty = true; say(e, "mountain forest look applied (LOOK tab to tune)");
              }
              y = by + P_ROW + 10; }
        }
    } else {
        Look *k = &lv->look;
        // sun as yaw/pitch for easy tuning
        static float sun_yaw = 0, sun_pitch = 0; static bool init = false;
        if (!init) { sun_yaw = atan2f(k->sun_dir.x, k->sun_dir.z) / DEG2RAD; sun_pitch = asinf(clampf(-k->sun_dir.y / fmaxf(v3_len(k->sun_dir), 1e-4f), -1, 1)) / DEG2RAD; init = true; }
        float sy = y; float cx2 = two_col ? c2 : x;
        #define SL(col, lbl, ptr, lo, hi) do { float *_p = (ptr); float _x = (col) ? cx2 : x; float _y = (col) && two_col ? sy : y; if (ui_slider(ui, _x, _y, cw, lbl, _p, lo, hi)) e->dirty = true; if (!(col) || !two_col) { y += 26; } if ((col) && two_col) sy += 26; } while (0)
        #define ROW() do { if (two_col) { y += 26; sy = y; } } while (0)
        #define SECTION(t) do { y = fmaxf(y, sy) + 6; ui_label(ui, x, y, t, v4(1, 0.85f, 0.4f, 1)); y += 24; sy = y; } while (0)
        #define CL(col, lbl, ptr, hi) do { float _x = (col) ? cx2 : x; float _y = (col) && two_col ? sy : y; if (ui_color(ui, _x, _y, cw, lbl, ptr, hi)) e->dirty = true; if (!(col) || !two_col) y += 3 * 26 + 6; if ((col) && two_col) sy += 3 * 26 + 6; } while (0)
        bool ch = false;
        SECTION(w >= 760 ? "TIME OF DAY   the sun, sky and fog colour follow the clock (off = the values below)" : "TIME OF DAY   (off = the values below)");
        { float hr = k->daytime < 0 ? -1 : k->daytime;
          if (ui_slider(ui, x, y, cw, hr < 0 ? "hour  off" : "hour", &hr, -1, 24)) { k->daytime = hr < 0 ? -1 : hr; e->dirty = true; }
          y += 30;
          static const struct { const char *name; float hour; } TOD[] = { {"DAWN 6.5", 6.5f}, {"NOON 13", 13.0f}, {"GOLDEN 18.5", 18.5f}, {"NIGHT 1", 1.0f} };
          float bw = row_w(w, 4);
          for (int i = 0; i < 4; i++) if (ui_button(ui, x + i * (bw + P_G), y, bw, P_ROW, TOD[i].name)) { k->daytime = TOD[i].hour; e->dirty = true; }
          y += P_ROW + 4; sy = y; }
        SECTION("SUN AND FOG");
        ch |= ui_slider(ui, x, y, cw, "sun yaw", &sun_yaw, -180, 180);
        if (two_col) { ch |= ui_slider(ui, cx2, sy, cw, "sun pitch", &sun_pitch, 0, 89); sy += 26; y += 26; } else { y += 26; ch |= ui_slider(ui, x, y, cw, "sun pitch", &sun_pitch, 0, 89); y += 26; sy = y; }
        if (ch) { float p = sun_pitch * DEG2RAD, yw = sun_yaw * DEG2RAD; k->sun_dir = v3(sinf(yw) * cosf(p), -sinf(p), cosf(yw) * cosf(p)); e->dirty = true; }
        SL(0, "sun power", &k->sun_intensity, 0, 3); SL(1, "fog density", &k->fog_density, 0, 0.12f); ROW();
        SL(0, "fog base y", &k->fog_base, -5, 10); SL(1, "fog falloff", &k->fog_falloff, 0, 1); ROW();
        SL(0, "fog start", &k->fog_start, 0, 30); SL(1, "sun scatter", &k->fog_scatter, 0, 2); ROW();
        SECTION("COLOURS");
        CL(0, "sun", &k->sun_color, 2); CL(1, "fog", &k->fog_color, 1); if (two_col) { y = fmaxf(y, sy); sy = y; }
        CL(0, "sky ambient", &k->sky_ambient, 1); CL(1, "ground ambient", &k->ground_ambient, 1); if (two_col) { y = fmaxf(y, sy); sy = y; }
        CL(0, "sky zenith", &k->sky_zenith, 1); CL(1, "sky horizon", &k->sky_horizon, 1); if (two_col) { y = fmaxf(y, sy); sy = y; }
        SECTION("GRADE");
        SL(0, "exposure", &k->exposure, 0.2f, 3); SL(1, "saturation", &k->saturation, 0, 2); ROW();
        SL(0, "contrast", &k->contrast, 0.5f, 1.8f); SL(1, "bloom", &k->bloom, 0, 1.5f); ROW();
        SL(0, "bloom threshold", &k->bloom_threshold, 0.2f, 3); SL(1, "stars", &k->stars, 0, 3); ROW();
        SECTION("TOON SHADING");
        SL(0, "toon softness", &k->toon_softness, 0.01f, 0.4f); SL(1, "shadow floor", &k->shadow_floor, 0, 0.6f); ROW();
        SL(0, "rim power", &k->rim_power, 1, 8); SL(1, "sun shadows", &k->shadow, 0, 1); ROW();
        SECTION(w >= 760 ? "PIXEL CHARACTERS   3D characters drawn as pixel art (size 0 = off)" : "PIXEL CHARACTERS   (size 0 = off)");
        { float sc = k->pixel_scale; float _y = y; if (ui_slider(ui, x, _y, cw, "pixel size", &sc, 0, 6)) { k->pixel_scale = roundf(sc); e->dirty = true; }
          float lv2 = k->pixel_levels; if (ui_slider(ui, two_col ? cx2 : x, two_col ? _y : _y + 26, cw, "colour levels", &lv2, 0, 16)) { k->pixel_levels = roundf(lv2); e->dirty = true; }
          y += two_col ? 26 : 52; sy = y; }
        SL(0, "outline", &k->pixel_outline, 0, 1); SL(1, "inner lines", &k->pixel_inner, 0, 1); ROW();
        { bool pal = k->pixel_palette > 0.5f; if (ui_toggle(ui, x, y, cw, P_ROW, "snap to the 32-colour palette", &pal)) { k->pixel_palette = pal ? 1 : 0; e->dirty = true; } y += P_ROW + 10; }
        #undef SL
        #undef ROW
        #undef SECTION
        #undef CL
    }
    e->pcontent[tab] = y - content_top;
    ui->in = saved_in;
    if (in_content) { ui->in.mx = -1e6f; ui->in.my = -1e6f; ui->in.pressed = false; }   // content owned the mouse
    // header and tabs, over the scrolled content
    Vec4 bg = v4(0.05f, 0.05f, 0.07f, 1);
    gfx_ui_rect(ui->g, 0, 0, w, head_h - 4, bg);
    y = P_M;
    ui_header(ui, x, y, e->dirty ? "ENVIRONMENT EDITOR  * unsaved" : "ENVIRONMENT EDITOR"); y += 32;
    {
        float bw = row_w(w, 5);
        if (ui_button(ui, x, y, bw, P_ROW, e->tab == 0 ? "[ PLACE ]" : "PLACE")) e->tab = 0;
        if (ui_button(ui, x + (bw + P_G), y, bw, P_ROW, e->tab == 1 ? "[ LOOK ]" : "LOOK")) e->tab = 1;
        if (ui_button(ui, x + 2 * (bw + P_G), y, bw, P_ROW, e->tab == 2 ? "[ TERRAIN ]" : "TERRAIN")) e->tab = 2;
        if (ui_button(ui, x + 3 * (bw + P_G), y, bw, P_ROW, "UNDO ^Z")) pop_undo(e, lv);
        if (ui_button(ui, x + 4 * (bw + P_G), y, bw, P_ROW, "SAVE ^S")) leveled_save(e, lv, tr);
    }
    if (max_scroll > 0) {   // scroll bar at the right edge of the content area
        float bh = view_h * view_h / (e->pcontent[tab] > 0 ? e->pcontent[tab] : 1), by = head_h + (view_h - bh) * (e->pscroll[tab] / max_scroll);
        gfx_ui_rect(ui->g, w - 6, head_h, 4, view_h, v4(0.12f, 0.12f, 0.14f, 1));
        gfx_ui_rect(ui->g, w - 6, by, 4, fmaxf(bh, 16), v4(0.45f, 0.43f, 0.40f, 1));
    }
    // footer
    gfx_ui_rect(ui->g, 0, h - foot_h, w, foot_h, bg);
    if (e->msg_t > 0) ui_label_fit(ui, x, h - 72, w - 2 * P_M, e->msg, v4(1, 0.85f, 0.4f, 1));
    ui_label_fit(ui, x, h - 48, w - 2 * P_M, "GAME WINDOW   WASD + QE fly, right-drag look, wheel speed, wheel here scrolls", v4(0.5f, 0.48f, 0.45f, 1));
    ui_label_fit(ui, x, h - 28, w - 2 * P_M, "click place / select, drag move, R rotate, [ ] scale, X delete, G duplicate, F fly to", v4(0.5f, 0.48f, 0.45f, 1));
    ui->in = saved_in;
}

bool leveled_save(LevelEd *e, Level *lv, Terrain *tr) {
    if (tr && tr->present) { snprintf(lv->terrain_file, sizeof lv->terrain_file, "%s", tr->file); if (!terrain_save(tr, HOLLOW_ASSET_DIR)) say(e, "terrain save failed"); }
    bool ok = level_save(lv, lv->path);
    say(e, ok ? "saved level" : "save failed, see log");
    if (ok) e->dirty = false;
    dbg_log("editor: save %s -> %s", lv->path, ok ? "ok" : "FAILED");
    return ok;
}
