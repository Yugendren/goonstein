// Binds a Character's gameplay animation state to a skinned model via a text config.
// Config format (assets/characters/*.txt):
//   model  PATH                relative to assets/ (skinned glTF)
//   sprite PATH                relative to assets/ (sprite sheet config, see sprite.h); either model or sprite
//   scale  S
//   yaw_offset DEG            rotate the model so its front matches yaw 0 (+Z)
//   texture_size N             downsample embedded textures to at most N
//   hide   NODE [NODE ...]     accessory nodes to hide
//   anim   NAME CLIP [loop] [hold] [contact F] [rate R]
//       NAME is one of the Anim enum names (idle walk attack parry parry_hit dodge hurt kneel dead roar stagger)
// Boss moves name their own clips in the enemy file (clip NAME contact F).
#pragma once
#include "model.h"
#include "sprite.h"
#include "combat.h"

typedef struct AnimBinding { int clip; bool loop, hold; float contact, rate; } AnimBinding;

typedef struct CharModel {
    Model model; bool loaded;
    bool is_sprite; SpriteDef sdef; SpriteActor sprite;
    AnimPlayer player; ModelPose pose;
    AnimBinding bind[ANIM_COUNT];
    float scale, yaw_offset;
    // change detection
    Anim last_anim; float last_anim_t; int last_move;
} CharModel;

bool charmodel_load(Gfx *g, CharModel *cm, const char *config_path);
void charmodel_destroy(Gfx *g, CharModel *cm);
// Advance animation for a player-controlled character (uses PlayerDef timings for fitted clips).
void charmodel_drive_player(CharModel *cm, const Player *p, float dt);
// Advance animation for a boss (uses its move clips and timings).
void charmodel_drive_boss(CharModel *cm, const Boss *b, float dt);
void charmodel_draw(Gfx *g, CharModel *cm, const Character *c, Vec4 tint);
// Sprite helpers used by the battle: play a named sprite animation (fitted so the first contact lands at lead), and contact timing.
void charmodel_sprite_play(CharModel *cm, const char *anim, float lead, bool restart);
float charmodel_sprite_contact(const CharModel *cm, const char *anim, int i, float lead);
void charmodel_sprite_settle(CharModel *cm);   // finished one-shots return to idle
