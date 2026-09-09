#include "props.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void props_clear(Gfx *g, PropCache *pc) {
    for (int i = 0; i < pc->n; i++) if (pc->models[i].part) { free(pc->models[i].part); pc->models[i].part = NULL; }
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

void props_draw(Gfx *g, PropCache *pc, const Level *lv, float time) {
    (void)time;
    for (int i = 0; i < lv->nprops; i++) {
        const Prop *p = &lv->props[i];
        PropModel *pm = find(pc, p->file);
        if (!pm || !pm->ok) continue;
        Vec3 st = p->stretch.x == 0 && p->stretch.y == 0 && p->stretch.z == 0 ? v3(1, 1, 1) : p->stretch;
        props_draw_matrix(g, pc, p->file, m4_trs(p->pos, p->yaw, v3(p->scale * st.x, p->scale * st.y, p->scale * st.z)), p->tint, p->glow, 0);
    }
    gfx_set_material(g, NULL);
}

static PropModel *load_one(Gfx *g, PropCache *pc, const char *file) {
    PropModel *pm = find(pc, file);
    if (pm) return pm->ok ? pm : NULL;
    if (pc->n >= PROPS_MAX_MODELS) return NULL;
    pm = &pc->models[pc->n++];
    memset(pm, 0, sizeof *pm);
    snprintf(pm->file, sizeof pm->file, "%s", file);
    char path[1024]; snprintf(path, sizeof path, "%s/%s", HOLLOW_ASSET_DIR, file);
    size_t L = strlen(file);
    if (L > 5 && !strcmp(file + L - 5, ".part")) {
        pm->part = malloc(sizeof *pm->part);
        pm->ok = pm->part && part_load(pm->part, path);
        if (!pm->ok) { free(pm->part); pm->part = NULL; }
        return pm->ok ? pm : NULL;
    }
    pm->ok = model_load(g, &pm->model, path, 512);
    if (pm->ok) { AnimPlayer rest = { .clip = -1, .prev = -1 }; model_pose(&pm->model, &rest, &pm->rest); }
    return pm->ok ? pm : NULL;
}

void props_draw_matrix(Gfx *g, PropCache *pc, const char *file, Mat4 world, Vec4 tint, Vec3 glow, int depth) {
    PropModel *pm = load_one(g, pc, file);
    if (!pm) return;
    if (pm->part) {
        if (depth > 2) return;
        for (int i = 0; i < pm->part->n; i++) {
            const Piece *p = &pm->part->pieces[i];
            Vec4 t = v4(tint.x * p->tint.x, tint.y * p->tint.y, tint.z * p->tint.z, tint.w);
            props_draw_matrix(g, pc, p->file, m4_mul(world, piece_matrix(p)), t, glow, depth + 1);
        }
        return;
    }
    Material m = material_default(); m.emissive = glow;
    gfx_set_material(g, &m);
    model_draw(g, &pm->model, &pm->rest, world, tint);
    gfx_set_material(g, NULL);
}
void props_draw_one(Gfx *g, PropCache *pc, const char *file, Vec3 pos, float yaw, float scale, Vec4 tint, Vec3 glow) {
    props_draw_matrix(g, pc, file, m4_trs(pos, yaw, v3(scale, scale, scale)), tint, glow, 0);
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
