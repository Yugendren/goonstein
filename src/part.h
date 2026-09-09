// Parts: the simplest possible CAD. A part is a list of shapes (boxes, cylinders, spheres,
// wedges, and instances of your OBJ exports), each with a position, size, rotation and colour.
// Saved as text in assets/models/own/NAME.part; loads like any model (prop or character part).
//
//   shape box    x y z  sx sy sz  yaw pitch roll  r g b
//   shape cyl    ...   (sx = diameter, sy = height, sz = diameter)
//   shape sphere ...
//   shape wedge  ...   (a box cut diagonally: a ramp / roof)
//   shape obj FILE x y z  sx sy sz  yaw pitch roll  [r g b]   (your import, optionally recoloured)
#pragma once
#include "gfx.h"

#define PART_MAX_SHAPES 64
typedef enum ShapeKind { SH_BOX, SH_CYL, SH_SPHERE, SH_WEDGE, SH_OBJ, SH_COUNT } ShapeKind;
typedef struct Shape {
    ShapeKind kind; char file[128];
    Vec3 pos, size; float yaw, pitch, roll;   // degrees
    Vec3 color; bool tinted;                  // colour 0..1 (sRGB); obj shapes keep their own colours unless tinted
} Shape;
typedef struct PartDoc { Shape shapes[PART_MAX_SHAPES]; int n; } PartDoc;

bool part_load(PartDoc *d, const char *path);
bool part_save(const PartDoc *d, const char *path);
// Triangle list of the whole part (world units, colours baked). malloc'd; caller frees.
Vertex *part_build(const PartDoc *d, const char *asset_dir, Uint32 *nverts);
struct Model;
bool part_load_model(Gfx *g, struct Model *m, const char *path);
const char *shape_kind_name(ShapeKind k);
