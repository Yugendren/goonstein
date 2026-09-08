#include "terrain.h"
#include <stdlib.h>
#include <string.h>

#define N TERRAIN_N
static inline int idx(int x, int z) { return z * N + x; }

void terrain_init(Terrain *t, float cell, Vec3 origin, float base_height, Vec3 base_color) {
    memset(t, 0, sizeof *t);
    t->cell = cell; t->origin = origin;
    for (int i = 0; i < N * N; i++) { t->height[i] = base_height; t->color[i] = base_color; }
    t->mesh_dirty = true; t->present = true;
}

void terrain_destroy(Gfx *g, Terrain *t) { if (t->mesh_ok) gfx_mesh_destroy(g, &t->mesh); t->mesh_ok = false; }

bool terrain_inside(const Terrain *t, float x, float z) {
    float u = (x - t->origin.x) / t->cell, v = (z - t->origin.z) / t->cell;
    return u >= 0 && v >= 0 && u <= N - 1 && v <= N - 1;
}

float terrain_height(const Terrain *t, float x, float z) {
    if (!t->present) return 0;
    float u = clampf((x - t->origin.x) / t->cell, 0, N - 1.001f), v = clampf((z - t->origin.z) / t->cell, 0, N - 1.001f);
    int ix = (int)u, iz = (int)v; float fx = u - ix, fz = v - iz;
    float h00 = t->height[idx(ix, iz)], h10 = t->height[idx(ix + 1, iz)], h01 = t->height[idx(ix, iz + 1)], h11 = t->height[idx(ix + 1, iz + 1)];
    return lerpf(lerpf(h00, h10, fx), lerpf(h01, h11, fx), fz) + t->origin.y;
}

Vec3 terrain_normal(const Terrain *t, float x, float z) {
    float e = t->cell * 0.5f;
    float hl = terrain_height(t, x - e, z), hr = terrain_height(t, x + e, z), hd = terrain_height(t, x, z - e), hu = terrain_height(t, x, z + e);
    return v3_norm(v3(hl - hr, 2 * e, hd - hu));
}

bool terrain_ray(const Terrain *t, Vec3 a, Vec3 b, Vec3 *hit) {
    if (!t->present) return false;
    Vec3 d = v3_sub(b, a); float len = v3_len(d); if (len < 1e-4f) return false;
    int steps = (int)(len / (t->cell * 0.5f)) + 1; if (steps > 2000) steps = 2000;
    Vec3 prev = a; float prev_d = a.y - terrain_height(t, a.x, a.z);
    for (int i = 1; i <= steps; i++) {
        Vec3 p = v3_add(a, v3_scale(d, (float)i / steps));
        float dd = p.y - terrain_height(t, p.x, p.z);
        if (dd <= 0 && prev_d > 0) {
            float k = prev_d / (prev_d - dd);
            *hit = v3_lerp(prev, p, k); hit->y = terrain_height(t, hit->x, hit->z);
            return terrain_inside(t, hit->x, hit->z);
        }
        prev = p; prev_d = dd;
    }
    return false;
}

void terrain_update_mesh(Gfx *g, Terrain *t) {
    if (!t->present || !t->mesh_dirty) return;
    static Vertex *verts = NULL; static Uint32 *idx32 = NULL;
    if (!verts) { verts = malloc(sizeof(Vertex) * N * N); idx32 = malloc(sizeof(Uint32) * (N - 1) * (N - 1) * 6); }
    for (int z = 0; z < N; z++) for (int x = 0; x < N; x++) {
        Vertex *v = &verts[idx(x, z)];
        float wx = t->origin.x + x * t->cell, wz = t->origin.z + z * t->cell;
        v->pos[0] = wx; v->pos[1] = t->height[idx(x, z)] + t->origin.y; v->pos[2] = wz;
        Vec3 n = terrain_normal(t, wx, wz);
        v->normal[0] = n.x; v->normal[1] = n.y; v->normal[2] = n.z;
        v->uv[0] = wx * 0.5f; v->uv[1] = wz * 0.5f;
        Vec3 c = t->color[idx(x, z)];
        v->color[0] = c.x; v->color[1] = c.y; v->color[2] = c.z; v->color[3] = 1;
    }
    // 16-bit indices cannot address 129*129 = 16641 vertices... they can (max 65535). Build as Uint16.
    static Uint16 *idx16 = NULL; if (!idx16) idx16 = malloc(sizeof(Uint16) * (N - 1) * (N - 1) * 6);
    Uint32 n = 0;
    for (int z = 0; z < N - 1; z++) for (int x = 0; x < N - 1; x++) {
        Uint16 a = (Uint16)idx(x, z), b = (Uint16)idx(x + 1, z), c = (Uint16)idx(x, z + 1), d = (Uint16)idx(x + 1, z + 1);
        // counter-clockwise seen from above (+y): a, c, b  and  b, c, d
        idx16[n++] = a; idx16[n++] = c; idx16[n++] = b;
        idx16[n++] = b; idx16[n++] = c; idx16[n++] = d;
    }
    (void)idx32;
    if (t->mesh_ok) gfx_mesh_destroy(g, &t->mesh);
    t->mesh = gfx_mesh_create(g, verts, N * N, idx16, n);
    t->mesh_ok = true; t->mesh_dirty = false;
}

void terrain_draw(Gfx *g, Terrain *t) {
    if (!t->present) return;
    terrain_update_mesh(g, t);
    if (!t->mesh_ok) return;
    gfx_draw(g, &t->mesh, &g->white, m4_identity(), v4(1, 1, 1, 1), v4(1, 1, 0, 0));
}

// ---------------------------------------------------------------- brushes

static float falloff(float d, float r) { float k = clampf(1 - d / r, 0, 1); return k * k * (3 - 2 * k); }

void terrain_brush(Terrain *t, TerrainBrush brush, Vec3 at, float radius, float strength, float dt, Vec3 paint_color, float flatten_to) {
    if (!t->present) return;
    int x0 = (int)floorf((at.x - radius - t->origin.x) / t->cell), x1 = (int)ceilf((at.x + radius - t->origin.x) / t->cell);
    int z0 = (int)floorf((at.z - radius - t->origin.z) / t->cell), z1 = (int)ceilf((at.z + radius - t->origin.z) / t->cell);
    if (x0 < 0) x0 = 0; if (z0 < 0) z0 = 0; if (x1 > N - 1) x1 = N - 1; if (z1 > N - 1) z1 = N - 1;
    static float tmp[N * N];
    if (brush == TB_SMOOTH) memcpy(tmp, t->height, sizeof tmp);
    for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
        float wx = t->origin.x + x * t->cell, wz = t->origin.z + z * t->cell;
        float d = hypotf(wx - at.x, wz - at.z); if (d > radius) continue;
        float k = falloff(d, radius) * dt;
        int i = idx(x, z);
        switch (brush) {
        case TB_RAISE: t->height[i] += strength * k; break;
        case TB_LOWER: t->height[i] -= strength * k; break;
        case TB_FLATTEN: t->height[i] = lerpf(t->height[i], flatten_to - t->origin.y, clampf(k * 3, 0, 1)); break;
        case TB_SMOOTH: {
            float sum = 0; int cnt = 0;
            for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) { int xx = x + dx, zz = z + dz; if (xx < 0 || zz < 0 || xx >= N || zz >= N) continue; sum += tmp[idx(xx, zz)]; cnt++; }
            t->height[i] = lerpf(t->height[i], sum / cnt, clampf(k * 4, 0, 1));
        } break;
        case TB_PAINT: t->color[i] = v3_lerp(t->color[i], paint_color, clampf(k * 5, 0, 1)); break;
        }
    }
    t->mesh_dirty = true;
}

void terrain_auto_biome(Terrain *t, float snow_h, float rock_slope, Vec3 grass, Vec3 rock, Vec3 snow, Vec3 dirt) {
    for (int z = 0; z < N; z++) for (int x = 0; x < N; x++) {
        float wx = t->origin.x + x * t->cell, wz = t->origin.z + z * t->cell;
        float h = t->height[idx(x, z)];
        float slope = 1.0f - terrain_normal(t, wx, wz).y;       // 0 flat .. 1 vertical
        Vec3 c = grass;
        c = v3_lerp(c, dirt, clampf((0.10f - h) / 0.5f, 0, 0.6f));   // low ground darker/dirtier
        c = v3_lerp(c, rock, clampf((slope - rock_slope * 0.6f) / (rock_slope * 0.6f), 0, 1));
        c = v3_lerp(c, snow, clampf((h - snow_h) / 6.0f, 0, 1) * (1 - clampf((slope - rock_slope) / 0.2f, 0, 0.7f)));
        t->color[idx(x, z)] = c;
    }
    t->mesh_dirty = true;
}

void terrain_generate_mountains(Terrain *t, float peak) {
    for (int z = 0; z < TERRAIN_N; z++) for (int x = 0; x < TERRAIN_N; x++) {
        float u = (float)x / (TERRAIN_N - 1), v = (float)z / (TERRAIN_N - 1);
        float h = 0;
        h += sinf(u * 6.3f + 1.3f) * cosf(v * 5.1f + 0.4f) * 6;
        h += sinf(u * 13.0f + 2.1f) * sinf(v * 11.0f + 1.7f) * 3;
        h += sinf(u * 27.0f) * sinf(v * 23.0f + 0.9f) * 1.2f;
        float edge = fminf(fminf(u, 1 - u), fminf(v, 1 - v));
        float rim = 1 - clampf(edge / 0.22f, 0, 1); rim = rim * rim;
        h += rim * peak + sinf(u * 41.0f + v * 17.0f) * rim * 2.5f;
        float centre = hypotf(u - 0.5f, v - 0.5f);
        h *= clampf((centre - 0.06f) / 0.14f, 0, 1);   // flat middle for the path and arena
        t->height[z * TERRAIN_N + x] = fmaxf(h, 0);
    }
    t->mesh_dirty = true;
}
