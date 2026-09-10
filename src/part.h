// Parts: assemblies. A part is a list of pieces (any model file: kit, imports, shapes, or another
// part) with a position, size, rotation and optional tint, grouped in the world editor from
// placed pieces. Saved as text in assets/models/own/NAME.part; used as a prop or a character part.
//
//   piece FILE  x y z  sx sy sz  yaw pitch roll  [r g b]  [tex NAME [TILE]]
//
// `tex NAME` replaces the piece's own textures with a world texture -- one of the seven procedural
// names (stone tile wood metal flesh plaster flat) or the stem of any image in assets/textures/ --
// projected in world space at TILE repeats per metre (default 1), so the piece's size never
// stretches it. A prop's own `tex` applies to every piece that does not set one.
//
// A part may also declare its own collider, which every prop placed from it gets unless the level
// line overrides it -- a hundred palms then need no collider lines at all:
//
//   collide R [H] [deck]     half-width, height (default 5), `deck` = a walkable top, no wall
#pragma once
#include "gfx.h"

#define PART_MAX_PIECES 64
typedef struct Piece {
    char file[128];
    Vec3 pos, size; float yaw, pitch, roll;   // degrees
    Vec4 tint;                                // 1 1 1 = untinted
    int  tex;                                 // `tex NAME`: -1 = the model's own textures
    float tex_tile;                           // its repeats per metre of world space (default 1)
} Piece;
typedef struct PartDoc { Piece pieces[PART_MAX_PIECES]; int n;
                        float collide, collide_h; bool collide_deck; } PartDoc;   // the part's own collider, scaled by the prop that places it (0 = none)

bool part_load(PartDoc *d, const char *path);
bool part_save(const PartDoc *d, const char *path);
// Local matrix of a piece (translate, rotate, scale).
Mat4 piece_matrix(const Piece *p);
