#include "terrain.h"
#include <stdlib.h>
#include <string.h>

#define N TERRAIN_N
static inline int idx(int x, int z) { return z * N + x; }

static unsigned hash_u(unsigned x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
static float hnoise(unsigned seed, int x, int z) { return (float)(hash_u(hash_u((unsigned)x * 73856093u ^ seed) ^ (unsigned)z * 19349663u) & 0xffffff) / 16777215.0f; }
static float vnoise(unsigned seed, float x, float z) {
    int xi = (int)floorf(x), zi = (int)floorf(z); float fx = x - xi, fz = z - zi; fx = fx * fx * (3 - 2 * fx); fz = fz * fz * (3 - 2 * fz);
    float a = hnoise(seed, xi, zi), b = hnoise(seed, xi + 1, zi), c = hnoise(seed, xi, zi + 1), d = hnoise(seed, xi + 1, zi + 1);
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fz);
}
static float fbm(unsigned seed, float x, float z, int oct) { float s = 0, amp = 0.5f, f = 1, norm = 0; for (int i = 0; i < oct; i++) { s += vnoise(seed + (unsigned)i * 101, x * f, z * f) * amp; norm += amp; amp *= 0.5f; f *= 2.03f; } return s / norm; }
float terrain_noise(unsigned seed, float x, float z, float scale) { return fbm(seed ^ 0x5bd1e995u, x / scale, z / scale, 3); }

void terrain_init(Terrain *t, float cell, Vec3 origin, float base_height, Vec3 base_color) {
    memset(t, 0, sizeof *t);
    t->cell = cell; t->origin = origin;
    for (int i = 0; i < N * N; i++) { t->height[i] = base_height; t->color[i] = base_color; }
    t->mesh_dirty = true; t->present = true; t->water = -1000;
}

void terrain_destroy(Gfx *g, Terrain *t) {
    if (t->mesh_ok) gfx_mesh_destroy(g, &t->mesh); t->mesh_ok = false;
    if (t->water_ok) gfx_mesh_destroy(g, &t->water_mesh); t->water_ok = false;
    if (t->water_tex_ok) gfx_texture_destroy(g, &t->water_tex); t->water_tex_ok = false;
}

void terrain_set_detail(Terrain *t, const Texture *tex, float tile, Vec3 gain) {
    t->detail = tex; t->detail_tile = tile; t->detail_gain = gain;
}

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
    t->water_dirty = true;   // the sea's shoreline is baked from these heights
}

void terrain_draw(Gfx *g, Terrain *t) {
    if (!t->present) return;
    terrain_update_mesh(g, t);
    if (!t->mesh_ok) return;
    if (t->detail) gfx_draw_planar(g, &t->mesh, t->detail, m4_identity(), v4(t->detail_gain.x, t->detail_gain.y, t->detail_gain.z, 1), t->detail_tile);
    else gfx_draw(g, &t->mesh, &g->white, m4_identity(), v4(1, 1, 1, 1), v4(1, 1, 0, 0));
}

// How far past the grid the sea is carried. Any level's far plane cuts the skirt long before its
// edge, which is the point: the horizon is then a line of fog, not the end of the water.
#define WATER_SKIRT 4.0f
// Depth (metres) at which water stops reading as shallow, and the width of the foam at the waterline.
#define WATER_SHELF 4.0f
#define WATER_FOAM  1.1f

// The sea is two things: a flat mesh (the grid, plus a four-quad frame of open water around it) and
// a texture of the coast -- red is how deep the water is, 0 at the waterline and 1 past the shelf,
// green is the foam mask along that waterline. The shader samples it by world position rather than
// by uv, so the mesh stays a plain grid and the same texture would serve any water surface.
void terrain_update_water(Gfx *g, Terrain *t) {
    if (!t->present || t->water < -900) return;
    if (t->water_ok && !t->water_dirty && t->water_built == t->water) return;
    static Vertex *verts = NULL; static Uint16 *idx16 = NULL;
    const int NV = N * N + 8, NI = (N - 1) * (N - 1) * 6 + 4 * 6;
    if (!verts) { verts = malloc(sizeof(Vertex) * NV); idx16 = malloc(sizeof(Uint16) * NI); }
    if (!verts || !idx16) return;
    float y = t->water;
    for (int z = 0; z < N; z++) for (int x = 0; x < N; x++) {
        Vertex *v = &verts[idx(x, z)];
        v->pos[0] = t->origin.x + x * t->cell; v->pos[1] = y; v->pos[2] = t->origin.z + z * t->cell;
        v->normal[0] = 0; v->normal[1] = 1; v->normal[2] = 0;
        v->uv[0] = v->uv[1] = 0;
        v->color[0] = v->color[1] = v->color[2] = v->color[3] = 1;
    }
    float span = (N - 1) * t->cell;
    float x0 = t->origin.x, z0 = t->origin.z, x1 = x0 + span, z1 = z0 + span;
    float ox0 = x0 - span * WATER_SKIRT, oz0 = z0 - span * WATER_SKIRT, ox1 = x1 + span * WATER_SKIRT, oz1 = z1 + span * WATER_SKIRT;
    const float corner[8][2] = { { x0, z0 }, { x1, z0 }, { x1, z1 }, { x0, z1 },        // grid corners, clockwise from -x -z
                                 { ox0, oz0 }, { ox1, oz0 }, { ox1, oz1 }, { ox0, oz1 } };
    for (int i = 0; i < 8; i++) {
        Vertex *v = &verts[N * N + i];
        v->pos[0] = corner[i][0]; v->pos[1] = y; v->pos[2] = corner[i][1];
        v->normal[0] = 0; v->normal[1] = 1; v->normal[2] = 0;
        v->uv[0] = v->uv[1] = 0;
        v->color[0] = v->color[1] = v->color[2] = v->color[3] = 1;
    }
    Uint32 n = 0;
    for (int z = 0; z < N - 1; z++) for (int x = 0; x < N - 1; x++) {
        Uint16 a = (Uint16)idx(x, z), b = (Uint16)idx(x + 1, z), c = (Uint16)idx(x, z + 1), d = (Uint16)idx(x + 1, z + 1);
        idx16[n++] = a; idx16[n++] = c; idx16[n++] = b;
        idx16[n++] = b; idx16[n++] = c; idx16[n++] = d;
    }
    for (int i = 0; i < 4; i++) {   // frame: each side runs from one grid corner to the next, out to the matching outer pair
        Uint16 ia = (Uint16)(N * N + i), ib = (Uint16)(N * N + (i + 1) % 4);
        Uint16 oa = (Uint16)(N * N + 4 + i), ob = (Uint16)(N * N + 4 + (i + 1) % 4);
        idx16[n++] = ia; idx16[n++] = ib; idx16[n++] = ob;
        idx16[n++] = ia; idx16[n++] = ob; idx16[n++] = oa;
    }
    if (t->water_ok) gfx_mesh_destroy(g, &t->water_mesh);
    t->water_mesh = gfx_mesh_create(g, verts, (Uint32)NV, idx16, n);
    t->water_ok = true; t->water_dirty = false; t->water_built = t->water;

    static unsigned char *px = NULL;
    if (!px) px = malloc((size_t)N * N * 4);
    if (px) {
        for (int i = 0; i < N * N; i++) {
            float depth = fmaxf(y - (t->height[i] + t->origin.y), 0.0f);
            px[i * 4 + 0] = (unsigned char)(clampf(depth / WATER_SHELF, 0, 1) * 255.0f);
            px[i * 4 + 1] = (unsigned char)((1 - clampf(depth / WATER_FOAM, 0, 1)) * 255.0f);
            px[i * 4 + 2] = 0; px[i * 4 + 3] = 255;
        }
        if (t->water_tex_ok) gfx_texture_destroy(g, &t->water_tex);
        t->water_tex = gfx_texture_create(g, px, N, N);
        t->water_tex_ok = true;
    }
}

void terrain_draw_water(Gfx *g, Terrain *t) {
    if (!t->present || t->water < -900) return;
    terrain_update_water(g, t);
    if (!t->water_ok || !t->water_tex_ok) return;
    // The material carries where the coast texture sits in the world: xy is the grid's origin in xz
    // and z one over its span, so the shader can find the right texel from a world position.
    float span = (N - 1) * t->cell;
    Material wm = material_default();
    wm.water = 1; wm.water_origin = v3(t->origin.x, t->origin.z, 1.0f / span);
    wm.emissive = v3(0.015f, 0.045f, 0.075f);
    gfx_set_material(g, &wm);
    gfx_draw(g, &t->water_mesh, &t->water_tex, m4_identity(), v4(0.10f, 0.24f, 0.36f, 1), v4(1, 1, 0, 0));
    gfx_set_material(g, NULL);
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

void terrain_generate(Terrain *t, const TerrainGen *p, Vec3 grass, Vec3 rock, Vec3 snow, Vec3 dirt, Vec3 sand) {
    unsigned seed = p->seed ? p->seed : 1;
    float span = (N - 1) * t->cell;
    for (int z = 0; z < N; z++) for (int x = 0; x < N; x++) {
        float u = (float)x / (N - 1), v = (float)z / (N - 1);
        float wx = t->origin.x + x * t->cell, wz = t->origin.z + z * t->cell;
        float hills = (fbm(seed, u * 4.0f, v * 4.0f, 4) - 0.5f) * 2.0f;                 // -1..1 rolling
        float ridge = 1.0f - fabsf(fbm(seed + 7, u * 2.2f, v * 2.2f, 4) * 2.0f - 1.0f);  // ridged 0..1
        float mask = powf(clampf(fbm(seed + 13, u * 1.3f, v * 1.3f, 2) * 1.6f - 0.3f, 0, 1), 1.5f);   // where the mountains live
        float edge = 1 - clampf(fminf(fminf(u, 1 - u), fminf(v, 1 - v)) / 0.12f, 0, 1);           // rim rises so the world reads as enclosed
        float h = hills * 6.0f * p->hills;
        h += ridge * ridge * 46.0f * p->mountains * fmaxf(mask, edge * 0.8f);
        h += (fbm(seed + 21, u * 16, v * 16, 3) - 0.5f) * 2.5f * p->roughness;
        // pads: blend to the pad height inside, soft edge outside
        for (int k = 0; k < p->nflat; k++) {
            float d = hypotf(wx - p->flat[k].x, wz - p->flat[k].z), r = p->flat_r[k];
            float w = 1 - clampf((d - r) / (r * 0.8f + 2.0f), 0, 1); w = w * w * (3 - 2 * w);
            h = lerpf(h, p->flat[k].y, w);
        }
        t->height[idx(x, z)] = h;
    }
    (void)span;
    t->water = p->water_h;
    t->mesh_dirty = true;
    // colours: biomes from height, slope and a moisture noise
    for (int z = 0; z < N; z++) for (int x = 0; x < N; x++) {
        float wx = t->origin.x + x * t->cell, wz = t->origin.z + z * t->cell;
        float h = t->height[idx(x, z)];
        float slope = 1.0f - terrain_normal(t, wx, wz).y;
        float moist = terrain_noise(seed, wx, wz, 30.0f);
        Vec3 c = v3_lerp(grass, v3_scale(grass, 0.75f), moist);           // wetter = darker grass
        c = v3_lerp(c, dirt, clampf(0.5f - moist, 0, 0.5f) * 0.6f);
        c = v3_lerp(c, sand, clampf((p->water_h + 1.2f - h) / 1.2f, 0, 1));   // shore
        c = v3_lerp(c, rock, clampf((slope - 0.28f) / 0.25f, 0, 1));
        c = v3_lerp(c, snow, clampf((h - p->snow_h) / 5.0f, 0, 1) * (1 - clampf((slope - 0.45f) / 0.2f, 0, 0.7f)));
        t->color[idx(x, z)] = c;
    }
}

void terrain_flatten_pad(Terrain *t, Vec3 at, float radius, float height) {
    int x0 = (int)floorf((at.x - radius * 2 - t->origin.x) / t->cell), x1 = (int)ceilf((at.x + radius * 2 - t->origin.x) / t->cell);
    int z0 = (int)floorf((at.z - radius * 2 - t->origin.z) / t->cell), z1 = (int)ceilf((at.z + radius * 2 - t->origin.z) / t->cell);
    if (x0 < 0) x0 = 0; if (z0 < 0) z0 = 0; if (x1 > N - 1) x1 = N - 1; if (z1 > N - 1) z1 = N - 1;
    for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
        float wx = t->origin.x + x * t->cell, wz = t->origin.z + z * t->cell;
        float d = hypotf(wx - at.x, wz - at.z);
        float w = 1 - clampf((d - radius) / (radius + 0.5f), 0, 1); w = w * w * (3 - 2 * w);
        t->height[idx(x, z)] = lerpf(t->height[idx(x, z)], height - t->origin.y, w);
    }
    t->mesh_dirty = true;
}

void terrain_path(Terrain *t, Vec3 a, Vec3 b, float width, Vec3 color) {
    float len = hypotf(b.x - a.x, b.z - a.z); int steps = (int)(len / (t->cell * 0.5f)) + 1;
    for (int i = 0; i <= steps; i++) {
        float k = (float)i / steps; Vec3 q = v3(lerpf(a.x, b.x, k), 0, lerpf(a.z, b.z, k));
        // level toward the running average height, then paint
        float hc = terrain_height(t, q.x, q.z);
        int x0 = (int)floorf((q.x - width - t->origin.x) / t->cell), x1 = (int)ceilf((q.x + width - t->origin.x) / t->cell);
        int z0 = (int)floorf((q.z - width - t->origin.z) / t->cell), z1 = (int)ceilf((q.z + width - t->origin.z) / t->cell);
        if (x0 < 0) x0 = 0; if (z0 < 0) z0 = 0; if (x1 > N - 1) x1 = N - 1; if (z1 > N - 1) z1 = N - 1;
        for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
            float wx = t->origin.x + x * t->cell, wz = t->origin.z + z * t->cell;
            float d = hypotf(wx - q.x, wz - q.z); if (d > width) continue;
            float w = 1 - clampf((d - width * 0.5f) / (width * 0.5f), 0, 1);
            t->height[idx(x, z)] = lerpf(t->height[idx(x, z)], hc - t->origin.y, 0.5f * w);
            t->color[idx(x, z)] = v3_lerp(t->color[idx(x, z)], color, 0.8f * w);
        }
    }
    t->mesh_dirty = true;
}
