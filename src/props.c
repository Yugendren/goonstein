#include "props.h"
#include "render_world.h"
#include "prof.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void props_clear(Gfx *g, PropCache *pc) {
    for (int i = 0; i < pc->n; i++) if (pc->models[i].part) { free(pc->models[i].part); pc->models[i].part = NULL; }
    for (int i = 0; i < pc->n; i++) if (pc->models[i].lod_ok) { model_destroy(g, &pc->models[i].lod); pc->models[i].lod_ok = false; }
    for (int i = 0; i < pc->n; i++) if (pc->models[i].ok) model_destroy(g, &pc->models[i].model);
    memset(pc, 0, sizeof *pc);
}

static PropModel *find(PropCache *pc, const char *file) {
    for (int i = 0; i < pc->n; i++) if (!strcmp(pc->models[i].file, file)) return &pc->models[i];
    return NULL;
}

static PropModel *load_one(Gfx *g, PropCache *pc, const char *file);
void props_load_level(Gfx *g, PropCache *pc, const Level *lv) {
    for (int i = 0; i < lv->nprops; i++) load_one(g, pc, lv->props[i].file);
}

// Skip a prop once its bounding sphere covers less than this much of the view: radius / distance.
// At the game's ~50 degree vertical fov and 1080 lines that is roughly four pixels across, small
// enough that dropping it is invisible but common enough to remove most of a dense world's far half.
#define PROP_CULL_SIZE 0.004f

// And swap to the far stand-in below ten times that -- radius over distance 0.04, which at the
// game's fov is a prop about ninety pixels across on a 1080-line frame. A shrub a metre across
// keeps its real mesh out to twenty-five metres and is a decimated one past that. Above this line
// the art is exactly what it always was.
#define PROP_LOD_SIZE 0.04f

// Rest-pose bounding sphere of a piece or assembly, computed once per file and cached.
static bool prop_sphere(Gfx *g, PropCache *pc, PropModel *pm, Vec3 *cen, float *rad) {
    if (pm->bsphere == 0) {
        Vec3 lo, hi;
        if (props_bounds(g, pc, pm->file, &lo, &hi)) {
            pm->bcen = v3_scale(v3_add(lo, hi), 0.5f);
            pm->brad = v3_len(v3_scale(v3_sub(hi, lo), 0.5f));
            pm->bsphere = 1;
        } else pm->bsphere = -1;
    }
    if (pm->bsphere < 0) return false;
    *cen = pm->bcen; *rad = pm->brad;
    return true;
}

// ---------------------------------------------------------------- instance collection
//
// props_draw's twin: the same walk over the level, the same bounding-sphere cull, the same
// recursion into .part assemblies -- but instead of a draw call per mesh it queues an instance.
// Nothing here touches the GPU, because it runs before the frame's first render pass opens.
//
// What it buys, on the island: a palm is thirty pieces and they are all the same two meshes with
// the same two textures, so the hundred and fifteen palms collapse from 3450 draws to 2.

// A prop with a skinned mesh anywhere in it cannot be instanced: skinning wants a joint matrix
// palette per draw and an instance has nowhere to keep one. Such a prop is drawn whole, the old
// way, rather than half instanced and half not. Asked once per file and cached, because for a
// .part it means loading and walking the whole assembly.
// HOLLOW_NOLOD=1 draws the real mesh everywhere, which is the only way to measure what the far
// stand-ins are worth: the same camera path with and without them, triangles counted both times.
static bool props_lod_off(void) {
    static int v = -1;
    if (v < 0) v = SDL_getenv("HOLLOW_NOLOD") != NULL;
    return v != 0;
}

static bool model_has_skin(const Model *m) {
    for (int i = 0; i < m->nmeshes; i++) if (m->meshes[i].skinned) return true;
    return false;
}
static bool prop_needs_fallback(Gfx *g, PropCache *pc, PropModel *pm, int depth) {
    if (pm->fallback) return pm->fallback > 0;
    int v = 0;
    if (pm->part) {
        if (depth <= 2)
            for (int i = 0; i < pm->part->n && !v; i++) {
                PropModel *lp = load_one(g, pc, pm->part->pieces[i].file);
                if (lp && prop_needs_fallback(g, pc, lp, depth + 1)) v = 1;
            }
    } else v = model_has_skin(&pm->model) ? 1 : 0;
    pm->fallback = v ? 1 : -1;
    return v != 0;
}

// Piece names and local matrices, resolved once per .part file (see PropModel's comment).
static void cache_pieces(Gfx *g, PropCache *pc, PropModel *pm) {
    if (pm->piece_cached || !pm->part) return;
    pm->piece_cached = true;
    for (int i = 0; i < pm->part->n && i < PART_MAX_PIECES; i++) {
        pm->piece_pm[i] = load_one(g, pc, pm->part->pieces[i].file);
        pm->piece_mat[i] = piece_matrix(&pm->part->pieces[i]);
    }
}

// `sets` is a bitmask of 1 << GfxInstSet: a prop the sun and the camera both see is expanded once
// and queued into both.
static void collect_matrix(Gfx *g, PropCache *pc, unsigned sets, bool lod, PropModel *pm, Mat4 world,
                           Vec4 tint, Vec3 glow, const Texture *tex, float tile, int depth) {
    if (!pm || !pm->ok) return;
    if (pm->part) {
        if (depth > 2) return;
        cache_pieces(g, pc, pm);
        for (int i = 0; i < pm->part->n && i < PART_MAX_PIECES; i++) {
            const Piece *p = &pm->part->pieces[i];
            Vec4 t = v4(tint.x * p->tint.x, tint.y * p->tint.y, tint.z * p->tint.z, tint.w);
            const Texture *pt = tex; float ptile = tile;
            if (p->tex >= 0 && pc->wt) { pt = world_texture(pc->wt, p->tex); ptile = p->tex_tile > 0 ? p->tex_tile : 1.0f; }
            collect_matrix(g, pc, sets, lod, pm->piece_pm[i], m4_mul(world, pm->piece_mat[i]), t, glow, pt, ptile, depth + 1);
        }
        return;
    }
    bool use_lod = lod && pm->lod_ok && !props_lod_off();
    const Model *m = use_lod ? &pm->lod : &pm->model;
    const ModelPose *pose = use_lod ? &pm->lod_rest : &pm->rest;
    for (int i = 0; i < m->nmeshes; i++) {
        const ModelMesh *mm = &m->meshes[i];
        bool hidden = false;
        for (int n = mm->node; n >= 0; n = m->nodes[n].parent) if (m->nodes[n].hidden) { hidden = true; break; }
        if (hidden) continue;
        Mat4 w = m4_mul(world, pose->global[mm->node]);
        const Texture *use = tex ? tex : &m->textures[mm->tex];
        Vec4 uv = tex ? v4(tile, 0, 0, 0) : v4(1, 1, 0, 0);
        if (sets & (1u << GFX_SET_SHADOW)) gfx_instance(g, GFX_SET_SHADOW, &mm->gpu, use, w, tint, uv, tex != NULL, glow);
        if (sets & (1u << GFX_SET_WORLD))  gfx_instance(g, GFX_SET_WORLD,  &mm->gpu, use, w, tint, uv, tex != NULL, glow);
    }
}

// Which PropModel every prop line resolves to, rebuilt when the level changes under us.
static void cache_prop_models(Gfx *g, PropCache *pc, const Level *lv) {
    if (pc->by_prop_lv == lv && pc->by_prop_n == lv->nprops && pc->by_prop_stamp == lv->mtime) return;
    pc->by_prop_lv = lv; pc->by_prop_n = lv->nprops; pc->by_prop_stamp = lv->mtime;
    for (int i = 0; i < lv->nprops && i < LEVEL_MAX_PROPS; i++) {
        PropModel *pm = find(pc, lv->props[i].file);
        pc->by_prop[i] = (pm && pm->ok) ? pm : NULL;
        if (pc->by_prop[i]) { prop_needs_fallback(g, pc, pc->by_prop[i], 0); cache_pieces(g, pc, pc->by_prop[i]); }
    }
    (void)g;
}

void props_collect(Gfx *g, PropCache *pc, const Level *lv, const struct WorldTextures *wt,
                   Mat4 shadow_vp, bool want_shadow, Mat4 camera_vp, Vec3 cam_pos) {
    pc->wt = wt;
    pc->nfb[GFX_SET_SHADOW] = pc->nfb[GFX_SET_WORLD] = 0;
    cache_prop_models(g, pc, lv);
    Frustum sun_fr = frustum_from_view_proj(shadow_vp), cam_fr = frustum_from_view_proj(camera_vp);
    unsigned drawn = 0, culled = 0;
    for (int i = 0; i < lv->nprops; i++) {
        PropModel *pm = pc->by_prop[i];
        if (!pm) continue;
        const Prop *p = &lv->props[i];
        Vec3 st = p->stretch.x == 0 && p->stretch.y == 0 && p->stretch.z == 0 ? v3(1, 1, 1) : p->stretch;
        Vec3 s = v3(p->scale * st.x, p->scale * st.y, p->scale * st.z);
        Mat4 world = m4_trs(p->pos, p->yaw, s);
        bool in_sun = want_shadow, in_cam = true, cam_lod = true;
        Vec3 lc; float lr;
        if (prop_sphere(g, pc, pm, &lc, &lr)) {   // no bounds (a missing piece): always drawn
            Vec3 c = m4_mul_point(world, lc);
            float r = lr * fmaxf(fabsf(s.x), fmaxf(fabsf(s.y), fabsf(s.z)));   // yaw keeps lengths
            // The sun set needs no distance cut of its own: game.c fits the sun's box around what
            // the camera looks at, so the frustum test is that box, exactly. Measuring from the eye
            // instead over-culls once the camera pulls back (at dist 120, 116 of 464 props inside
            // the box lost their shadows that way).
            in_sun = want_shadow && frustum_sees_sphere(&sun_fr, c, r);
            // Camera set: also drop what is only a few pixels across.
            float dist = v3_len(v3_sub(c, cam_pos));
            in_cam = frustum_sees_sphere(&cam_fr, c, r) && r >= PROP_CULL_SIZE * dist;
            cam_lod = r < PROP_LOD_SIZE * dist;
        }
        if (in_cam) drawn++; else culled++;
        if (!in_sun && !in_cam) continue;
        if (pm->fallback > 0) {   // a skinned mesh somewhere in it: drawn whole, the old way
            if (in_sun && pc->nfb[GFX_SET_SHADOW] < LEVEL_MAX_PROPS) pc->fb[GFX_SET_SHADOW][pc->nfb[GFX_SET_SHADOW]++] = i;
            if (in_cam && pc->nfb[GFX_SET_WORLD]  < LEVEL_MAX_PROPS) pc->fb[GFX_SET_WORLD][pc->nfb[GFX_SET_WORLD]++]  = i;
            continue;
        }
        const Texture *tex = p->tex >= 0 && wt ? world_texture(wt, p->tex) : NULL;
        float tile = p->tex_tile > 0 ? p->tex_tile : 1.0f;
        // The sun always gets the stand-in; the camera gets it once the prop is small on screen.
        // When those agree -- which is most props most of the time -- one walk fills both sets.
        if (in_sun && in_cam && cam_lod)      collect_matrix(g, pc, (1u << GFX_SET_SHADOW) | (1u << GFX_SET_WORLD), true, pm, world, p->tint, p->glow, tex, tile, 0);
        else {
            if (in_sun) collect_matrix(g, pc, 1u << GFX_SET_SHADOW, true,    pm, world, p->tint, p->glow, tex, tile, 0);
            if (in_cam) collect_matrix(g, pc, 1u << GFX_SET_WORLD,  cam_lod, pm, world, p->tint, p->glow, tex, tile, 0);
        }
    }
    pc->props_drawn = drawn; pc->props_culled = culled;
    prof_count(PROF_C_PROPS_DRAWN, drawn); prof_count(PROF_C_PROPS_CULLED, culled);
}

void props_draw_fallback(Gfx *g, PropCache *pc, const Level *lv, const struct WorldTextures *wt, GfxInstSet set) {
    if (pc->nfb[set] <= 0) return;
    pc->wt = wt;
    for (int k = 0; k < pc->nfb[set]; k++) {
        const Prop *p = &lv->props[pc->fb[set][k]];
        Vec3 st = p->stretch.x == 0 && p->stretch.y == 0 && p->stretch.z == 0 ? v3(1, 1, 1) : p->stretch;
        Vec3 s = v3(p->scale * st.x, p->scale * st.y, p->scale * st.z);
        const Texture *tex = p->tex >= 0 && wt ? world_texture(wt, p->tex) : NULL;
        props_draw_matrix(g, pc, p->file, m4_trs(p->pos, p->yaw, s), p->tint, p->glow, tex, p->tex_tile > 0 ? p->tex_tile : 1.0f, 0);
    }
    gfx_set_material(g, NULL);
}

// FILE.recolor beside a model: lines of `r g b  r2 g2 b2` (0-255) move that paint colour, shading kept.
static void apply_recolor_sidecar(Gfx *g, Model *m, const char *model_path) {
    char sp[1100]; snprintf(sp, sizeof sp, "%s.recolor", model_path);
    size_t n = 0; char *text = SDL_LoadFile(sp, &n); if (!text) return;
    unsigned char from[32][3], to[32][3]; int count = 0;
    char *cur = text;
    while (*cur && count < 32) {
        char *line = cur; char *nl = strchr(cur, '\n'); if (nl) { *nl = 0; cur = nl + 1; } else cur += strlen(cur);
        char *hash = strchr(line, '#'); if (hash) *hash = 0;
        int v[6]; if (sscanf(line, "%d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) continue;
        for (int c = 0; c < 3; c++) { from[count][c] = (unsigned char)v[c]; to[count][c] = (unsigned char)v[3 + c]; }
        count++;
    }
    SDL_free(text);
    if (count) { model_recolor(g, m, from, to, count); SDL_Log("recolour: %s (%d colours)", sp, count); }
}
static long long file_mtime(const char *path) { SDL_PathInfo info; return SDL_GetPathInfo(path, &info) ? (long long)info.modify_time : 0; }
static bool load_into(Gfx *g, PropModel *pm, const char *file);
static PropModel *load_one(Gfx *g, PropCache *pc, const char *file) {
    PropModel *pm = find(pc, file);
    if (pm) return pm->ok ? pm : NULL;
    if (pc->n >= PROPS_MAX_MODELS) {
        // Silently drawing nothing is the worst possible answer here: a model that never appears
        // looks like a broken renderer rather than a full cache. Say it once and move on.
        static bool said = false;
        if (!said) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "props: model cache full at %d files, '%s' and anything after it will not be drawn", PROPS_MAX_MODELS, file); said = true; }
        return NULL;
    }
    pm = &pc->models[pc->n++];
    memset(pm, 0, sizeof *pm);
    snprintf(pm->file, sizeof pm->file, "%s", file);
    load_into(g, pm, file);
    return pm->ok ? pm : NULL;
}
int props_hot_reload(Gfx *g, PropCache *pc) {
    static Uint64 last = 0; Uint64 now = SDL_GetTicks(); if (now - last < 1000) return 0; last = now;
    int n = 0;
    for (int i = 0; i < pc->n; i++) {
        PropModel *pm = &pc->models[i];
        char path[1024]; snprintf(path, sizeof path, "%s/%s", HOLLOW_ASSET_DIR, pm->file);
        long long m = file_mtime(path);
        if (m == 0 || m == pm->mtime) continue;
        if (pm->lod_ok) { model_destroy(g, &pm->lod); pm->lod_ok = false; }
        if (pm->ok && pm->part) { free(pm->part); pm->part = NULL; } else if (pm->ok) model_destroy(g, &pm->model);
        pm->ok = false; pm->bsphere = 0; pm->fallback = 0;
        for (int k = 0; k < pc->n; k++) pc->models[k].piece_cached = false;   // a reloaded file may be a piece of any part
        pc->by_prop_lv = NULL;
        load_into(g, pm, pm->file);
        SDL_Log("hot reload: %s", pm->file); n++;
    }
    return n;
}
static bool load_into(Gfx *g, PropModel *pm, const char *file) {
    char path[1024]; snprintf(path, sizeof path, "%s/%s", HOLLOW_ASSET_DIR, file);
    pm->mtime = file_mtime(path);
    size_t L = strlen(file);
    if (L > 5 && !strcmp(file + L - 5, ".part")) {
        pm->part = malloc(sizeof *pm->part);
        pm->ok = pm->part && part_load(pm->part, path);
        if (!pm->ok) { free(pm->part); pm->part = NULL; }
        return pm->ok;
    }
    pm->ok = model_load(g, &pm->model, path, 512);
    if (pm->ok) { AnimPlayer rest = { .clip = -1, .prev = -1 }; model_pose(&pm->model, &rest, &pm->rest); apply_recolor_sidecar(g, &pm->model, path); }
    // The far stand-in, if someone made one. Its textures are capped at 256: it is only ever seen
    // small or as a shadow, and a second full-size copy of every plant atlas is exactly the memory
    // this whole exercise is trying not to spend.
    if (pm->ok) {
        char lp[1100]; snprintf(lp, sizeof lp, "%s.lod.glb", path);
        SDL_PathInfo li;
        if (SDL_GetPathInfo(lp, &li) && model_load(g, &pm->lod, lp, 256)) {
            AnimPlayer rest = { .clip = -1, .prev = -1 };
            model_pose(&pm->lod, &rest, &pm->lod_rest);
            pm->lod_ok = true;
        }
    }
    return pm->ok;
}

void props_draw_matrix(Gfx *g, PropCache *pc, const char *file, Mat4 world, Vec4 tint, Vec3 glow, const Texture *tex, float tile, int depth) {
    PropModel *pm = load_one(g, pc, file);
    if (!pm) return;
    if (pm->part) {
        if (depth > 2) return;
        for (int i = 0; i < pm->part->n; i++) {
            const Piece *p = &pm->part->pieces[i];
            Vec4 t = v4(tint.x * p->tint.x, tint.y * p->tint.y, tint.z * p->tint.z, tint.w);
            const Texture *pt = tex; float ptile = tile;
            if (p->tex >= 0 && pc->wt) { pt = world_texture(pc->wt, p->tex); ptile = p->tex_tile > 0 ? p->tex_tile : 1.0f; }
            props_draw_matrix(g, pc, p->file, m4_mul(world, piece_matrix(p)), t, glow, pt, ptile, depth + 1);
        }
        return;
    }
    Material m = material_default(); m.emissive = glow;
    gfx_set_material(g, &m);
    model_draw_tex(g, &pm->model, &pm->rest, world, tint, tex, tile);
    gfx_set_material(g, NULL);
}
void props_draw_one(Gfx *g, PropCache *pc, const char *file, Vec3 pos, float yaw, float scale, Vec4 tint, Vec3 glow) {
    props_draw_matrix(g, pc, file, m4_trs(pos, yaw, v3(scale, scale, scale)), tint, glow, NULL, 0, 0);
}

bool props_bounds(Gfx *g, PropCache *pc, const char *file, Vec3 *bmin, Vec3 *bmax) {
    PropModel *pm = load_one(g, pc, file);
    if (!pm) return false;
    if (pm->part) {   // union of the pieces' boxes, corners transformed
        Vec3 lo = v3(1e9f, 1e9f, 1e9f), hi = v3(-1e9f, -1e9f, -1e9f); bool any = false;
        for (int i = 0; i < pm->part->n; i++) {
            const Piece *p = &pm->part->pieces[i]; Vec3 a, b;
            if (!props_bounds(g, pc, p->file, &a, &b)) continue;
            Mat4 m = piece_matrix(p);
            for (int c = 0; c < 8; c++) { Vec3 q = v3((c & 1) ? b.x : a.x, (c & 2) ? b.y : a.y, (c & 4) ? b.z : a.z);
                Vec3 w = v3(m.m[0] * q.x + m.m[4] * q.y + m.m[8] * q.z + m.m[12], m.m[1] * q.x + m.m[5] * q.y + m.m[9] * q.z + m.m[13], m.m[2] * q.x + m.m[6] * q.y + m.m[10] * q.z + m.m[14]);
                lo = v3(fminf(lo.x, w.x), fminf(lo.y, w.y), fminf(lo.z, w.z)); hi = v3(fmaxf(hi.x, w.x), fmaxf(hi.y, w.y), fmaxf(hi.z, w.z)); any = true; }
        }
        if (!any) return false;
        *bmin = lo; *bmax = hi; return true;
    }
    *bmin = pm->model.bmin; *bmax = pm->model.bmax;
    return true;
}
