// See part.h. Primitives are generated with flat normals so the toon shading reads as faces.
#include "part.h"
#include "model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *KIND[SH_COUNT] = { "box", "cyl", "sphere", "wedge", "obj" };
const char *shape_kind_name(ShapeKind k) { return k >= 0 && k < SH_COUNT ? KIND[k] : "?"; }

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
        if (strcmp(tok[0], "shape") != 0 || nt < 2) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d unknown line", path, ln); continue; }
        if (d->n >= PART_MAX_SHAPES) break;
        Shape *s = &d->shapes[d->n]; memset(s, 0, sizeof *s);
        int k; for (k = 0; k < SH_COUNT; k++) if (!strcmp(tok[1], KIND[k])) break;
        if (k == SH_COUNT) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d unknown shape %s", path, ln, tok[1]); continue; }
        s->kind = (ShapeKind)k;
        int a = 2;
        if (k == SH_OBJ) { if (nt < 3) continue; snprintf(s->file, sizeof s->file, "%s", tok[2]); a = 3; }
        float f[12] = { 0, 0, 0, 1, 1, 1, 0, 0, 0, 0.7f, 0.7f, 0.7f }; int nf = 0;
        for (int i = a; i < nt && nf < 12; i++) f[nf++] = (float)atof(tok[i]);
        s->pos = v3(f[0], f[1], f[2]); s->size = v3(f[3], f[4], f[5]); s->yaw = f[6]; s->pitch = f[7]; s->roll = f[8];
        s->color = v3(f[9], f[10], f[11]); s->tinted = k != SH_OBJ || nf >= 12;
        d->n++;
    }
    SDL_free(text);
    return true;
}

bool part_save(const PartDoc *d, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "cannot write %s", path); return false; }
    fprintf(f, "# part: shape KIND [FILE] x y z  sx sy sz  yaw pitch roll  r g b   (see src/part.h)\n");
    for (int i = 0; i < d->n; i++) {
        const Shape *s = &d->shapes[i];
        fprintf(f, "shape %-6s ", KIND[s->kind]);
        if (s->kind == SH_OBJ) fprintf(f, "%s ", s->file);
        fprintf(f, "%.3f %.3f %.3f  %.3f %.3f %.3f  %.1f %.1f %.1f", s->pos.x, s->pos.y, s->pos.z, s->size.x, s->size.y, s->size.z, s->yaw, s->pitch, s->roll);
        if (s->kind != SH_OBJ || s->tinted) fprintf(f, "  %.3f %.3f %.3f", s->color.x, s->color.y, s->color.z);
        fprintf(f, "\n");
    }
    fclose(f);
    return true;
}

// ---------------------------------------------------------------- geometry

typedef struct TL { Vertex *v; Uint32 n, cap; } TL;
static void tl_tri(TL *t, Vec3 a, Vec3 b, Vec3 c, Vec4 col) {
    if (t->n + 3 > t->cap) { t->cap = t->cap ? t->cap * 2 : 4096; t->v = realloc(t->v, t->cap * sizeof *t->v); }
    Vec3 nrm = v3_norm(v3_cross(v3_sub(b, a), v3_sub(c, a)));
    Vec3 p[3] = { a, b, c };
    for (int k = 0; k < 3; k++) { Vertex *o = &t->v[t->n++]; memset(o, 0, sizeof *o); o->pos[0] = p[k].x; o->pos[1] = p[k].y; o->pos[2] = p[k].z; o->normal[0] = nrm.x; o->normal[1] = nrm.y; o->normal[2] = nrm.z; o->color[0] = col.x; o->color[1] = col.y; o->color[2] = col.z; o->color[3] = 1; }
}
static void tl_quad(TL *t, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec4 col) { tl_tri(t, a, b, c, col); tl_tri(t, a, c, d, col); }

// unit shapes: box spans -0.5..0.5, sits with its base at y = 0 (0..1 in y) so parts stack on the ground
static void gen_box(TL *t, Vec4 c) {
    Vec3 p[8]; for (int i = 0; i < 8; i++) p[i] = v3((i & 1) ? 0.5f : -0.5f, (i & 2) ? 1.0f : 0.0f, (i & 4) ? 0.5f : -0.5f);
    tl_quad(t, p[0], p[2], p[3], p[1], c);   // back  (-z)
    tl_quad(t, p[4], p[5], p[7], p[6], c);   // front (+z)
    tl_quad(t, p[0], p[4], p[6], p[2], c);   // left
    tl_quad(t, p[1], p[3], p[7], p[5], c);   // right
    tl_quad(t, p[2], p[6], p[7], p[3], c);   // top
    tl_quad(t, p[0], p[1], p[5], p[4], c);   // bottom
}
static void gen_wedge(TL *t, Vec4 c) {   // ramp rising toward +z
    Vec3 a = v3(-0.5f, 0, -0.5f), b = v3(0.5f, 0, -0.5f), cc = v3(0.5f, 0, 0.5f), d = v3(-0.5f, 0, 0.5f), e = v3(0.5f, 1, 0.5f), f = v3(-0.5f, 1, 0.5f);
    tl_quad(t, a, b, cc, d, c);       // bottom
    tl_quad(t, d, cc, e, f, c);       // back wall (+z)
    tl_quad(t, a, f, e, b, c);        // slope
    tl_tri(t, a, d, f, c);            // left
    tl_tri(t, b, e, cc, c);           // right
}
static void gen_cyl(TL *t, Vec4 c, int sides) {
    for (int i = 0; i < sides; i++) {
        float a0 = (float)i / sides * 2 * PI, a1 = (float)(i + 1) / sides * 2 * PI;
        Vec3 b0 = v3(cosf(a0) * 0.5f, 0, sinf(a0) * 0.5f), b1 = v3(cosf(a1) * 0.5f, 0, sinf(a1) * 0.5f);
        Vec3 t0 = v3(b0.x, 1, b0.z), t1 = v3(b1.x, 1, b1.z);
        tl_quad(t, b0, t0, t1, b1, c);
        tl_tri(t, v3(0, 1, 0), t0, t1, c) ; tl_tri(t, v3(0, 0, 0), b1, b0, c);
    }
}
static void gen_sphere(TL *t, Vec4 c, int seg) {
    for (int j = 0; j < seg / 2; j++) {
        float p0 = (float)j / (seg / 2) * PI, p1 = (float)(j + 1) / (seg / 2) * PI;
        for (int i = 0; i < seg; i++) {
            float a0 = (float)i / seg * 2 * PI, a1 = (float)(i + 1) / seg * 2 * PI;
            Vec3 q00 = v3(sinf(p0) * cosf(a0) * 0.5f, 0.5f - cosf(p0) * 0.5f, sinf(p0) * sinf(a0) * 0.5f), q01 = v3(sinf(p0) * cosf(a1) * 0.5f, 0.5f - cosf(p0) * 0.5f, sinf(p0) * sinf(a1) * 0.5f);
            Vec3 q10 = v3(sinf(p1) * cosf(a0) * 0.5f, 0.5f - cosf(p1) * 0.5f, sinf(p1) * sinf(a0) * 0.5f), q11 = v3(sinf(p1) * cosf(a1) * 0.5f, 0.5f - cosf(p1) * 0.5f, sinf(p1) * sinf(a1) * 0.5f);
            tl_tri(t, q00, q11, q10, c); tl_tri(t, q00, q01, q11, c);
        }
    }
}

static Mat4 shape_matrix(const Shape *s) {
    Mat4 r = m4_mul(m4_rotate_y(s->yaw * DEG2RAD), m4_mul(m4_rotate_x(s->pitch * DEG2RAD), m4_rotate_z(s->roll * DEG2RAD)));
    return m4_mul(m4_translate(s->pos), m4_mul(r, m4_scale(s->size)));
}
static Vec3 xf_point(Mat4 m, Vec3 p) { return v3(m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12], m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13], m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]); }
static Vec3 xf_dir(Mat4 m, Vec3 p) { return v3_norm(v3(m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z, m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z, m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z)); }

Vertex *part_build(const PartDoc *d, const char *asset_dir, Uint32 *nverts) {
    TL out = {0};
    for (int i = 0; i < d->n; i++) {
        const Shape *s = &d->shapes[i];
        Vec4 col = v4(powf(s->color.x, 2.2f), powf(s->color.y, 2.2f), powf(s->color.z, 2.2f), 1);
        TL t = {0};
        if (s->kind == SH_OBJ) {
            char path[640]; snprintf(path, sizeof path, "%s/%s", asset_dir, s->file);
            Uint32 n = 0; Vertex *v = model_obj_read(path, &n, NULL, NULL);
            if (!v) continue;
            t.v = v; t.n = t.cap = n;
            if (s->tinted) for (Uint32 k = 0; k < n; k++) { t.v[k].color[0] = col.x; t.v[k].color[1] = col.y; t.v[k].color[2] = col.z; }
        } else if (s->kind == SH_BOX) gen_box(&t, col);
        else if (s->kind == SH_WEDGE) gen_wedge(&t, col);
        else if (s->kind == SH_CYL) gen_cyl(&t, col, 12);
        else gen_sphere(&t, col, 12);
        Mat4 m = shape_matrix(s);
        // normals: transform ignoring scale (fine for uniform-ish scales; renormalised)
        Mat4 rot = m4_mul(m4_rotate_y(s->yaw * DEG2RAD), m4_mul(m4_rotate_x(s->pitch * DEG2RAD), m4_rotate_z(s->roll * DEG2RAD)));
        bool mirrored = (s->size.x < 0) ^ (s->size.y < 0) ^ (s->size.z < 0);
        for (Uint32 k = 0; k < t.n; k += 3) {
            Vertex tri[3];
            for (int q = 0; q < 3; q++) {
                tri[q] = t.v[k + (mirrored ? 2 - q : q)];
                Vec3 p = xf_point(m, v3(tri[q].pos[0], tri[q].pos[1], tri[q].pos[2])); tri[q].pos[0] = p.x; tri[q].pos[1] = p.y; tri[q].pos[2] = p.z;
                Vec3 nn = xf_dir(rot, v3(tri[q].normal[0], tri[q].normal[1], tri[q].normal[2])); if (mirrored) nn = v3_scale(nn, -1); tri[q].normal[0] = nn.x; tri[q].normal[1] = nn.y; tri[q].normal[2] = nn.z;
            }
            if (out.n + 3 > out.cap) { out.cap = out.cap ? out.cap * 2 : 8192; out.v = realloc(out.v, out.cap * sizeof *out.v); }
            memcpy(out.v + out.n, tri, sizeof tri); out.n += 3;
        }
        free(t.v);
    }
    *nverts = out.n;
    return out.v;
}

bool part_load_model(Gfx *g, Model *m, const char *path) {
    PartDoc d; if (!part_load(&d, path)) return false;
    Uint32 n = 0; Vertex *v = part_build(&d, HOLLOW_ASSET_DIR, &n);
    if (!v || n == 0) { free(v); SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "part %s: nothing to build", path); return false; }
    bool ok = model_from_triangles(g, m, v, n);
    free(v);
    if (ok) SDL_Log("part %s: %d shapes, %u triangles, %.2f m tall", path, d.n, n / 3, m->bmax.y);
    return ok;
}
