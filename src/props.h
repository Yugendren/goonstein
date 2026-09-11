// Static prop models placed by the level, loaded once per file and drawn in rest pose.
#pragma once
#include "model.h"
#include "level.h"
#include "part.h"

struct WorldTextures;   // render_world.h; only ever a pointer here

// The island already names 108 distinct prop files, and every item model and weapon comes out of
// the same cache. Running out used to be silent -- the model simply never drew -- so this is now
// generous and load_one says so when it fills up.
#define PROPS_MAX_MODELS 192

// bcen/brad: rest-pose bounding sphere, taken from the bounds once per file and reused every frame.
// lod: a decimated stand-in loaded from "<file>.lod.glb" if that file exists (tools/make_lods.sh
// makes them). It is drawn instead of the real model for every shadow-map instance -- a shadow is a
// silhouette and does not care -- and for anything small enough on screen, which on the island is
// most of the scatter most of the time. The real model is still what you see up close, so nothing
// about the near look changes.
typedef struct PropModel { char file[128]; Model model; ModelPose rest; bool ok; PartDoc *part; long long mtime;   // part: an assembly instead of a model
                           Model lod; ModelPose lod_rest; bool lod_ok;
                           Vec3 bcen; float brad; int bsphere;                                    // bsphere: 0 not computed yet, 1 valid, -1 no bounds
                           int fallback;                                                          // fallback: 0 not asked yet, 1 has a skinned mesh somewhere in it, -1 does not
                           // A .part's pieces are named by file and posed by three angles in
                           // DEGREES. Resolving the name is a strcmp against every loaded file and
                           // building the matrix is six trig calls, and both answers are the same
                           // every frame for the life of the file. A hundred palms of thirty
                           // pieces, twice a frame (sun and camera), made that 41000 sines. Cached.
                           struct PropModel *piece_pm[PART_MAX_PIECES]; Mat4 piece_mat[PART_MAX_PIECES]; bool piece_cached;
                           } PropModel;
// wt: the texture set the last props_draw was given. A piece's own `tex NAME` is a level texture
// id, and the ghost / debris callers of props_draw_matrix have no texture set to resolve it with,
// so the cache keeps the one the world pass used.
// fb: props that could not be instanced and must still be drawn one at a time inside each pass.
// A prop qualifies only by having a skinned mesh in it -- a rigged model placed as scenery -- since
// skinning needs a joint matrix palette per draw and there is nowhere to put one per instance. The
// island has none, so this list is normally empty; it exists so that a level that does have one
// keeps drawing rather than quietly losing the prop.
typedef struct PropCache { PropModel models[PROPS_MAX_MODELS]; int n;
                           unsigned props_drawn, props_culled;   // last props_collect of the camera set, for the debugger
                           int fb[GFX_SET_COUNT][LEVEL_MAX_PROPS]; int nfb[GFX_SET_COUNT];
                           // Which PropModel each of the level's props resolves to, worked out once
                           // per level rather than once per prop per pass, for the same reason.
                           // Dropped when the level or any model file is reloaded.
                           PropModel *by_prop[LEVEL_MAX_PROPS]; int by_prop_n; const Level *by_prop_lv; long long by_prop_stamp;
                           const struct WorldTextures *wt; } PropCache;

void props_clear(Gfx *g, PropCache *pc);
// Reload any loaded model or part file that changed on disk (checked about once a second). Returns how many.
int  props_hot_reload(Gfx *g, PropCache *pc);
// Load every prop the level references (cached by file). Missing files log and are skipped.
void props_load_level(Gfx *g, PropCache *pc, const Level *lv);
// Build one of this frame's instance lists from the level's props, culled against `view_proj`.
// Nothing is drawn and no GPU call is made: this runs before any render pass opens, which is where
// gfx_instances_upload needs it (see the instancing block in gfx.h). `cam_pos` is the eye the
// few-pixels size test measures from; for GFX_SET_SHADOW there is no size test, because the sun's
// box is already fitted to what the camera looks at and a prop's distance from the eye says
// nothing about how big its shadow is.
// Both sets are built in ONE walk over the level: a prop that the sun and the camera can both see
// has its assembly expanded once and its instances queued twice, which is half the matrix work of
// walking the level twice. want_shadow false skips the sun set entirely (night, or shadows off).
void props_collect(Gfx *g, PropCache *pc, const Level *lv, const struct WorldTextures *wt,
                   Mat4 shadow_vp, bool want_shadow, Mat4 camera_vp, Vec3 cam_pos);
// Draw the handful of props props_collect could not instance. Call inside the matching render pass.
void props_draw_fallback(Gfx *g, PropCache *pc, const Level *lv, const struct WorldTextures *wt, GfxInstSet set);
// Draw one piece by file (loaded on demand). Used for editor ghosts.
void props_draw_one(Gfx *g, PropCache *pc, const char *file, Vec3 pos, float yaw, float scale, Vec4 tint, Vec3 glow);
// Draw with a full matrix (assemblies, stretched pieces). depth limits nested parts.
// tex, if given, replaces every rigid mesh's own texture, projected in world space at `tile`
// repeats per metre; a piece that names its own `tex` overrides it.
void props_draw_matrix(Gfx *g, PropCache *pc, const char *file, Mat4 world, Vec4 tint, Vec3 glow, const Texture *tex, float tile, int depth);
// Rest-pose bounds of a piece (loads on demand); false if missing.
bool props_bounds(Gfx *g, PropCache *pc, const char *file, Vec3 *bmin, Vec3 *bmax);
