// Drawing the level, the characters (box figures with procedural animation), and textures.
#pragma once
#include "gfx.h"
#include "level.h"
#include "combat.h"

typedef struct WorldTextures { Texture tex[TEX_COUNT]; } WorldTextures;

void world_textures_create(Gfx *g, WorldTextures *wt);
void world_textures_destroy(Gfx *g, WorldTextures *wt);
void draw_level(Gfx *g, const Level *lv, const WorldTextures *wt);
// Soft shadow disc under a character.
void draw_blob_shadow(Gfx *g, Vec3 pos, float radius, float strength);
// is_boss picks the hulking proportions and the boss-specific poses.
void draw_character(Gfx *g, const Character *c, Vec3 base_color, Vec3 size, bool is_boss, const Texture *skin);
