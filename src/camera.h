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
    // Frame-rate view offsets (Half-Life style): eased head motion advanced once per rendered frame
    // by camera_view_step/camera_view_land/camera_view_advance and applied by camera_first, so a
    // stair or a landing reads as a smooth ease instead of a snap tied to the 60 Hz sim tick.
    float step_off;                    // metres: held offset from a step, eased to 0 by camera_view_advance
    float dip, dip_v;                  // metres, m/s: landing dip spring (critically damped, omega 22)
    float bob_phase, bob_gain;         // radians, 0..1: walk-bob phase accumulator and its live gain
    // Scene
    Vec3 goal_eye, goal_target; float goal_fov;
    float shake, shake_t;
} Camera;

void camera_init(Camera *c);
// Orbit around the player. c->yaw/c->pitch are already current when this is called -- see
// camera_look, the only place that turns the view from input now. lock_pos is the lock-on target
// (used when locked). The camera never enters solid level blocks.
void camera_orbit(Camera *c, Vec3 player_pos, bool has_lock, Vec3 lock_pos, const Level *lv, float dt);
void camera_toggle_lock(Camera *c);
// Fixed-angle isometric follow for the overworld. yaw fixed, pitch fixed, distance fixed.
void camera_iso(Camera *c, Vec3 player_pos, const Level *lv, float dt);
void camera_iso_set(float pitch_deg, float dist, float fov_deg, float yaw_deg);   // overworld framing (from the level's camera line)
// Capture override: pin the third-person orbit to a fixed pitch/dist/fov/yaw. Mouse look stops
// working while it is on, so nothing in the game calls this -- only the HOLLOW_CAM env hook, whose
// whole job is taking the same shot twice under different looks (see assets/looks/README.md).
void camera_orbit_pin(float pitch_deg, float dist, float fov_deg, float yaw_deg, bool pin_yaw);
// pin_yaw false leaves the orbit's own yaw alone, which matters whenever anything is still walking
// around: movement is camera-relative, so freezing the yaw also freezes which way "forward" is and
// a bot walks off at a constant wrong angle and never reaches what it was going for.
// Put the camera behind the player facing yaw (used on restarts).
void camera_snap_behind(Camera *c, Vec3 player_pos, float player_yaw, const Level *lv);
// First person: the eye rides the player's head at the current c->yaw/c->pitch (see camera_look).
// bob scales the head bob (0 off, 1 the default subtle amount) as a final multiplier only -- the
// phase and gain live in the camera and are advanced once a frame by camera_view_advance, since
// camera_first itself may be called more than once per frame (it does not advance any phase).
void camera_first(Camera *c, Vec3 player_pos, float eye_height, float bob, float dt);
// Put the first-person eye at the player facing yaw, pitch level (spawns, restarts). Also clears
// the view offsets (step ease, landing dip, bob) so the head starts perfectly still.
void camera_snap_first(Camera *c, Vec3 player_pos, float eye_height, float yaw);
// Radians of yaw per unit of mouse delta, shared by every mouse-look camera. Scaled by the
// settings.txt `mouse_sens` multiplier set with camera_set_mouse_sens.
float camera_mouse_sens(void);
// settings.txt `mouse_sens`: 1.0 is today's default (0.0022 rad/pixel). Clamped to 0.05..10 so a
// bad or missing setting can't zero out or run away with the sensitivity.
void camera_set_mouse_sens(float mult);
// Applied once per RENDERED FRAME with that frame's mouse delta, so the view turns the instant the
// mouse moves instead of once every 16.7 ms of sim tick. No smoothing and no acceleration: the
// pointer delta is the rotation. stick_x/stick_y are gamepad axes in -1..1 and are a RATE (radians
// per second, scaled by dt) rather than a delta, which is why they need dt at all. Pitch clamps to
// the first-person limit while c->mode == CAM_FIRST, otherwise to the orbit camera's limit; yaw is
// kept wrapped into -PI..PI so it cannot grow without bound over a long session -- nothing may
// interpolate yaw across that wrap.
void camera_look(Camera *c, float mouse_dx, float mouse_dy, float stick_x, float stick_y, float dt);
// The eye does not sit exactly on the head: it eases up behind a step, dips when you land, and bobs
// as you walk. All three are advanced per FRAME, because at 144 Hz a 60 Hz bob is a rattle.
// The feet just snapped dy metres (+ up): hold the eye where it was and let it catch up.
void  camera_view_step(Camera *c, float dy);
// Landed at this speed (m/s, positive): kick the landing dip. Speeds under 2.5 m/s are ignored, so
// stepping off a kerb doesn't dip the view.
void  camera_view_land(Camera *c, float fall_speed);
// Advance the step ease, landing dip and walk bob by one rendered frame. speed is the player's
// planar speed in m/s; bob_amount is the level's bob knob (0 off, 1 the default amount).
void  camera_view_advance(Camera *c, float speed, float bob_amount, float dt);
// Metres to add to the eye height right now (step ease + landing dip).
float camera_view_offset(const Camera *c);
// Radians; one full cycle per stride. Shared with the viewmodel so it bobs in step with the camera.
float camera_bob_phase(const Camera *c);
// 0..1: how much bob is currently live (eases in with speed, out at a stop).
float camera_bob_gain(const Camera *c);
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
