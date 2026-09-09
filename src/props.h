// Static prop models placed by the level, loaded once per file and drawn in rest pose.
#pragma once
#include "model.h"
#include "level.h"
#include "part.h"

#define PROPS_MAX_MODELS 96

// bcen/brad: rest-pose bounding sphere, taken from the bounds once per file and reused every frame.
typedef struct PropModel { char file[128]; Model model; ModelPose rest; bool ok; PartDoc *part; long long mtime;   // part: an assembly instead of a model
                           Vec3 bcen; float brad; int bsphere; } PropModel;                       // bsphere: 0 not computed yet, 1 valid, -1 no bounds
typedef struct PropCache { PropModel models[PROPS_MAX_MODELS]; int n;
                           unsigned props_drawn, props_culled; } PropCache;   // last props_draw, for the debugger

void props_clear(Gfx *g, PropCache *pc);
// Reload any loaded model or part file that changed on disk (checked about once a second). Returns how many.
int  props_hot_reload(Gfx *g, PropCache *pc);
// Load every prop the level references (cached by file). Missing files log and are skipped.
void props_load_level(Gfx *g, PropCache *pc, const Level *lv);
void props_draw(Gfx *g, PropCache *pc, const Level *lv, float time);
// Draw one piece by file (loaded on demand). Used for editor ghosts.
void props_draw_one(Gfx *g, PropCache *pc, const char *file, Vec3 pos, float yaw, float scale, Vec4 tint, Vec3 glow);
// Draw with a full matrix (assemblies, stretched pieces). depth limits nested parts.
void props_draw_matrix(Gfx *g, PropCache *pc, const char *file, Mat4 world, Vec4 tint, Vec3 glow, int depth);
// Rest-pose bounds of a piece (loads on demand); false if missing.
bool props_bounds(Gfx *g, PropCache *pc, const char *file, Vec3 *bmin, Vec3 *bmax);
