// Camera: third-person orbit with lock-on, plus cutscene control.
#pragma once
#include "hmath.h"
#include "level.h"

typedef enum CamMode { CAM_ORBIT, CAM_SCENE, CAM_ISO } CamMode;

typedef struct Camera {
    CamMode mode;
    Vec3 eye, target; float fov;       // current, after smoothing and shake
    // Orbit state
    float yaw, pitch;                  // radians; yaw 0 looks down +Z
    float dist, height;                // desired distance and pivot height above the player's feet
    float cur_dist;                    // after wall collision
    bool  locked; Vec3 lock_pos; bool has_lock;
    // Scene
    Vec3 goal_eye, goal_target; float goal_fov;
    float shake, shake_t;
} Camera;

void camera_init(Camera *c);
// Orbit around the player. look_x/look_y are this frame's mouse or stick deltas.
// lock_pos is the lock-on target (used when locked). The camera never enters solid level blocks.
void camera_orbit(Camera *c, Vec3 player_pos, float look_x, float look_y, bool has_lock, Vec3 lock_pos, const Level *lv, float dt);
void camera_toggle_lock(Camera *c);
// Fixed-angle isometric follow for the overworld. yaw fixed, pitch fixed, distance fixed.
void camera_iso(Camera *c, Vec3 player_pos, const Level *lv, float dt);
// Put the camera behind the player facing yaw (used on restarts).
void camera_snap_behind(Camera *c, Vec3 player_pos, float player_yaw, const Level *lv);
// Cutscene: absolute.
void camera_set_scene(Camera *c, Vec3 eye, Vec3 target, float fov, bool cut);
void camera_end_scene(Camera *c);
void camera_add_shake(Camera *c, float amount);
void camera_update(Camera *c, float dt);
Mat4 camera_view_proj(const Camera *c, float aspect);
// Same camera translated by `offset` (the pixel-art layer snaps the camera to its texel grid).
Mat4 camera_view_proj_offset(const Camera *c, float aspect, Vec3 offset);
// Convert stick input to a world XZ direction relative to the camera's facing.
Vec3 camera_move_dir(const Camera *c, float in_x, float in_y);
