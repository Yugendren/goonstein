// Binds a Character's gameplay animation state to a skinned model via a text config.
// Config format (assets/characters/*.txt):
//   model  PATH                relative to assets/ (skinned glTF)
//   sprite PATH                relative to assets/ (sprite sheet config, see sprite.h); either model or sprite
//   scale  S
//   yaw_offset DEG            rotate the model so its front matches yaw 0 (+Z)
//   texture_size N             downsample embedded textures to at most N
//   hide   NODE [NODE ...]     accessory nodes to hide
//   borrow FILE NODE           draw mesh node NODE from another rigged FILE (relative to assets/)
//       on this character's skeleton. The KayKit rigs share joint names and inverse binds, so a
//       Mage hat rides a Knight. Hiding the part it replaces is up to you (hide Knight_Helmet).
//   anim   NAME CLIP [loop] [hold] [contact F] [rate R]
//       NAME is one of the Anim enum names (idle walk attack parry parry_hit dodge hurt kneel dead roar stagger)
// Boss moves name their own clips in the enemy file (clip NAME contact F).
#pragma once
#include "model.h"
#include "sprite.h"
#include "combat.h"
#include "part.h"

typedef struct AnimBinding { int clip; bool loop, hold; float contact, rate; } AnimBinding;

// A character file in memory: what the builder edits and what charmodel_apply turns into a model.
#define SPEC_MAX_HIDDEN 32
#define SPEC_MAX_RECOLOR 32
#define SPEC_MAX_ATTACH 16
#define SPEC_MAX_BORROW 16
#define CHAR_MAX_BORROW_FILES 8
typedef struct CharSpec {
    char model[256], sprite[256];        // one of the two is set (relative to assets/)
    float scale, yaw_offset_deg; int tex_size;
    char hidden[SPEC_MAX_HIDDEN][48]; int nhidden;
    unsigned char rc_from[SPEC_MAX_RECOLOR][3], rc_to[SPEC_MAX_RECOLOR][3]; int nrecolor;
    struct { char clip[64]; bool set, loop, hold; float contact, rate; } anims[ANIM_COUNT];
    // rigid parts (your own OBJ or any model) fixed to a bone: attach FILE BONE x y z yaw pitch roll scale
    struct { char file[128], bone[48]; Vec3 pos; float yaw, pitch, roll, scale; } attach[SPEC_MAX_ATTACH]; int nattach;
    // mesh parts taken from another rigged file and drawn on this skeleton: borrow FILE NODE
    struct { char file[128], node[48]; } borrow[SPEC_MAX_BORROW]; int nborrow;
} CharSpec;


typedef struct CharModel {
    Model model; bool loaded;
    bool is_sprite; SpriteDef sdef; SpriteActor sprite;
    AnimPlayer player; ModelPose pose;
    AnimBinding bind[ANIM_COUNT];
    float scale, yaw_offset;
    CharSpec spec;                       // what this character was built from
    struct { Model model; ModelPose rest; Mat4 local; Vec4 tint; int attach; } sub[32]; int nsub;   // loaded attachment models (a .part expands into its pieces)
    // borrowed parts: each source file loaded once, with its own texture; drawn with the base pose
    struct { Model model; char file[128]; } lent[CHAR_MAX_BORROW_FILES]; int nlent;
    int borrow_lent[SPEC_MAX_BORROW];    // spec.borrow[i] -> lent[] index, -1 if it failed to load
    // change detection
    Anim last_anim; float last_anim_t; int last_move;
} CharModel;

bool charmodel_load(Gfx *g, CharModel *cm, const char *config_path);
bool charmodel_spec_load(CharSpec *sp, const char *config_path);
bool charmodel_spec_save(const CharSpec *sp, const char *config_path);
// Build (or rebuild) the model from a spec. On failure cm is left unloaded.
bool charmodel_apply(Gfx *g, CharModel *cm, const CharSpec *sp);
// Fill the anim table by matching the model's clip names against the usual KayKit / Quaternius names.
// style: 0 one-handed, 1 two-handed, 2 spellcaster, 3 unarmed. Returns how many actions were bound.
int  charmodel_spec_autobind(CharSpec *sp, const Model *m, int style);
void charmodel_destroy(Gfx *g, CharModel *cm);
// Timing of the clip bound to an action: seconds from the start of the clip to its contact frame,
// and the clip's duration. False if the action has no clip, no contact mark, or this is a sprite.
// The fight uses it to time a swing off the animation instead of a fixed number.
bool charmodel_clip_timing(CharModel *cm, Anim a, float *contact, float *duration);
// Advance animation for a player-controlled character (uses PlayerDef timings for fitted clips).
void charmodel_drive_player(CharModel *cm, const Player *p, float dt);
// Advance animation for a boss (uses its move clips and timings).
void charmodel_drive_boss(CharModel *cm, const Boss *b, float dt);
// Idle / walk / scripted characters (NPCs): plays the bound clip for c->anim and advances it.
void charmodel_drive_simple(CharModel *cm, const Character *c, float dt);
void charmodel_draw(Gfx *g, CharModel *cm, const Character *c, Vec4 tint);
// Draw a 3D character with an explicit pose and world matrix (portraits); attachments included.
void charmodel_draw_posed(Gfx *g, const CharModel *cm, const ModelPose *pose, Mat4 world, Vec4 tint);
// Sprite helpers used by the battle: play a named sprite animation (fitted so the first contact lands at lead), and contact timing.
void charmodel_sprite_play(CharModel *cm, const char *anim, float lead, bool restart);
float charmodel_sprite_contact(const CharModel *cm, const char *anim, int i, float lead);
void charmodel_sprite_settle(CharModel *cm);   // finished one-shots return to idle
