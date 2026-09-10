// Camera: third-person orbit with lock-on, plus cutscene control.
#pragma once
#include "hmath.h"
#include "level.h"

// Default far plane. A level asks for its own with `look far METRES` (see level.h Look.view_far):
// 80 m suits the walled village levels and culls most of their world for free; a 300 m island
// needs ~600 to read as an island at all.
#define CAMERA_FAR_DEFAULT 80.0f

typedef enum CamMode { CAM_ORBIT, CAM_SCENE, CAM_ISO, CAM_FIRST } CamMode;

typedef struct Camera {
    CamMode mode;
    Vec3 eye, target; float fov;       // current, after smoothing and shake
    // Orbit state
    float yaw, pitch;                  // radians; yaw 0 looks down +Z
    float dist, height;                // desired distance and pivot height above the player's feet
    float cur_dist;                    // after wall collision
    bool  locked; Vec3 lock_pos; bool has_lock;
    float view_far;                    // far plane in metres (the level's `look far`); 0 = the 80 m default
    float roll;                        // radians the view lies over about the forward axis; a goon knocked flat sees the world side-on
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
void camera_iso_set(float pitch_deg, float dist, float fov_deg, float yaw_deg);   // overworld framing (from the level's camera line)
// Put the camera behind the player facing yaw (used on restarts).
void camera_snap_behind(Camera *c, Vec3 player_pos, float player_yaw, const Level *lv);
// First person: the eye rides the player's head. look_x/look_y are this frame's mouse deltas, with
// the orbit camera's sensitivity; pitch is clamped to the orbit camera's limit in both directions.
// bob scales the head bob (0 off, 1 the default subtle amount); speed is the player's planar speed
// in m/s and phase its walk phase accumulator.
void camera_first(Camera *c, Vec3 player_pos, float eye_height, float look_x, float look_y,
                  float bob, float speed, float phase, float dt);
// Put the first-person eye at the player facing yaw, pitch level (spawns, restarts).
void camera_snap_first(Camera *c, Vec3 player_pos, float eye_height, float yaw);
// Radians of yaw per unit of mouse delta, shared by every mouse-look camera.
float camera_mouse_sens(void);
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
