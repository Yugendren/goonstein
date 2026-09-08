#include "pixio.h"
#include "vendor/stb_image.h"
#include "vendor/stb_image_write.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int find_anim(const PixDoc *d, const char *name) {
    for (int i = 0; i < d->nanims; i++) if (!strcmp(d->anims[i].name, name)) return i;
    return -1;
}

void pix_doc_init(PixDoc *d, const char *name) {
    memset(d, 0, sizeof *d);
    snprintf(d->name, sizeof d->name, "%s", name);
    d->size = 2.0f;
}

void pix_doc_free(PixDoc *d) {
    for (int i = 0; i < d->nanims; i++) free(d->anims[i].frames);
    memset(d, 0, sizeof *d);
}

int pix_anim_add(PixDoc *d, const char *name, int fw, int fh, int ndirs, int nframes, float fps, bool loop) {
    if (d->nanims >= PIX_MAX_ANIMS) return -1;
    if (fw < 1) fw = 1; if (fw > PIX_MAX_SIZE) fw = PIX_MAX_SIZE;
    if (fh < 1) fh = 1; if (fh > PIX_MAX_SIZE) fh = PIX_MAX_SIZE;
    if (nframes < 1) nframes = 1; if (nframes > PIX_MAX_FRAMES) nframes = PIX_MAX_FRAMES;
    ndirs = ndirs <= 1 ? 1 : 4;
    PixFrame *frames = calloc((size_t)ndirs * PIX_MAX_FRAMES, sizeof(PixFrame));
    if (!frames) return -1;
    int idx = d->nanims++;
    PixAnim *a = &d->anims[idx];
    memset(a, 0, sizeof *a);
    snprintf(a->name, sizeof a->name, "%s", name);
    a->fw = fw; a->fh = fh; a->ndirs = ndirs; a->nframes = nframes;
    a->fps = fps > 0 ? fps : 8; a->loop = loop;
    a->frames = frames;
    return idx;
}

void pix_anim_remove(PixDoc *d, int idx) {
    if (idx < 0 || idx >= d->nanims) return;
    free(d->anims[idx].frames);
    for (int i = idx; i < d->nanims - 1; i++) d->anims[i] = d->anims[i + 1];
    d->nanims--;
    memset(&d->anims[d->nanims], 0, sizeof d->anims[d->nanims]);
}

PixFrame *pix_frame(PixAnim *a, int dir, int frame) {
    if (!a || dir < 0 || dir >= a->ndirs || frame < 0 || frame >= a->nframes) return NULL;
    return &a->frames[dir * PIX_MAX_FRAMES + frame];
}

bool pix_anim_insert_frame(PixAnim *a, int at, bool duplicate_previous) {
    if (!a) return false;
    if (a->nframes >= PIX_MAX_FRAMES) return false;
    if (at < 0 || at > a->nframes) return false;
    for (int dir = 0; dir < a->ndirs; dir++) {
        PixFrame *base = &a->frames[dir * PIX_MAX_FRAMES];
        memmove(&base[at + 1], &base[at], (size_t)(a->nframes - at) * sizeof(PixFrame));
        if (duplicate_previous && at > 0) base[at] = base[at - 1];
        else memset(&base[at], 0, sizeof(PixFrame));
    }
    for (int i = 0; i < a->ncontact; i++) if (a->contact[i] >= at) a->contact[i]++;
    a->nframes++;
    return true;
}

bool pix_anim_delete_frame(PixAnim *a, int at) {
    if (!a) return false;
    if (a->nframes <= 1) return false;
    if (at < 0 || at >= a->nframes) return false;
    for (int dir = 0; dir < a->ndirs; dir++) {
        PixFrame *base = &a->frames[dir * PIX_MAX_FRAMES];
        memmove(&base[at], &base[at + 1], (size_t)(a->nframes - at - 1) * sizeof(PixFrame));
        memset(&base[a->nframes - 1], 0, sizeof(PixFrame));
    }
    int nc = 0;
    for (int i = 0; i < a->ncontact; i++) {
        if (a->contact[i] == at) continue;
        a->contact[nc++] = a->contact[i] > at ? a->contact[i] - 1 : a->contact[i];
    }
    a->ncontact = nc;
    a->nframes--;
    return true;
}

// Compose a `ndirs*fw` x `nframes*fh` RGBA8 sheet (column = direction, row = frame). Caller frees.
uint8_t *pix_compose_sheet(const PixAnim *a, int *out_w, int *out_h) {
    int w = a->ndirs * a->fw, h = a->nframes * a->fh;
    uint8_t *buf = calloc((size_t)w * (size_t)h, 4);
    if (!buf) return NULL;
    for (int dir = 0; dir < a->ndirs; dir++) {
        for (int f = 0; f < a->nframes; f++) {
            const PixFrame *pf = &a->frames[dir * PIX_MAX_FRAMES + f];
            for (int y = 0; y < a->fh; y++) {
                uint8_t *dst = buf + ((size_t)(f * a->fh + y) * w + (size_t)dir * a->fw) * 4;
                const uint8_t *src = (const uint8_t *)&pf->px[y * PIX_MAX_SIZE];
                memcpy(dst, src, (size_t)a->fw * 4);
            }
        }
    }
    *out_w = w; *out_h = h;
    return buf;
}

// Copy one fw x fh cell (col, row) of a decoded RGBA8 image (stride img_w) into a frame.
static void copy_cell_into_frame(PixFrame *pf, const uint8_t *img, int img_w, int col, int row, int fw, int fh) {
    for (int y = 0; y < fh; y++) {
        const uint8_t *src = img + ((size_t)(row * fh + y) * img_w + (size_t)col * fw) * 4;
        memcpy(&pf->px[y * PIX_MAX_SIZE], src, (size_t)fw * 4);
    }
}

bool pix_doc_save(const PixDoc *d, const char *asset_dir) {
    char dir_sprites[512], dir_chars[512];
    snprintf(dir_sprites, sizeof dir_sprites, "%s/sprites/own", asset_dir);
    snprintf(dir_chars, sizeof dir_chars, "%s/characters", asset_dir);
    if (!SDL_CreateDirectory(dir_sprites)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "pixio: cannot create %s: %s", dir_sprites, SDL_GetError());
        return false;
    }
    if (!SDL_CreateDirectory(dir_chars)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "pixio: cannot create %s: %s", dir_chars, SDL_GetError());
        return false;
    }

    for (int i = 0; i < d->nanims; i++) {
        const PixAnim *a = &d->anims[i];
        int w, h;
        uint8_t *buf = pix_compose_sheet(a, &w, &h);
        if (!buf) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "pixio: out of memory composing %s", a->name); return false; }
        char path[768];
        snprintf(path, sizeof path, "%s/sprites/own/%s_%s.png", asset_dir, d->name, a->name);
        int ok = stbi_write_png(path, w, h, 4, buf, w * 4);
        free(buf);
        if (!ok) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "pixio: failed to write %s", path); return false; }
    }

    char txt_path[768];
    snprintf(txt_path, sizeof txt_path, "%s/sprites/own/%s.txt", asset_dir, d->name);
    FILE *f = fopen(txt_path, "w");
    if (!f) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "pixio: failed to write %s", txt_path); return false; }
    fprintf(f, "# generated by the sprite editor\n");
    fprintf(f, "size %g\n", (double)d->size);
    if (d->nanims > 0) fprintf(f, "frame %d %d\n", d->anims[0].fw, d->anims[0].fh);
    for (int i = 0; i < d->nanims; i++) {
        const PixAnim *a = &d->anims[i];
        fprintf(f, "sheet %s sprites/own/%s_%s.png %d %d\n", a->name, d->name, a->name, a->ndirs, a->nframes);
    }
    for (int i = 0; i < d->nanims; i++) {
        const PixAnim *a = &d->anims[i];
        fprintf(f, "anim %s %s fps %g", a->name, a->name, (double)a->fps);
        if (a->loop) fprintf(f, " loop");
        fprintf(f, " dircol");
        for (int c = 0; c < a->ncontact; c++) fprintf(f, " contact %d", a->contact[c]);
        fprintf(f, "\n");
    }
    fclose(f);

    char char_path[768];
    snprintf(char_path, sizeof char_path, "%s/characters/%s.txt", asset_dir, d->name);
    f = fopen(char_path, "w");
    if (!f) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "pixio: failed to write %s", char_path); return false; }
    fprintf(f, "# generated by the sprite editor\n");
    fprintf(f, "sprite sprites/own/%s.txt\n", d->name);
    fprintf(f, "scale 1.0\n");
    fprintf(f, "yaw_offset 0\n");
    for (int i = 0; i < d->nanims; i++) fprintf(f, "anim %s %s\n", d->anims[i].name, d->anims[i].name);
    bool has_attack = find_anim(d, "attack") >= 0;
    bool has_attack2 = find_anim(d, "attack2") >= 0, has_attack3 = find_anim(d, "attack3") >= 0;
    if (has_attack && !has_attack2 && !has_attack3) { fprintf(f, "anim attack2 attack\n"); fprintf(f, "anim attack3 attack\n"); }
    bool has_hit = find_anim(d, "hit") >= 0;
    if (has_hit && find_anim(d, "hurt") < 0) fprintf(f, "anim hurt hit\n");
    if (has_hit && find_anim(d, "parry") < 0) fprintf(f, "anim parry hit\n");
    if (find_anim(d, "run") < 0 && find_anim(d, "walk") >= 0) fprintf(f, "anim run walk\n");
    fclose(f);
    return true;
}

bool pix_doc_load(PixDoc *d, const char *asset_dir, const char *name) {
    pix_doc_init(d, name);
    char path[768];
    snprintf(path, sizeof path, "%s/sprites/own/%s.txt", asset_dir, name);
    size_t len;
    char *text = SDL_LoadFile(path, &len);
    if (!text) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "pixio: missing %s", path); return false; }

    typedef struct { char name[32]; char path[256]; int cols, rows; } LoadSheet;
    LoadSheet sheets[PIX_MAX_ANIMS]; int nsheets = 0;

    bool ok = true;
    char *cur = text;
    int ln = 0;
    while (*cur) {
        char *line = cur; char *nl = strchr(cur, '\n');
        if (nl) { *nl = 0; cur = nl + 1; } else cur += strlen(cur);
        ln++;
        char *hash = strchr(line, '#'); if (hash) *hash = 0;
        char *tok[32]; int nt = 0; char *save = NULL;
        for (char *t = SDL_strtok_r(line, " \t\r", &save); t && nt < 32; t = SDL_strtok_r(NULL, " \t\r", &save)) tok[nt++] = t;
        if (nt == 0) continue;
        if (!strcmp(tok[0], "size") && nt >= 2) d->size = (float)atof(tok[1]);
        else if (!strcmp(tok[0], "frame")) { /* per-anim frame size is derived from each sheet instead */ }
        else if (!strcmp(tok[0], "sheet") && nt >= 5) {
            if (nsheets >= PIX_MAX_ANIMS) continue;
            LoadSheet *s = &sheets[nsheets++];
            snprintf(s->name, sizeof s->name, "%s", tok[1]);
            snprintf(s->path, sizeof s->path, "%s", tok[2]);
            s->cols = atoi(tok[3]); s->rows = atoi(tok[4]);
        } else if (!strcmp(tok[0], "anim") && nt >= 3) {
            LoadSheet *s = NULL;
            for (int i = 0; i < nsheets; i++) if (!strcmp(sheets[i].name, tok[2])) { s = &sheets[i]; break; }
            if (!s) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d anim %s: unknown sheet %s", path, ln, tok[1], tok[2]); ok = false; continue; }
            char full[1024]; snprintf(full, sizeof full, "%s/%s", asset_dir, s->path);
            int w, h, n;
            uint8_t *img = stbi_load(full, &w, &h, &n, 4);
            if (!img) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "pixio: cannot decode %s", full); ok = false; continue; }
            int cols = s->cols > 0 ? s->cols : 1, rows = s->rows > 0 ? s->rows : 1;
            int fw = w / cols, fh = h / rows;
            float fps = 8; bool loop = false;
            int contact[PIX_MAX_CONTACT]; int ncontact = 0;
            for (int i = 3; i < nt; i++) {
                if (!strcmp(tok[i], "fps") && i + 1 < nt) fps = (float)atof(tok[++i]);
                else if (!strcmp(tok[i], "loop")) loop = true;
                else if (!strcmp(tok[i], "dircol")) { /* always the layout we save */ }
                else if (!strcmp(tok[i], "contact") && i + 1 < nt) { if (ncontact < PIX_MAX_CONTACT) contact[ncontact++] = atoi(tok[++i]); }
            }
            int idx = pix_anim_add(d, tok[1], fw, fh, cols, rows, fps, loop);
            if (idx < 0) { stbi_image_free(img); SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "pixio: too many anims loading %s", path); ok = false; continue; }
            PixAnim *a = &d->anims[idx];
            a->ncontact = ncontact;
            memcpy(a->contact, contact, sizeof(int) * (size_t)ncontact);
            for (int dir = 0; dir < a->ndirs; dir++)
                for (int fr = 0; fr < a->nframes; fr++)
                    copy_cell_into_frame(&a->frames[dir * PIX_MAX_FRAMES + fr], img, w, dir, fr, a->fw, a->fh);
            stbi_image_free(img);
        }
    }
    SDL_free(text);
    return ok && d->nanims > 0;
}

int pix_anim_import_sheet(PixDoc *d, const char *png_path, const char *anim_name, int fw, int fh, bool dircol, float fps, bool loop) {
    int w, h, n;
    uint8_t *img = stbi_load(png_path, &w, &h, &n, 4);
    if (!img) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "pixio: cannot decode %s", png_path); return -1; }
    if (fw < 1) fw = 1;
    if (fh < 1) fh = 1;
    int cols = w / fw; if (cols < 1) cols = 1;
    int rows = h / fh; if (rows < 1) rows = 1;
    int ndirs, nframes;
    bool single_row_strip = false;
    if (dircol) {
        ndirs = cols < 4 ? cols : 4; if (ndirs < 1) ndirs = 1;
        nframes = rows;
    } else if (rows <= 1) {
        ndirs = 1; nframes = cols; single_row_strip = true;
    } else {
        ndirs = rows < 4 ? rows : 4;
        nframes = cols;
    }
    if (nframes < 1) nframes = 1;
    int idx = pix_anim_add(d, anim_name, fw, fh, ndirs, nframes, fps, loop);
    if (idx < 0) { stbi_image_free(img); return -1; }
    PixAnim *a = &d->anims[idx];
    for (int dir = 0; dir < a->ndirs; dir++) {
        for (int fr = 0; fr < a->nframes; fr++) {
            int col = dircol ? dir : fr;
            int row = dircol ? fr : (single_row_strip ? 0 : dir);
            copy_cell_into_frame(&a->frames[dir * PIX_MAX_FRAMES + fr], img, w, col, row, fw, fh);
        }
    }
    stbi_image_free(img);
    return idx;
}
