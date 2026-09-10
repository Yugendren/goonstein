// Heightmap terrain: a square grid of heights and vertex colours, sculpted and painted by the
// environment editor, rendered as one lit mesh, walked on by characters.
#pragma once
#include "gfx.h"
#include <stdbool.h>

#define TERRAIN_N 129            // vertices per side (128 cells)

typedef struct Terrain {
    float cell;                  // metres per cell
    Vec3  origin;                // world position of vertex (0,0); the grid extends +x and +z
    float height[TERRAIN_N * TERRAIN_N];
    Vec3  color[TERRAIN_N * TERRAIN_N];
    Mesh  mesh; bool mesh_ok; bool mesh_dirty;
    Mesh  water_mesh; bool water_ok; bool water_dirty; float water_built;   // sea surface; water_built = the height it was built for
    Texture water_tex; bool water_tex_ok;   // per-cell water depth and foam mask, sampled by the sea shader
    bool  present;               // false = the level has no terrain (flat ground blocks only)
    float water;                 // water surface height (metres); below -900 = no water
    char  file[128];             // base path relative to assets/, e.g. levels/glade_terrain
    // Ground detail map (optional): a photoscan multiplied into the biome colours with world-space
    // planar UVs, so the flat-white/vertex-colour ground picks up grain. NULL = none (see terrain_draw).
    const Texture *detail; float detail_tile; Vec3 detail_gain;
} Terrain;

void  terrain_init(Terrain *t, float cell, Vec3 origin, float base_height, Vec3 base_color);
void  terrain_destroy(Gfx *g, Terrain *t);
// A ground detail map, multiplied into the biome colours with world-space planar UVs.
// `gain` should be 1/mean so the map adds grain without darkening the biome palette.
// tex = NULL clears it. Call after terrain_load/terrain_init/terrain_generate: they reset the
// terrain wholesale and would otherwise clobber this.
void  terrain_set_detail(Terrain *t, const Texture *tex, float tile, Vec3 gain);
// Height and normal at a world position (bilinear); outside the grid returns the edge height.
float terrain_height(const Terrain *t, float x, float z);
Vec3  terrain_normal(const Terrain *t, float x, float z);
bool  terrain_inside(const Terrain *t, float x, float z);
// Ray from a to b (world) against the surface; true with the hit point.
bool  terrain_ray(const Terrain *t, Vec3 a, Vec3 b, Vec3 *hit);
// Rebuild the GPU mesh if dirty (call once per frame from the render side).
void  terrain_update_mesh(Gfx *g, Terrain *t);
void  terrain_draw(Gfx *g, Terrain *t);
// The sea: a flat grid at the water height, ringed by a skirt that carries the surface out past any
// far plane so the horizon is fog rather than an edge, plus a small texture of how deep the water is
// over each cell. The shader shades the coast from that texture, sampled by world position, which is
// why the material carries the grid's origin and size (see gfx.h's Material.water).
void  terrain_update_water(Gfx *g, Terrain *t);
void  terrain_draw_water(Gfx *g, Terrain *t);

// Brushes. radius/strength in metres and metres-per-second-ish units; dt scales the effect.
typedef enum TerrainBrush { TB_RAISE, TB_LOWER, TB_SMOOTH, TB_FLATTEN, TB_PAINT } TerrainBrush;
void  terrain_brush(Terrain *t, TerrainBrush brush, Vec3 at, float radius, float strength, float dt, Vec3 paint_color, float flatten_to);
// Paint every vertex by height and slope: snow above snow_h, rock where slope is steep, grass else,
// blended softly. Colours are the caller's biome palette.
// Fill the heights with a ring of mountains around a flat middle (a starting point for sculpting).
void  terrain_generate_mountains(Terrain *t, float peak);
void  terrain_auto_biome(Terrain *t, float snow_h, float rock_slope, Vec3 grass, Vec3 rock, Vec3 snow, Vec3 dirt);

// Generator: seed-driven noise landmass. mountains/hills 0..1 scale the relief, roughness adds
// small detail, snow_h is the snow line, water_h the lake level, flat_r keeps a flat pad at each
// `flat` point (spawn, arena). Colours are the biome palette.
typedef struct TerrainGen { unsigned seed; float mountains, hills, roughness, snow_h, water_h; Vec3 flat[4]; float flat_r[4]; int nflat; } TerrainGen;
void  terrain_generate(Terrain *t, const TerrainGen *p, Vec3 grass, Vec3 rock, Vec3 snow, Vec3 dirt, Vec3 sand);
// Moisture-like noise 0..1 at a world position for the given seed (forest density, biome variety).
float terrain_noise(unsigned seed, float x, float z, float scale);
// Flatten a soft-edged pad to `height` (instant). Path: flatten and paint along a segment.
void  terrain_flatten_pad(Terrain *t, Vec3 at, float radius, float height);
void  terrain_path(Terrain *t, Vec3 a, Vec3 b, float width, Vec3 color);
// Any 8- or 16-bit greyscale/colour PNG becomes the heights (resampled), spanning `range` metres.
bool  terrain_import_heightmap(Terrain *t, const char *png_path, float range);

// File layer (terrain_io.c): heights as 16-bit PNG (<file>_h.png) and colours as 8-bit PNG (<file>_c.png).
bool  terrain_save(const Terrain *t, const char *asset_dir);
bool  terrain_load(Terrain *t, const char *asset_dir, const char *file);   // sets present/file on success
