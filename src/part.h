// Parts: assemblies. A part is a list of pieces (any model file: kit, imports, shapes, or another
// part) with a position, size, rotation and optional tint, grouped in the world editor from
// placed pieces. Saved as text in assets/models/own/NAME.part; used as a prop or a character part.
//
//   piece FILE  x y z  sx sy sz  yaw pitch roll  [r g b]
#pragma once
#include "gfx.h"

#define PART_MAX_PIECES 64
typedef struct Piece {
    char file[128];
    Vec3 pos, size; float yaw, pitch, roll;   // degrees
    Vec4 tint;                                // 1 1 1 = untinted
} Piece;
typedef struct PartDoc { Piece pieces[PART_MAX_PIECES]; int n; } PartDoc;

bool part_load(PartDoc *d, const char *path);
bool part_save(const PartDoc *d, const char *path);
// Local matrix of a piece (translate, rotate, scale).
Mat4 piece_matrix(const Piece *p);
