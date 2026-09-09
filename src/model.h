// Skinned models from glTF (.glb): node hierarchy, one skin, animation clips, embedded textures.
// Rigid meshes parented to bones (helmets, weapons) follow the animated node transforms.
#pragma once
#include "gfx.h"

#define MODEL_MAX_NODES   128
#define MODEL_MAX_MESHES  48
#define MODEL_MAX_JOINTS  64
#define MODEL_MAX_TEX     24     // glTF materials, or OBJ material colours

typedef struct ModelNode {
    char name[48];
    int  parent;             // -1 for roots
    Vec3 t; Quat r; Vec3 s;  // rest pose
    bool hidden;             // set by the game to hide accessory meshes
} ModelNode;

typedef struct ModelMesh {
    Mesh gpu;
    bool skinned;
    int  node;               // node whose transform applies (rigid meshes)
    int  tex;                // index into textures
} ModelMesh;

typedef struct AnimChannel {
    int node, path;          // path: 0 translation, 1 rotation, 2 scale
    int nkeys;
    float *times, *values;   // values: 3 or 4 floats per key
} AnimChannel;

typedef struct AnimClip {
    char name[64];
    float duration;
    int nchannels;
    AnimChannel *channels;
} AnimClip;

typedef struct Model {
    ModelNode nodes[MODEL_MAX_NODES]; int nnodes;
    ModelMesh meshes[MODEL_MAX_MESHES]; int nmeshes;
    int  joints[MODEL_MAX_JOINTS]; Mat4 inv_bind[MODEL_MAX_JOINTS]; int njoints;
    AnimClip *clips; int nclips;
    Texture textures[MODEL_MAX_TEX]; int ntextures;
    unsigned char *tex_px[MODEL_MAX_TEX]; int tex_w[MODEL_MAX_TEX], tex_h[MODEL_MAX_TEX];   // CPU copies for recolouring
    Vec3 bmin, bmax;         // rest-pose bounds
} Model;

// OBJ as a plain triangle list (3 vertices per triangle, material colour in the vertex colour,
// millimetre files scaled to metres, stood on y = 0). malloc'd; caller frees. For part files.
Vertex *model_obj_read(const char *path, Uint32 *nverts, Vec3 *bmin, Vec3 *bmax);
// Build a static model from a triangle list (white texture, vertex colours).
bool model_from_triangles(Gfx *g, Model *m, const Vertex *v, Uint32 nverts);

// Recolouring: the flat-colour atlases of low-poly packs have a handful of distinct colours, so a
// character is recoloured by remapping those. model_palette lists them by pixel count.
typedef struct ModelColor { unsigned char rgb[3]; int count; } ModelColor;
int  model_palette(const Model *m, ModelColor *out, int max);
// Rebuild the textures with every `from` colour replaced by `to` (exact matches). n pairs.
void model_recolor(Gfx *g, Model *m, const unsigned char (*from)[3], const unsigned char (*to)[3], int n);
// Names of nodes that carry meshes (the parts a character is made of), in file order.
int  model_part_names(const Model *m, const char **out, int max);
// Same, read straight out of a .glb/.gltf without loading it (the builder lists another file's
// parts before anything borrows them). Copies the names out; nothing is put on the GPU.
int  model_file_part_names(const char *path, char (*out)[48], int max);

// Two-segment playback so a clip's contact moment can be pinned to a gameplay timing.
typedef struct AnimPlayer {
    int   clip;    float time;  float rate1, rate2, split;  bool loop, hold;
    int   prev;    float prev_time; float prev_rate;         // fading out
    float fade, fade_dur;
} AnimPlayer;

typedef struct ModelPose {
    Mat4 global[MODEL_MAX_NODES];
    Mat4 joints[MODEL_MAX_JOINTS];
} ModelPose;

// Loads .glb/.gltf (skinned, animated) or .obj/.mtl (static, one flat colour per material; CAD
// exports). OBJ files larger than 50 units are taken as millimetres and scaled to metres.
bool model_load(Gfx *g, Model *m, const char *path, int max_tex_size);
void model_destroy(Gfx *g, Model *m);
int  model_find_clip(const Model *m, const char *name);   // -1 if missing
int  model_find_node(const Model *m, const char *name);
void model_hide_node(Model *m, const char *name, bool hidden);

// Start a clip. rate 1 = authored speed. fade_dur seconds of crossfade from whatever was playing.
void anim_play(AnimPlayer *p, const Model *m, int clip, float rate, bool loop, bool hold, float fade_dur);
// Start a clip so that clip time `contact` (seconds) arrives after `lead` seconds of game time and
// the remainder of the clip fills `tail` seconds. Non-looping, holds the last frame.
void anim_play_fitted(AnimPlayer *p, const Model *m, int clip, float contact, float lead, float tail, float fade_dur);
void anim_update(AnimPlayer *p, const Model *m, float dt);
bool anim_finished(const AnimPlayer *p, const Model *m);

void model_pose(const Model *m, const AnimPlayer *p, ModelPose *out);
void model_draw(Gfx *g, const Model *m, const ModelPose *pose, Mat4 world, Vec4 tint);
// Draw only the meshes whose node name is one of `names`, posed by `host` — another model that
// shares this one's skeleton (same joint names and inverse binds), so a part borrowed from a second
// file follows the host's animation. Skinned meshes take the host's joint matrices; rigid ones
// (helmets, hats, capes, weapons) hang off the host bone with the same name as theirs, and are
// skipped if the host has no such bone. `pose` is the host's pose. Returns how many were drawn.
int  model_draw_nodes(Gfx *g, const Model *m, const Model *host, const ModelPose *pose, Mat4 world, Vec4 tint, const char *const *names, int n);
// The host node a rigid mesh node hangs off (nearest ancestor whose name the host also has), with
// the transform chain from that node down to it. -1 if the host has no such bone.
int  model_host_node(const Model *m, const Model *host, int node, Mat4 *local);
