#include "props.h"
#include <stdio.h>
#include <string.h>

void props_clear(Gfx *g, PropCache *pc) {
    for (int i = 0; i < pc->n; i++) if (pc->models[i].ok) model_destroy(g, &pc->models[i].model);
    memset(pc, 0, sizeof *pc);
}

static PropModel *find(PropCache *pc, const char *file) {
    for (int i = 0; i < pc->n; i++) if (!strcmp(pc->models[i].file, file)) return &pc->models[i];
    return NULL;
}

void props_load_level(Gfx *g, PropCache *pc, const Level *lv) {
    for (int i = 0; i < lv->nprops; i++) {
        const char *file = lv->props[i].file;
        if (find(pc, file)) continue;
        if (pc->n >= PROPS_MAX_MODELS) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "props: cache full, skipping %s", file); continue; }
        PropModel *pm = &pc->models[pc->n++];
        memset(pm, 0, sizeof *pm);
        snprintf(pm->file, sizeof pm->file, "%s", file);
        char path[1024]; snprintf(path, sizeof path, "%s/%s", HOLLOW_ASSET_DIR, file);
        pm->ok = model_load(g, &pm->model, path, 512);
        if (pm->ok) {
            AnimPlayer rest = { .clip = -1, .prev = -1 };
            model_pose(&pm->model, &rest, &pm->rest);
        }
    }
}

void props_draw(Gfx *g, PropCache *pc, const Level *lv, float time) {
    (void)time;
    for (int i = 0; i < lv->nprops; i++) {
        const Prop *p = &lv->props[i];
        PropModel *pm = find(pc, p->file);
        if (!pm || !pm->ok) continue;
        Material m = material_default();
        m.emissive = p->glow;
        gfx_set_material(g, &m);
        model_draw(g, &pm->model, &pm->rest, m4_trs(p->pos, p->yaw, v3(p->scale, p->scale, p->scale)), p->tint);
    }
    gfx_set_material(g, NULL);
}
