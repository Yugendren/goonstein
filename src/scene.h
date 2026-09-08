// Cutscene: a text timeline of camera keyframes, subtitles, actor commands and screen effects.
// See assets/scenes/README.md for the format. The scene never touches game objects directly;
// actor commands go through SceneHost callbacks so the game decides what an actor is.
#pragma once
#include "hmath.h"
#include <stdbool.h>

#define SCENE_MAX_CMDS 256
#define SCENE_TEXT_MAX 200

typedef enum SceneCmdType {
    SC_CAM,        // camera keyframe: pos = eye, target = look-at, fov, a = 1 if hard cut into this key
    SC_SAY,        // subtitle text for dur seconds; actor = optional speaker name
    SC_ACTOR_MOVE, // actor walks to pos over dur seconds
    SC_ACTOR_FACE, // actor turns to face target point
    SC_ACTOR_ANIM, // actor plays animation text (idle, walk, attack, parry, kneel, dead, roar ...)
    SC_ACTOR_TELEPORT, // actor snaps to pos, facing yaw a
    SC_FADE,       // screen fade from a to b over dur
    SC_LETTERBOX,  // a = 1 on, 0 off
    SC_SHAKE,      // camera shake amount a for dur
    SC_SOUND,      // play sound named text
    SC_END,        // scene finishes
} SceneCmdType;

typedef struct SceneCmd {
    float t;                  // start time in seconds
    SceneCmdType type;
    char actor[32];
    char text[SCENE_TEXT_MAX];
    Vec3 pos, target;
    float fov, dur, a, b;
} SceneCmd;

typedef struct SceneHost {
    void *ud;
    void (*actor_move)(void *ud, const char *actor, Vec3 pos, float dur);
    void (*actor_face)(void *ud, const char *actor, Vec3 target);
    void (*actor_anim)(void *ud, const char *actor, const char *anim);
    void (*actor_teleport)(void *ud, const char *actor, Vec3 pos, float yaw);
    void (*sound)(void *ud, const char *name);
} SceneHost;

typedef struct Scene {
    SceneCmd cmds[SCENE_MAX_CMDS]; int n;   // sorted by t after load
    char path[512];
    // playback
    bool  playing, done;
    float time;
    int   next;                              // index of the next command to fire
    // outputs, valid while playing (and after, until scene_start is called again)
    bool  cam_valid; Vec3 cam_eye, cam_target; float cam_fov;
    char  subtitle[SCENE_TEXT_MAX]; char speaker[32]; float subtitle_until;
    float letterbox;                         // 0..1 target (game animates toward it)
    float fade;                              // 0 = black, 1 = visible
    float shake;                             // current shake amount
} Scene;

// Load and sort. Returns false and logs on parse error.
bool scene_load(Scene *sc, const char *path);
// Begin playback from t = 0. Outputs reset: fade 1, letterbox 1, no subtitle.
void scene_start(Scene *sc);
// Advance; fires commands whose t has passed via host, interpolates camera between CAM keys
// (ease in-out unless the next key is a cut), expires subtitles, decays shake, animates fade.
void scene_update(Scene *sc, float dt, const SceneHost *host);
// Jump to the end: fires remaining actor/teleport commands so state is consistent, sets done.
void scene_skip(Scene *sc, const SceneHost *host);
