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
    bool  present;               // false = the level has no terrain (flat ground blocks only)
    char  file[128];             // base path relative to assets/, e.g. levels/glade_terrain
} Terrain;

void  terrain_init(Terrain *t, float cell, Vec3 origin, float base_height, Vec3 base_color);
void  terrain_destroy(Gfx *g, Terrain *t);
// Height and normal at a world position (bilinear); outside the grid returns the edge height.
float terrain_height(const Terrain *t, float x, float z);
Vec3  terrain_normal(const Terrain *t, float x, float z);
bool  terrain_inside(const Terrain *t, float x, float z);
// Ray from a to b (world) against the surface; true with the hit point.
bool  terrain_ray(const Terrain *t, Vec3 a, Vec3 b, Vec3 *hit);
// Rebuild the GPU mesh if dirty (call once per frame from the render side).
void  terrain_update_mesh(Gfx *g, Terrain *t);
void  terrain_draw(Gfx *g, Terrain *t);

// Brushes. radius/strength in metres and metres-per-second-ish units; dt scales the effect.
typedef enum TerrainBrush { TB_RAISE, TB_LOWER, TB_SMOOTH, TB_FLATTEN, TB_PAINT } TerrainBrush;
void  terrain_brush(Terrain *t, TerrainBrush brush, Vec3 at, float radius, float strength, float dt, Vec3 paint_color, float flatten_to);
// Paint every vertex by height and slope: snow above snow_h, rock where slope is steep, grass else,
// blended softly. Colours are the caller's biome palette.
// Fill the heights with a ring of mountains around a flat middle (a starting point for sculpting).
void  terrain_generate_mountains(Terrain *t, float peak);
void  terrain_auto_biome(Terrain *t, float snow_h, float rock_slope, Vec3 grass, Vec3 rock, Vec3 snow, Vec3 dirt);

// File layer (terrain_io.c): heights as 16-bit PNG (<file>_h.png) and colours as 8-bit PNG (<file>_c.png).
bool  terrain_save(const Terrain *t, const char *asset_dir);
bool  terrain_load(Terrain *t, const char *asset_dir, const char *file);   // sets present/file on success
