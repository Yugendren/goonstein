// Camera: authored fixed angles in exploration, a follow camera in fights, and cutscene control.
#pragma once
#include "hmath.h"
#include "level.h"

typedef enum CamMode { CAM_FIXED, CAM_FOLLOW, CAM_SCENE } CamMode;

typedef struct Camera {
    CamMode mode;
    Vec3 eye, target; float fov;        // current, after smoothing and shake
    Vec3 goal_eye, goal_target; float goal_fov;
    float blend;                        // 0..1 progress of a smooth transition (1 = arrived)
    Vec3 from_eye, from_target; float from_fov;
    const CamVolume *vol;               // active fixed volume
    float shake, shake_t;
    // Follow tuning
    float follow_dist, follow_height, follow_lambda;
    float orbit;                        // manual right-stick offset in radians
    // Movement basis: fixed cameras cut, but input direction must not flip mid-press.
    Vec3 basis_fwd, basis_right; bool basis_locked;
} Camera;

void camera_init(Camera *c);
// Exploration: pick the authored volume for player position; cuts instantly on change.
void camera_set_fixed(Camera *c, const Level *lv, Vec3 player_pos);
// Fight: shoulder camera behind the player looking at the midpoint to the boss.
void camera_set_follow(Camera *c, Vec3 player_pos, float player_yaw, Vec3 boss_pos, float look_x, const Level *lv);
// Cutscene: absolute.
void camera_set_scene(Camera *c, Vec3 eye, Vec3 target, float fov, bool cut);
void camera_add_shake(Camera *c, float amount);
void camera_update(Camera *c, float dt);
Mat4 camera_view_proj(const Camera *c, float aspect);
// Convert stick input to a world XZ direction using the current camera. Locks the basis while
// the stick is held so a camera cut does not reverse the player's run.
Vec3 camera_move_dir(Camera *c, float in_x, float in_y);
