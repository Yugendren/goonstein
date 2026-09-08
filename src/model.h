// Skinned models from glTF (.glb): node hierarchy, one skin, animation clips, embedded textures.
// Rigid meshes parented to bones (helmets, weapons) follow the animated node transforms.
#pragma once
#include "gfx.h"

#define MODEL_MAX_NODES   128
#define MODEL_MAX_MESHES  48
#define MODEL_MAX_JOINTS  64
#define MODEL_MAX_TEX     4

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
    Vec3 bmin, bmax;         // rest-pose bounds
} Model;

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
