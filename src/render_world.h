// Drawing the level, the characters (box figures with procedural animation), and textures.
#pragma once
#include "gfx.h"
#include "level.h"
#include "combat.h"

// The seven procedural textures plus one per image in assets/textures/ (see level.h).
typedef struct WorldTextures {
    Texture tex[TEX_COUNT]; Texture user[LEVEL_MAX_USER_TEX]; int nuser;
    Vec3 user_mean[LEVEL_MAX_USER_TEX];   // mean colour of each scanned image, 0..1 per channel
} WorldTextures;

// Texture for a level texture id: below TEX_COUNT procedural, above it a scanned image.
// Out of range (a level saved with a texture whose file has since gone) falls back to flat.
const Texture *world_texture(const WorldTextures *wt, int id);
// 1/mean, per channel: the tint that makes a detail map multiply to the colour it covers.
// Clamped so a near-black or near-white source image does not blow the gain up; (1,1,1) for a
// procedural texture or an id out of range.
Vec3 world_texture_gain(const WorldTextures *wt, int id);
// Id of a scanned texture (assets/textures/NAME.*) by name, or -1.
int world_texture_find(const WorldTextures *wt, const char *name);

void world_textures_create(Gfx *g, WorldTextures *wt);
void world_textures_destroy(Gfx *g, WorldTextures *wt);
void draw_level(Gfx *g, const Level *lv, const WorldTextures *wt);
// Soft shadow disc under a character.
void draw_blob_shadow(Gfx *g, Vec3 pos, float radius, float strength);
// is_boss picks the hulking proportions and the boss-specific poses.
void draw_character(Gfx *g, const Character *c, Vec3 base_color, Vec3 size, bool is_boss, const Texture *skin);
