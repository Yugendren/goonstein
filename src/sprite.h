// Sprite sheets and frame animation for pixel-art actors drawn as upright billboards.
// Config format (assets/sprites/*.txt):
//   size H                       world height in metres of one frame
//   frame W H                    frame size in pixels for sheets that do not override it
//   sheet NAME FILE COLS ROWS [foot PX]   FILE relative to assets/; COLS x ROWS frames; foot = pixels from the
//                                        bottom of a frame to the character's feet (default 0)
//   anim NAME SHEET [key value ...]
//        fps F        frames per second (default 8)
//        loop         loops (default: plays once and holds the last frame)
//        dir          rows are facings in the order down, up, left, right (or two rows: left, right)
//        dircol       columns are facings (down, up, left, right) and frames run down the rows
//        right SHEET  a separate sheet used when facing right (for left/right boss sheets)
//        contact I    frame index where the attack lands; repeat for multi-hit
//        row R        use only this row of the sheet (non-directional)
//        frames A B   use frames A..B inclusive of the row
#pragma once
#include "gfx.h"

#define SPRITE_MAX_SHEETS 24
#define SPRITE_MAX_ANIMS  24
#define SPRITE_MAX_CONTACT 6

typedef enum Facing { FACE_DOWN, FACE_UP, FACE_LEFT, FACE_RIGHT } Facing;

typedef struct SpriteSheet { char name[32]; Texture tex; int cols, rows, fw, fh, foot; } SpriteSheet;

typedef struct SpriteAnim {
    char name[32];
    int sheet, sheet_right;          // sheet_right -1 if none
    float fps; bool loop, directional, dir_cols;
    int row;                         // fixed row (or -1 = pick by facing)
    int first, last;                 // frame range within the row
    int contact[SPRITE_MAX_CONTACT]; int ncontact;
} SpriteAnim;

typedef struct SpriteDef {
    SpriteSheet sheets[SPRITE_MAX_SHEETS]; int nsheets;
    SpriteAnim anims[SPRITE_MAX_ANIMS]; int nanims;
    float size;                      // world height of a frame
    int frame_w, frame_h;
} SpriteDef;

typedef struct SpriteActor {
    const SpriteDef *def;
    int anim; float time, rate;      // rate multiplies fps
    Facing facing;
    bool finished;
} SpriteActor;

bool sprite_def_load(Gfx *g, SpriteDef *d, const char *path);
void sprite_def_destroy(Gfx *g, SpriteDef *d);
int  sprite_find_anim(const SpriteDef *d, const char *name);   // -1 if missing

void sprite_actor_init(SpriteActor *a, const SpriteDef *d);
// Start an animation by index; rate 1 = authored fps. Restarts if already playing when restart is true.
void sprite_play(SpriteActor *a, int anim, float rate, bool restart);
// Start an attack so that its first contact frame lands after `lead` seconds (rate is derived).
void sprite_play_fitted(SpriteActor *a, int anim, float lead);
void sprite_update(SpriteActor *a, float dt);
// Seconds from the start of `anim` (at the given rate) to contact number i.
float sprite_contact_time(const SpriteDef *d, int anim, int i, float rate);
float sprite_anim_duration(const SpriteDef *d, int anim, float rate);
// Draw at foot position. Facing left/right sheets are picked by actor facing.
void sprite_actor_draw(Gfx *g, const SpriteActor *a, Vec3 foot, Vec4 tint, float scale);
// Choose a facing from a world direction as seen by the camera.
Facing sprite_facing_from(Vec3 dir, Vec3 cam_forward, Vec3 cam_right);
