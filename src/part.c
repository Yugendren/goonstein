// See part.h.
#include "part.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool part_load(PartDoc *d, const char *path) {
    memset(d, 0, sizeof *d);
    size_t n = 0; char *text = SDL_LoadFile(path, &n);
    if (!text) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "part missing: %s", path); return false; }
    char *cur = text; int ln = 0;
    while (*cur) {
        char *line = cur; char *nl = strchr(cur, '\n'); if (nl) { *nl = 0; cur = nl + 1; } else cur += strlen(cur);
        ln++;
        char *hash = strchr(line, '#'); if (hash) *hash = 0;
        char *tok[24]; int nt = 0; char *save = NULL;
        for (char *t = SDL_strtok_r(line, " \t\r", &save); t && nt < 24; t = SDL_strtok_r(NULL, " \t\r", &save)) tok[nt++] = t;
        if (nt == 0) continue;
        if (strcmp(tok[0], "collide") == 0 && nt >= 2) {
            d->collide = (float)atof(tok[1]);
            d->collide_h = nt >= 3 && strcmp(tok[2], "deck") != 0 ? (float)atof(tok[2]) : 0;
            for (int i = 2; i < nt; i++) if (strcmp(tok[i], "deck") == 0) d->collide_deck = true;
            continue;
        }
        if (strcmp(tok[0], "piece") != 0 || nt < 3) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d unknown line", path, ln); continue; }
        if (d->n >= PART_MAX_PIECES) break;
        Piece *p = &d->pieces[d->n]; memset(p, 0, sizeof *p);
        snprintf(p->file, sizeof p->file, "%s", tok[1]);
        float f[12] = { 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1 }; int nf = 0;
        for (int i = 2; i < nt && nf < 12; i++) f[nf++] = (float)atof(tok[i]);
        p->pos = v3(f[0], f[1], f[2]); p->size = v3(f[3], f[4], f[5]); p->yaw = f[6]; p->pitch = f[7]; p->roll = f[8]; p->tint = v4(f[9], f[10], f[11], 1);
        d->n++;
    }
    SDL_free(text);
    return true;
}

bool part_save(const PartDoc *d, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "cannot write %s", path); return false; }
    fprintf(f, "# part: piece FILE  x y z  sx sy sz  yaw pitch roll  [r g b]   (see src/part.h)\n");
    if (d->collide > 0) {
        fprintf(f, "collide %.3f", d->collide);
        if (d->collide_h > 0 || d->collide_deck) fprintf(f, " %.3f", d->collide_h > 0 ? d->collide_h : 5.0f);
        if (d->collide_deck) fprintf(f, " deck");
        fprintf(f, "\n");
    }
    for (int i = 0; i < d->n; i++) {
        const Piece *p = &d->pieces[i];
        fprintf(f, "piece %s  %.3f %.3f %.3f  %.3f %.3f %.3f  %.1f %.1f %.1f", p->file, p->pos.x, p->pos.y, p->pos.z, p->size.x, p->size.y, p->size.z, p->yaw, p->pitch, p->roll);
        if (p->tint.x != 1 || p->tint.y != 1 || p->tint.z != 1) fprintf(f, "  %.3f %.3f %.3f", p->tint.x, p->tint.y, p->tint.z);
        fprintf(f, "\n");
    }
    fclose(f);
    return true;
}

Mat4 piece_matrix(const Piece *p) {
    Mat4 r = m4_mul(m4_rotate_y(p->yaw * DEG2RAD), m4_mul(m4_rotate_x(p->pitch * DEG2RAD), m4_rotate_z(p->roll * DEG2RAD)));
    return m4_mul(m4_translate(p->pos), m4_mul(r, m4_scale(p->size)));
}
