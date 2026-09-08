// Static prop models placed by the level, loaded once per file and drawn in rest pose.
#pragma once
#include "model.h"
#include "level.h"

#define PROPS_MAX_MODELS 96

typedef struct PropModel { char file[128]; Model model; ModelPose rest; bool ok; } PropModel;
typedef struct PropCache { PropModel models[PROPS_MAX_MODELS]; int n; } PropCache;

void props_clear(Gfx *g, PropCache *pc);
// Load every prop the level references (cached by file). Missing files log and are skipped.
void props_load_level(Gfx *g, PropCache *pc, const Level *lv);
void props_draw(Gfx *g, PropCache *pc, const Level *lv, float time);
