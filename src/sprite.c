#include "sprite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int find_sheet(const SpriteDef *d, const char *name) {
    for (int i = 0; i < d->nsheets; i++) if (!strcmp(d->sheets[i].name, name)) return i;
    return -1;
}
int sprite_find_anim(const SpriteDef *d, const char *name) {
    for (int i = 0; i < d->nanims; i++) if (!strcmp(d->anims[i].name, name)) return i;
    return -1;
}

bool sprite_def_load(Gfx *g, SpriteDef *d, const char *path) {
    memset(d, 0, sizeof *d);
    d->size = 1.6f; d->frame_w = d->frame_h = 16;
    size_t n; char *text = SDL_LoadFile(path, &n);
    if (!text) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "sprite def missing: %s", path); return false; }
    char *cur = text; int ln = 0;
    while (*cur) {
        char *line = cur; char *nl = strchr(cur, '\n');
        if (nl) { *nl = 0; cur = nl + 1; } else cur += strlen(cur);
        ln++;
        char *hash = strchr(line, '#'); if (hash) *hash = 0;
        char *tok[32]; int nt = 0; char *save = NULL;
        for (char *t = SDL_strtok_r(line, " \t\r", &save); t && nt < 32; t = SDL_strtok_r(NULL, " \t\r", &save)) tok[nt++] = t;
        if (nt == 0) continue;
        if (!strcmp(tok[0], "size") && nt >= 2) d->size = (float)atof(tok[1]);
        else if (!strcmp(tok[0], "frame") && nt >= 3) { d->frame_w = atoi(tok[1]); d->frame_h = atoi(tok[2]); }
        else if (!strcmp(tok[0], "sheet") && nt >= 5) {
            if (d->nsheets >= SPRITE_MAX_SHEETS) continue;
            SpriteSheet *sh = &d->sheets[d->nsheets++];
            memset(sh, 0, sizeof *sh);
            snprintf(sh->name, sizeof sh->name, "%s", tok[1]);
            char full[1024]; snprintf(full, sizeof full, "%s/%s", HOLLOW_ASSET_DIR, tok[2]);
            snprintf(sh->path, sizeof sh->path, "%s", full);
            sh->tex = gfx_texture_load_exact(g, full);
            sh->cols = atoi(tok[3]); sh->rows = atoi(tok[4]);
            sh->fw = sh->cols > 0 ? sh->tex.w / sh->cols : d->frame_w;
            sh->fh = sh->rows > 0 ? sh->tex.h / sh->rows : d->frame_h;
            if (nt >= 7 && !strcmp(tok[5], "foot")) sh->foot = atoi(tok[6]);
            if (sh->tex.tex == g->white.tex) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d sheet %s failed to load", path, ln, tok[2]);
        }
        else if (!strcmp(tok[0], "anim") && nt >= 3) {
            if (d->nanims >= SPRITE_MAX_ANIMS) continue;
            SpriteAnim *a = &d->anims[d->nanims++];
            memset(a, 0, sizeof *a);
            snprintf(a->name, sizeof a->name, "%s", tok[1]);
            a->sheet = find_sheet(d, tok[2]); a->sheet_right = -1; a->fps = 8; a->row = -1; a->first = 0; a->last = -1;
            if (a->sheet < 0) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d anim %s: unknown sheet %s", path, ln, tok[1], tok[2]);
            for (int i = 3; i < nt; i++) {
                if (!strcmp(tok[i], "fps") && i + 1 < nt) a->fps = (float)atof(tok[++i]);
                else if (!strcmp(tok[i], "loop")) a->loop = true;
                else if (!strcmp(tok[i], "dir")) a->directional = true;
                else if (!strcmp(tok[i], "dircol")) { a->directional = true; a->dir_cols = true; }
                else if (!strcmp(tok[i], "right") && i + 1 < nt) a->sheet_right = find_sheet(d, tok[++i]);
                else if (!strcmp(tok[i], "contact") && i + 1 < nt) { if (a->ncontact < SPRITE_MAX_CONTACT) a->contact[a->ncontact++] = atoi(tok[++i]); }
                else if (!strcmp(tok[i], "row") && i + 1 < nt) a->row = atoi(tok[++i]);
                else if (!strcmp(tok[i], "frames") && i + 2 < nt) { a->first = atoi(tok[i + 1]); a->last = atoi(tok[i + 2]); i += 2; }
                else SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d unknown anim key %s", path, ln, tok[i]);
            }
            if (a->last < 0 && a->sheet >= 0) a->last = a->dir_cols ? d->sheets[a->sheet].rows - 1 : d->sheets[a->sheet].cols - 1;
        }
        else SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d unknown key %s", path, ln, tok[0]);
    }
    SDL_free(text);
    return d->nsheets > 0 && d->nanims > 0;
}

void sprite_def_destroy(Gfx *g, SpriteDef *d) {
    for (int i = 0; i < d->nsheets; i++) gfx_texture_destroy(g, &d->sheets[i].tex);
    memset(d, 0, sizeof *d);
}

void sprite_actor_init(SpriteActor *a, const SpriteDef *d) { memset(a, 0, sizeof *a); a->def = d; a->anim = -1; a->rate = 1; a->facing = FACE_DOWN; }

void sprite_play(SpriteActor *a, int anim, float rate, bool restart) {
    if (anim < 0 || anim >= a->def->nanims) return;
    if (a->anim == anim && !restart) { a->rate = rate; return; }
    a->anim = anim; a->time = 0; a->rate = rate > 0 ? rate : 1; a->finished = false;
}

float sprite_contact_time(const SpriteDef *d, int anim, int i, float rate) {
    if (anim < 0 || anim >= d->nanims) return 0;
    const SpriteAnim *a = &d->anims[anim];
    int frame = i < a->ncontact ? a->contact[i] : a->last;
    return ((float)(frame - a->first) + 0.5f) / (a->fps * (rate > 0 ? rate : 1));
}

float sprite_anim_duration(const SpriteDef *d, int anim, float rate) {
    if (anim < 0 || anim >= d->nanims) return 0;
    const SpriteAnim *a = &d->anims[anim];
    return (float)(a->last - a->first + 1) / (a->fps * (rate > 0 ? rate : 1));
}

void sprite_play_fitted(SpriteActor *a, int anim, float lead) {
    if (anim < 0 || anim >= a->def->nanims) return;
    float natural = sprite_contact_time(a->def, anim, 0, 1.0f);
    float rate = (lead > 0.01f && natural > 0.01f) ? natural / lead : 1.0f;
    sprite_play(a, anim, rate, true);
}

void sprite_update(SpriteActor *a, float dt) {
    if (a->anim < 0) return;
    const SpriteAnim *an = &a->def->anims[a->anim];
    a->time += dt;
    float dur = sprite_anim_duration(a->def, a->anim, a->rate);
    if (dur <= 0) return;
    if (an->loop) { while (a->time >= dur) a->time -= dur; }
    else if (a->time >= dur) { a->time = dur - 1e-4f; a->finished = true; }
}

static int facing_row(const SpriteAnim *an, const SpriteSheet *sh, Facing f, bool *flip) {
    *flip = false;
    if (an->row >= 0) return an->row;
    if (!an->directional) return 0;
    if (sh->rows >= 4) return (int)f;               // down, up, left, right
    if (sh->rows == 3) { if (f == FACE_DOWN) return 0; if (f == FACE_UP) return 1; *flip = f == FACE_RIGHT; return 2; }   // down, up, side
    if (sh->rows == 2) return f == FACE_RIGHT ? 1 : 0;   // left, right (down/up draw as left)
    return 0;
}

void sprite_actor_draw(Gfx *g, const SpriteActor *a, Vec3 foot, Vec4 tint, float scale) {
    if (a->anim < 0) return;
    const SpriteAnim *an = &a->def->anims[a->anim];
    int si = (an->sheet_right >= 0 && a->facing == FACE_RIGHT) ? an->sheet_right : an->sheet;
    if (si < 0) return;
    const SpriteSheet *sh = &a->def->sheets[si];
    bool flip = false;
    int row = facing_row(an, sh, a->facing, &flip);
    // Left/right boss sheets: when only a left sheet exists, mirror it for right
    if (an->sheet_right < 0 && !an->directional && a->facing == FACE_RIGHT && an->row < 0 && sh->rows == 1) flip = true;
    int nframes = an->last - an->first + 1;
    int frame = an->first + (int)(a->time * an->fps * a->rate);
    if (frame > an->last) frame = an->last;
    if (frame < an->first) frame = an->first;
    (void)nframes;
    int col = frame % sh->cols;
    if (an->dir_cols) {   // directions across, frames down
        int dcol = sh->cols >= 4 ? (int)a->facing : (sh->cols == 2 ? (a->facing == FACE_RIGHT ? 1 : 0) : 0);
        if (sh->cols == 3) { dcol = a->facing == FACE_DOWN ? 0 : a->facing == FACE_UP ? 1 : 2; flip = a->facing == FACE_RIGHT; }
        col = dcol; row = frame; if (row >= sh->rows) row = sh->rows - 1;
    }
    // Inset by half a texel so nearest sampling never picks up a neighbouring frame's edge.
    float ix = 0.5f / (float)sh->tex.w, iy = 0.5f / (float)sh->tex.h;
    float u0 = (float)col * sh->fw / (float)sh->tex.w + ix;
    float v0 = (float)row * sh->fh / (float)sh->tex.h + iy;
    float u1 = u0 + (float)sh->fw / (float)sh->tex.w - 2 * ix, v1 = v0 + (float)sh->fh / (float)sh->tex.h - 2 * iy;
    float uv[4] = { u0, v0, u1, v1 };
    float h = a->def->size * ((float)sh->fh / (float)(a->def->frame_h > 0 ? a->def->frame_h : sh->fh)) * scale, w = h * (float)sh->fw / (float)sh->fh;
    float px_to_m = h / (float)sh->fh;
    Vec3 fpos = v3(foot.x, foot.y - (float)sh->foot * px_to_m, foot.z);
    gfx_draw_sprite(g, &sh->tex, fpos, w, h, uv, tint, flip);
}

Facing sprite_facing_from(Vec3 dir, Vec3 cam_forward, Vec3 cam_right) {
    dir.y = 0; cam_forward.y = 0; cam_right.y = 0;
    float f = v3_dot(v3_norm(dir), v3_norm(cam_forward)), r = v3_dot(v3_norm(dir), v3_norm(cam_right));
    if (fabsf(r) > fabsf(f)) return r > 0 ? FACE_RIGHT : FACE_LEFT;
    return f > 0 ? FACE_UP : FACE_DOWN;
}

void sprite_set_facing(SpriteActor *a, Vec3 dir, Vec3 cam_forward, Vec3 cam_right) {
    dir.y = 0; cam_forward.y = 0; cam_right.y = 0;
    if (v3_len(dir) < 1e-4f) return;
    Vec3 d = v3_norm(dir);
    float f = v3_dot(d, v3_norm(cam_forward)), r = v3_dot(d, v3_norm(cam_right));
    // score each facing; keep the current one unless another beats it by a margin
    float score[4] = { -f, f, -r, r };
    int best = (int)a->facing; float bs = score[best];
    for (int i = 0; i < 4; i++) if (score[i] > bs + 0.25f) { best = i; bs = score[i]; }
    a->facing = (Facing)best;
}
