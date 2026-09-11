#include "terrain.h"
#include <stdlib.h>
#include <string.h>

// Every terrain's data is packed tightly at its own live size (t->n), not the max TERRAIN_N, so a
// smaller terrain does not waste time walking storage it isn't using.
static inline int idx(const Terrain *t, int x, int z) { return z * t->n + x; }

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
    t->cell = cell; t->origin = origin; t->n = TERRAIN_N_DEFAULT;
    for (int i = 0; i < t->n * t->n; i++) { t->height[i] = base_height; t->color[i] = base_color; }
    t->mesh_dirty = true; t->present = true; t->water = -1000;
}

void terrain_destroy(Gfx *g, Terrain *t) {
    if (t->mesh_ok) for (int i = 0; i < t->nmesh; i++) gfx_mesh_destroy(g, &t->mesh[i]);
    t->mesh_ok = false; t->nmesh = 0;
    if (t->water_ok) gfx_mesh_destroy(g, &t->water_mesh); t->water_ok = false;
    if (t->water_tex_ok) gfx_texture_destroy(g, &t->water_tex); t->water_tex_ok = false;
}

void terrain_set_detail(Terrain *t, const Texture *tex, float tile, Vec3 gain) {
    t->detail = tex; t->detail_tile = tile; t->detail_gain = gain;
}

bool terrain_inside(const Terrain *t, float x, float z) {
    float u = (x - t->origin.x) / t->cell, v = (z - t->origin.z) / t->cell;
    return u >= 0 && v >= 0 && u <= t->n - 1 && v <= t->n - 1;
}

float terrain_height(const Terrain *t, float x, float z) {
    if (!t->present) return 0;
    float u = clampf((x - t->origin.x) / t->cell, 0, t->n - 1.001f), v = clampf((z - t->origin.z) / t->cell, 0, t->n - 1.001f);
    int ix = (int)u, iz = (int)v; float fx = u - ix, fz = v - iz;
    float h00 = t->height[idx(t, ix, iz)], h10 = t->height[idx(t, ix + 1, iz)], h01 = t->height[idx(t, ix, iz + 1)], h11 = t->height[idx(t, ix + 1, iz + 1)];
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

// A GPU mesh's indices are Uint16, so one mesh can address at most 65536 vertices; the biggest grid
// (TERRAIN_N, the island's 257 a side) has 257*257 = 66049, just over that. So the ground is built as
// up to a 2x2 grid of chunks, each at most TERRAIN_N_DEFAULT (129) vertices a side -- the same size
// as the whole grid used to be, so a t->n == TERRAIN_N_DEFAULT terrain still builds as the single
// chunk it always did (mesh[0], nmesh == 1), unchanged from before this existed.
void terrain_update_mesh(Gfx *g, Terrain *t) {
    if (!t->present || !t->mesh_dirty) return;
    int n = t->n;
    const int CH = TERRAIN_N_DEFAULT - 1;                    // cells per chunk edge
    int nc = (n - 1 + CH - 1) / CH; if (nc < 1) nc = 1;      // chunks per axis
    static Vertex *verts = NULL; static Uint16 *idx16 = NULL;
    if (!verts) { verts = malloc(sizeof(Vertex) * TERRAIN_N_DEFAULT * TERRAIN_N_DEFAULT); idx16 = malloc(sizeof(Uint16) * CH * CH * 6); }
    Mesh built[4]; int nm = 0;
    for (int cz = 0; cz < nc; cz++) for (int cx = 0; cx < nc; cx++) {
        int x0 = cx * CH, x1 = x0 + CH < n - 1 ? x0 + CH : n - 1;
        int z0 = cz * CH, z1 = z0 + CH < n - 1 ? z0 + CH : n - 1;
        int cw = x1 - x0 + 1, cd = z1 - z0 + 1;
        // Every chunk's vertices come from the same global (x, z) -> world formula, so the row or
        // column a chunk shares with its neighbour lands on identical positions and normals: no
        // crack and no lighting seam at the chunk boundary.
        for (int lz = 0; lz < cd; lz++) for (int lx = 0; lx < cw; lx++) {
            int x = x0 + lx, z = z0 + lz;
            Vertex *v = &verts[lz * cw + lx];
            float wx = t->origin.x + x * t->cell, wz = t->origin.z + z * t->cell;
            v->pos[0] = wx; v->pos[1] = t->height[idx(t, x, z)] + t->origin.y; v->pos[2] = wz;
            Vec3 nrm = terrain_normal(t, wx, wz);
            v->normal[0] = nrm.x; v->normal[1] = nrm.y; v->normal[2] = nrm.z;
            v->uv[0] = wx * 0.5f; v->uv[1] = wz * 0.5f;
            Vec3 c = t->color[idx(t, x, z)];
            v->color[0] = c.x; v->color[1] = c.y; v->color[2] = c.z; v->color[3] = 1;
        }
        Uint32 ni = 0;
        for (int lz = 0; lz < cd - 1; lz++) for (int lx = 0; lx < cw - 1; lx++) {
            Uint16 a = (Uint16)(lz * cw + lx), b = (Uint16)(lz * cw + lx + 1), c = (Uint16)((lz + 1) * cw + lx), d = (Uint16)((lz + 1) * cw + lx + 1);
            // counter-clockwise seen from above (+y): a, c, b  and  b, c, d
            idx16[ni++] = a; idx16[ni++] = c; idx16[ni++] = b;
            idx16[ni++] = b; idx16[ni++] = c; idx16[ni++] = d;
        }
        built[nm++] = gfx_mesh_create(g, verts, (Uint32)(cw * cd), idx16, ni);
    }
    if (t->mesh_ok) for (int i = 0; i < t->nmesh; i++) gfx_mesh_destroy(g, &t->mesh[i]);
    for (int i = 0; i < nm; i++) t->mesh[i] = built[i];
    t->nmesh = nm; t->mesh_ok = true; t->mesh_dirty = false;
    t->water_dirty = true;   // the sea's shoreline is baked from these heights
}

void terrain_draw(Gfx *g, Terrain *t) {
    if (!t->present) return;
    terrain_update_mesh(g, t);
    if (!t->mesh_ok) return;
    if (!t->detail) {
        for (int i = 0; i < t->nmesh; i++) gfx_draw(g, &t->mesh[i], &g->white, m4_identity(), v4(1, 1, 1, 1), v4(1, 1, 0, 0));
        return;
    }
    // material.water = 2 says "this is the ground" to lit.frag, which answers it by breaking up
    // the detail map's tiling (see the shader). It rides on the same number the sea already uses
    // -- 1 is the sea, 2 is the ground -- because one bit does not deserve a new uniform, and
    // because the material is the only channel that reaches the fragment stage per draw without
    // touching the instancer's batch key.
    Material m = material_default();
    m.water = 2.0f;
    gfx_set_material(g, &m);
    for (int i = 0; i < t->nmesh; i++)
        gfx_draw_planar(g, &t->mesh[i], t->detail, m4_identity(), v4(t->detail_gain.x, t->detail_gain.y, t->detail_gain.z, 1), t->detail_tile);
    gfx_set_material(g, NULL);
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
    int n = t->n;
    // Flat water needs no more detail than fits in a handful of triangles; only the coast texture
    // below needs the full resolution, to know exactly where the shore is. So the mesh is strided
    // down to at most TERRAIN_N_DEFAULT (129) vertices a side -- the same 16-bit-index ceiling the
    // ground chunks exist for -- and at t->n == TERRAIN_N_DEFAULT the stride is 1, an unstrided grid
    // identical to before this existed.
    int stride = (n - 1) / (TERRAIN_N_DEFAULT - 1); if (stride < 1) stride = 1;
    int m = (n - 1) / stride + 1;   // vertices a side of the (strided) water grid
    static Vertex *verts = NULL; static Uint16 *idx16 = NULL;
    const int NVMAX = TERRAIN_N_DEFAULT * TERRAIN_N_DEFAULT + 8, NIMAX = (TERRAIN_N_DEFAULT - 1) * (TERRAIN_N_DEFAULT - 1) * 6 + 4 * 6;
    if (!verts) { verts = malloc(sizeof(Vertex) * NVMAX); idx16 = malloc(sizeof(Uint16) * NIMAX); }
    if (!verts || !idx16) return;
    int nv = m * m + 8;
    float y = t->water;
    for (int Z = 0; Z < m; Z++) for (int X = 0; X < m; X++) {
        int x = X * stride, z = Z * stride;
        Vertex *v = &verts[Z * m + X];
        v->pos[0] = t->origin.x + x * t->cell; v->pos[1] = y; v->pos[2] = t->origin.z + z * t->cell;
        v->normal[0] = 0; v->normal[1] = 1; v->normal[2] = 0;
        v->uv[0] = v->uv[1] = 0;
        v->color[0] = v->color[1] = v->color[2] = v->color[3] = 1;
    }
    float span = (n - 1) * t->cell;
    float x0 = t->origin.x, z0 = t->origin.z, x1 = x0 + span, z1 = z0 + span;
    float ox0 = x0 - span * WATER_SKIRT, oz0 = z0 - span * WATER_SKIRT, ox1 = x1 + span * WATER_SKIRT, oz1 = z1 + span * WATER_SKIRT;
    const float corner[8][2] = { { x0, z0 }, { x1, z0 }, { x1, z1 }, { x0, z1 },        // grid corners, clockwise from -x -z
                                 { ox0, oz0 }, { ox1, oz0 }, { ox1, oz1 }, { ox0, oz1 } };
    for (int i = 0; i < 8; i++) {
        Vertex *v = &verts[m * m + i];
        v->pos[0] = corner[i][0]; v->pos[1] = y; v->pos[2] = corner[i][1];
        v->normal[0] = 0; v->normal[1] = 1; v->normal[2] = 0;
        v->uv[0] = v->uv[1] = 0;
        v->color[0] = v->color[1] = v->color[2] = v->color[3] = 1;
    }
    Uint32 ni = 0;
    for (int Z = 0; Z < m - 1; Z++) for (int X = 0; X < m - 1; X++) {
        Uint16 a = (Uint16)(Z * m + X), b = (Uint16)(Z * m + X + 1), c = (Uint16)((Z + 1) * m + X), d = (Uint16)((Z + 1) * m + X + 1);
        idx16[ni++] = a; idx16[ni++] = c; idx16[ni++] = b;
        idx16[ni++] = b; idx16[ni++] = c; idx16[ni++] = d;
    }
    for (int i = 0; i < 4; i++) {   // frame: each side runs from one grid corner to the next, out to the matching outer pair
        Uint16 ia = (Uint16)(m * m + i), ib = (Uint16)(m * m + (i + 1) % 4);
        Uint16 oa = (Uint16)(m * m + 4 + i), ob = (Uint16)(m * m + 4 + (i + 1) % 4);
        idx16[ni++] = ia; idx16[ni++] = ib; idx16[ni++] = ob;
        idx16[ni++] = ia; idx16[ni++] = ob; idx16[ni++] = oa;
    }
    if (t->water_ok) gfx_mesh_destroy(g, &t->water_mesh);
    t->water_mesh = gfx_mesh_create(g, verts, (Uint32)nv, idx16, ni);
    t->water_ok = true; t->water_dirty = false; t->water_built = t->water;

    static unsigned char *px = NULL;
    if (!px) px = malloc((size_t)TERRAIN_N * TERRAIN_N * 4);
    if (px) {
        for (int i = 0; i < n * n; i++) {
            float depth = fmaxf(y - (t->height[i] + t->origin.y), 0.0f);
            px[i * 4 + 0] = (unsigned char)(clampf(depth / WATER_SHELF, 0, 1) * 255.0f);
            px[i * 4 + 1] = (unsigned char)((1 - clampf(depth / WATER_FOAM, 0, 1)) * 255.0f);
            px[i * 4 + 2] = 0; px[i * 4 + 3] = 255;
        }
        if (t->water_tex_ok) gfx_texture_destroy(g, &t->water_tex);
        t->water_tex = gfx_texture_create(g, px, n, n);
        t->water_tex_ok = true;
    }
}

void terrain_draw_water(Gfx *g, Terrain *t) {
    if (!t->present || t->water < -900) return;
    terrain_update_water(g, t);
    if (!t->water_ok || !t->water_tex_ok) return;
    // The material carries where the coast texture sits in the world: xy is the grid's origin in xz
    // and z one over its span, so the shader can find the right texel from a world position.
    float span = (t->n - 1) * t->cell;
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
    if (x0 < 0) x0 = 0; if (z0 < 0) z0 = 0; if (x1 > t->n - 1) x1 = t->n - 1; if (z1 > t->n - 1) z1 = t->n - 1;
    static float tmp[TERRAIN_N * TERRAIN_N];
    if (brush == TB_SMOOTH) memcpy(tmp, t->height, sizeof tmp);
    for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
        float wx = t->origin.x + x * t->cell, wz = t->origin.z + z * t->cell;
        float d = hypotf(wx - at.x, wz - at.z); if (d > radius) continue;
        float k = falloff(d, radius) * dt;
        int i = idx(t, x, z);
        switch (brush) {
        case TB_RAISE: t->height[i] += strength * k; break;
        case TB_LOWER: t->height[i] -= strength * k; break;
        case TB_FLATTEN: t->height[i] = lerpf(t->height[i], flatten_to - t->origin.y, clampf(k * 3, 0, 1)); break;
        case TB_SMOOTH: {
            float sum = 0; int cnt = 0;
            for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) { int xx = x + dx, zz = z + dz; if (xx < 0 || zz < 0 || xx >= t->n || zz >= t->n) continue; sum += tmp[idx(t, xx, zz)]; cnt++; }
            t->height[i] = lerpf(t->height[i], sum / cnt, clampf(k * 4, 0, 1));
        } break;
        case TB_PAINT: t->color[i] = v3_lerp(t->color[i], paint_color, clampf(k * 5, 0, 1)); break;
        }
    }
    t->mesh_dirty = true;
}

void terrain_auto_biome(Terrain *t, float snow_h, float rock_slope, Vec3 grass, Vec3 rock, Vec3 snow, Vec3 dirt) {
    for (int z = 0; z < t->n; z++) for (int x = 0; x < t->n; x++) {
        float wx = t->origin.x + x * t->cell, wz = t->origin.z + z * t->cell;
        float h = t->height[idx(t, x, z)];
        float slope = 1.0f - terrain_normal(t, wx, wz).y;       // 0 flat .. 1 vertical
        Vec3 c = grass;
        c = v3_lerp(c, dirt, clampf((0.10f - h) / 0.5f, 0, 0.6f));   // low ground darker/dirtier
        c = v3_lerp(c, rock, clampf((slope - rock_slope * 0.6f) / (rock_slope * 0.6f), 0, 1));
        c = v3_lerp(c, snow, clampf((h - snow_h) / 6.0f, 0, 1) * (1 - clampf((slope - rock_slope) / 0.2f, 0, 0.7f)));
        t->color[idx(t, x, z)] = c;
    }
    t->mesh_dirty = true;
}

void terrain_generate_mountains(Terrain *t, float peak) {
    for (int z = 0; z < t->n; z++) for (int x = 0; x < t->n; x++) {
        float u = (float)x / (t->n - 1), v = (float)z / (t->n - 1);
        float h = 0;
        h += sinf(u * 6.3f + 1.3f) * cosf(v * 5.1f + 0.4f) * 6;
        h += sinf(u * 13.0f + 2.1f) * sinf(v * 11.0f + 1.7f) * 3;
        h += sinf(u * 27.0f) * sinf(v * 23.0f + 0.9f) * 1.2f;
        float edge = fminf(fminf(u, 1 - u), fminf(v, 1 - v));
        float rim = 1 - clampf(edge / 0.22f, 0, 1); rim = rim * rim;
        h += rim * peak + sinf(u * 41.0f + v * 17.0f) * rim * 2.5f;
        float centre = hypotf(u - 0.5f, v - 0.5f);
        h *= clampf((centre - 0.06f) / 0.14f, 0, 1);   // flat middle for the path and arena
        t->height[idx(t, x, z)] = fmaxf(h, 0);
    }
    t->mesh_dirty = true;
}

void terrain_generate(Terrain *t, const TerrainGen *p, Vec3 grass, Vec3 rock, Vec3 snow, Vec3 dirt, Vec3 sand) {
    unsigned seed = p->seed ? p->seed : 1;
    int n = t->n;
    float span = (n - 1) * t->cell;
    for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
        float u = (float)x / (n - 1), v = (float)z / (n - 1);
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
        t->height[idx(t, x, z)] = h;
    }
    (void)span;
    t->water = p->water_h;
    t->mesh_dirty = true;
    // colours: biomes from height, slope and a moisture noise
    for (int z = 0; z < n; z++) for (int x = 0; x < n; x++) {
        float wx = t->origin.x + x * t->cell, wz = t->origin.z + z * t->cell;
        float h = t->height[idx(t, x, z)];
        float slope = 1.0f - terrain_normal(t, wx, wz).y;
        float moist = terrain_noise(seed, wx, wz, 30.0f);
        Vec3 c = v3_lerp(grass, v3_scale(grass, 0.75f), moist);           // wetter = darker grass
        c = v3_lerp(c, dirt, clampf(0.5f - moist, 0, 0.5f) * 0.6f);
        c = v3_lerp(c, sand, clampf((p->water_h + 1.2f - h) / 1.2f, 0, 1));   // shore
        c = v3_lerp(c, rock, clampf((slope - 0.28f) / 0.25f, 0, 1));
        c = v3_lerp(c, snow, clampf((h - p->snow_h) / 5.0f, 0, 1) * (1 - clampf((slope - 0.45f) / 0.2f, 0, 0.7f)));
        t->color[idx(t, x, z)] = c;
    }
}

void terrain_flatten_pad(Terrain *t, Vec3 at, float radius, float height) {
    int x0 = (int)floorf((at.x - radius * 2 - t->origin.x) / t->cell), x1 = (int)ceilf((at.x + radius * 2 - t->origin.x) / t->cell);
    int z0 = (int)floorf((at.z - radius * 2 - t->origin.z) / t->cell), z1 = (int)ceilf((at.z + radius * 2 - t->origin.z) / t->cell);
    if (x0 < 0) x0 = 0; if (z0 < 0) z0 = 0; if (x1 > t->n - 1) x1 = t->n - 1; if (z1 > t->n - 1) z1 = t->n - 1;
    for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
        float wx = t->origin.x + x * t->cell, wz = t->origin.z + z * t->cell;
        float d = hypotf(wx - at.x, wz - at.z);
        float w = 1 - clampf((d - radius) / (radius + 0.5f), 0, 1); w = w * w * (3 - 2 * w);
        t->height[idx(t, x, z)] = lerpf(t->height[idx(t, x, z)], height - t->origin.y, w);
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
        if (x0 < 0) x0 = 0; if (z0 < 0) z0 = 0; if (x1 > t->n - 1) x1 = t->n - 1; if (z1 > t->n - 1) z1 = t->n - 1;
        for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
            float wx = t->origin.x + x * t->cell, wz = t->origin.z + z * t->cell;
            float d = hypotf(wx - q.x, wz - q.z); if (d > width) continue;
            float w = 1 - clampf((d - width * 0.5f) / (width * 0.5f), 0, 1);
            t->height[idx(t, x, z)] = lerpf(t->height[idx(t, x, z)], hc - t->origin.y, 0.5f * w);
            t->color[idx(t, x, z)] = v3_lerp(t->color[idx(t, x, z)], color, 0.8f * w);
        }
    }
    t->mesh_dirty = true;
}
