#include "camera.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#define MOUSE_SENS 0.0022f
#define PITCH_MIN (-0.35f)
#define PITCH_MAX (1.05f)
#define FP_PITCH  PITCH_MAX     // first person looks as far up as the orbit camera looks down
#define FP_LOOK   6.0f          // how far ahead the look-at point sits
#define FP_FOV    70.0f         // wider than the orbit camera's 55: first person needs the peripheral read
#define CAM_STICK_RATE 2.6f     // radians/second at full stick deflection (camera_look)
#define BOB_STRIDE 2.2f         // metres per full walk-bob cycle (camera_view_advance)

// settings.txt mouse_sens multiplier; see camera_set_mouse_sens. The only mutable state this file
// adds beyond the Camera struct itself.
static float mouse_sens_mult = 1.0f;

void camera_init(Camera *c) {
    memset(c, 0, sizeof *c);
    c->fov = c->goal_fov = 55;
    c->yaw = 0; c->pitch = 0.32f;
    c->dist = 5.5f; c->height = 1.5f; c->cur_dist = c->dist;
    c->eye = v3(0, 2, -5); c->target = v3(0, 1, 0);
    c->view_far = CAMERA_FAR_DEFAULT;
}

static Vec3 orbit_dir(float yaw, float pitch) {
    // direction from pivot to eye
    return v3(-sinf(yaw) * cosf(pitch), sinf(pitch), -cosf(yaw) * cosf(pitch));
}

float camera_mouse_sens(void) { return MOUSE_SENS * mouse_sens_mult; }

void camera_set_mouse_sens(float mult) { mouse_sens_mult = clampf(mult, 0.05f, 10.0f); }

void camera_look(Camera *c, float mouse_dx, float mouse_dy, float stick_x, float stick_y, float dt) {
    float sens = camera_mouse_sens();
    c->yaw -= mouse_dx * sens;   // mouse right turns the view toward the right hand (-X when facing +Z)
    c->pitch += mouse_dy * sens;
    c->yaw -= stick_x * CAM_STICK_RATE * dt;
    c->pitch += stick_y * CAM_STICK_RATE * dt;
    // Wrap yaw into -PI..PI so it cannot grow without bound over a long session. Nothing may
    // interpolate yaw across this wrap (a naive lerp would spin the long way around the circle).
    c->yaw = fmodf(c->yaw + PI, 2 * PI);
    if (c->yaw < 0) c->yaw += 2 * PI;
    c->yaw -= PI;
    c->pitch = c->mode == CAM_FIRST ? clampf(c->pitch, -FP_PITCH, FP_PITCH) : clampf(c->pitch, PITCH_MIN, PITCH_MAX);
}

// View direction for a look-from-eye camera, matching camera_move_dir's forward: yaw 0 looks down
// +Z. Positive pitch looks DOWN, the same convention as orbit_dir (positive pitch lifts the eye
// above the pivot), so the body can be turned by copying c->yaw straight across.
static Vec3 look_dir(float yaw, float pitch) {
    return v3(sinf(yaw) * cosf(pitch), -sinf(pitch), cosf(yaw) * cosf(pitch));
}

// See camera.h: the HOLLOW_CAM capture override. Off until something calls camera_orbit_pin.
static bool  orb_pinned = false, orb_pin_yaw = true;
static float orb_pitch = 0.32f, orb_dist = 5.5f, orb_fov = 55.0f, orb_yaw = 0.0f;
void camera_orbit_pin(float pitch_deg, float dist, float fov_deg, float yaw_deg, bool pin_yaw) {
    orb_pinned = true; orb_pitch = pitch_deg * DEG2RAD; orb_dist = dist; orb_fov = fov_deg;
    orb_yaw = yaw_deg * DEG2RAD; orb_pin_yaw = pin_yaw;
}

void camera_orbit(Camera *c, Vec3 pp, bool has_lock, Vec3 lock_pos, const Level *lv, float dt) {
    if (c->mode != CAM_ORBIT) { c->pitch = 0.32f; c->cur_dist = c->dist = 5.5f; }   // entering from a scene or the fixed view: settle behind the player at a readable pitch
    c->mode = CAM_ORBIT;
    c->has_lock = has_lock; c->lock_pos = lock_pos;
    if (!has_lock) c->locked = false;
    Vec3 pivot = v3(pp.x, pp.y + c->height, pp.z);
    if (c->locked) {
        // Face the target: yaw toward it, pitch settles to a readable angle, the pivot leans toward it.
        Vec3 to = v3_sub(lock_pos, pp); to.y = 0;
        float want = v3_len(to) > 0.3f ? atan2f(to.x, to.z) : c->yaw;
        c->yaw = angle_damp(c->yaw, want, 8, dt);
        c->pitch = damp(c->pitch, 0.30f, 4, dt);
        pivot = v3_lerp(pivot, v3(lock_pos.x, lock_pos.y + 1.4f, lock_pos.z), 0.25f);
        // Big targets need room: pull back as the distance to the target grows, but stay close enough for a walled arena.
        float far_k = clampf((v3_len(to) - 3.0f) / 8.0f, 0, 1);
        c->dist = lerpf(6.0f, 8.0f, far_k);
    } else {
        c->dist = 5.5f;
    }
    // The capture pin wins over both branches: a fixed frame is the point of it.
    if (orb_pinned) { if (orb_pin_yaw) c->yaw = orb_yaw; c->pitch = orb_pitch; c->dist = orb_dist; }
    Vec3 want_eye = v3_add(pivot, v3_scale(orbit_dir(c->yaw, c->pitch), c->dist));
    // Wall collision: shorten along the ray until clear, with a margin so we don't clip geometry.
    float t = level_ray_solid(lv, pivot, want_eye, 0.35f);
    float target_dist = fmaxf(0.8f, c->dist * t);
    c->cur_dist = target_dist < c->cur_dist ? target_dist : damp(c->cur_dist, target_dist, 6, dt);
    Vec3 eye = v3_add(pivot, v3_scale(orbit_dir(c->yaw, c->pitch), c->cur_dist));
    // The render path re-places the eye every frame from the interpolated player position, so
    // damping it here on top of that would be double smoothing (and lag) -- set it directly. The
    // cur_dist damp above is a wall solve, not a smoothing of motion, and stays.
    c->eye = eye; c->target = pivot;
    c->fov = damp(c->fov, orb_pinned ? orb_fov : 55, 4, dt);
}

void camera_toggle_lock(Camera *c) { if (c->has_lock) c->locked = !c->locked; }

#define ISO_YAW   (-35.0f * DEG2RAD)
#define ISO_PITCH (36.0f * DEG2RAD)
#define ISO_DIST  14.0f
static float iso_pitch = ISO_PITCH, iso_dist = ISO_DIST, iso_fov = 32, iso_yaw = ISO_YAW;
void camera_iso_set(float pitch_deg, float dist, float fov_deg, float yaw_deg) { iso_pitch = pitch_deg * DEG2RAD; iso_dist = dist; iso_fov = fov_deg; iso_yaw = yaw_deg * DEG2RAD; }
void camera_iso(Camera *c, Vec3 pp, const Level *lv, float dt) {
    (void)lv;
    bool entering = c->mode != CAM_ISO;
    c->mode = CAM_ISO;
    c->yaw = iso_yaw; c->pitch = iso_pitch; c->dist = c->cur_dist = SDL_getenv("HOLLOW_ISO_DIST") ? (float)atof(SDL_getenv("HOLLOW_ISO_DIST")) : iso_dist;   // env: overview captures
    Vec3 pivot = v3(pp.x, pp.y + 1.0f, pp.z);
    Vec3 eye = v3_add(pivot, v3_scale(orbit_dir(c->yaw, c->pitch), c->dist));
    if (entering) { c->eye = eye; c->target = pivot; }
    c->eye = v3_damp(c->eye, eye, 8, dt);
    c->target = v3_damp(c->target, pivot, 8, dt);
    c->fov = damp(c->fov, iso_fov, 4, dt);   // a long lens flattens the view (isometric feel); a wider one reads as over the shoulder
}

void camera_snap_behind(Camera *c, Vec3 pp, float yaw, const Level *lv) {
    c->mode = CAM_ORBIT; c->yaw = yaw; c->pitch = 0.32f; c->cur_dist = c->dist = 5.5f;
    Vec3 pivot = v3(pp.x, pp.y + c->height, pp.z);
    Vec3 eye = v3_add(pivot, v3_scale(orbit_dir(c->yaw, c->pitch), c->dist));
    float t = level_ray_solid(lv, pivot, eye, 0.35f);
    c->cur_dist = fmaxf(0.8f, c->dist * t);
    c->eye = v3_add(pivot, v3_scale(orbit_dir(c->yaw, c->pitch), c->cur_dist));
    c->target = pivot;
}

void camera_first(Camera *c, Vec3 player_pos, float eye_height, float bob, float dt) {
    bool entering = c->mode != CAM_FIRST;
    c->mode = CAM_FIRST; c->locked = false; c->has_lock = false;
    c->dist = c->cur_dist = 0;
    // Head bob: two vertical dips and one lateral sway per stride. The phase and gain are advanced
    // once a frame by camera_view_advance (not here -- this may be called more than once per
    // frame); `bob` is applied here only as a final amplitude multiplier, so a level can turn it
    // off with `view first 0` without touching the accumulator.
    float gain = c->bob_gain;
    float by = sinf(c->bob_phase * 2.0f) * 0.018f * gain * bob;
    float bx = sinf(c->bob_phase) * 0.014f * gain * bob;
    Vec3 fwd = look_dir(c->yaw, c->pitch);
    Vec3 right = v3_norm(v3_cross(fwd, v3(0, 1, 0)));
    // No damping here: the eye must track the player exactly so game.c's frame-rate interpolation
    // (which lerps eye/target between ticks) doesn't double-smooth.
    float h = eye_height + camera_view_offset(c) + by;
    c->eye = v3_add(v3(player_pos.x, player_pos.y + h, player_pos.z), v3_scale(right, bx));
    c->target = v3_add(c->eye, v3_scale(fwd, FP_LOOK));
    c->fov = entering ? FP_FOV : damp(c->fov, FP_FOV, 6, dt);
}

void camera_snap_first(Camera *c, Vec3 player_pos, float eye_height, float yaw) {
    c->mode = CAM_FIRST; c->yaw = yaw; c->pitch = 0; c->dist = c->cur_dist = 0; c->fov = FP_FOV;
    c->step_off = 0; c->dip = 0; c->dip_v = 0; c->bob_phase = 0; c->bob_gain = 0;
    Vec3 fwd = look_dir(c->yaw, c->pitch);
    c->eye = v3(player_pos.x, player_pos.y + eye_height, player_pos.z);
    c->target = v3_add(c->eye, v3_scale(fwd, FP_LOOK));
}

// See camera.h: eased step-up, landing dip and walk bob, all advanced per rendered frame.
void camera_view_step(Camera *c, float dy) {
    c->step_off = clampf(c->step_off - dy, -0.7f, 0.7f);
}

void camera_view_land(Camera *c, float fall_speed) {
    if (fall_speed < 2.5f) return;   // stepping off a kerb must not dip the view
    // A critically damped spring kicked with velocity v peaks at v / (omega * e), so the impulse has
    // to be about 0.6 of the fall speed for a 1 m drop (6.3 m/s) to buy the ~0.1 m dip that reads as
    // knees giving. The clamps keep a fall off a cliff from planting the eye in the floor.
    c->dip_v -= clampf(fall_speed, 0, 12.0f) * 0.55f;
}

void camera_view_advance(Camera *c, float speed, float bob_amount, float dt) {
    // Step ease: chase zero at a rate proportional to how far off we are (fast for a big step,
    // floored so a tiny step doesn't linger), clamped so it can never overshoot past zero.
    float rate = fmaxf(fabsf(c->step_off) * 18.0f, 0.35f) * dt;
    if (c->step_off > 0) c->step_off = fmaxf(0.0f, c->step_off - rate);
    else c->step_off = fminf(0.0f, c->step_off + rate);

    // Landing dip: critically damped spring back to zero.
    const float omega = 12.0f;   // peaks ~85 ms after the landing and is gone inside 0.4 s
    c->dip_v += (-c->dip * omega * omega - 2 * omega * c->dip_v) * dt;
    c->dip += c->dip_v * dt;
    c->dip = clampf(c->dip, -0.16f, 0.02f);

    // Bob: gain eases toward the target (so stopping doesn't cut the bob dead), phase advances with
    // distance walked -- one full cycle per BOB_STRIDE metres -- and parks at 0 once the gain has
    // died so the next walk starts from a level head.
    float target_gain = bob_amount * clampf(speed / 3.0f, 0, 1);
    c->bob_gain = damp(c->bob_gain, target_gain, 6, dt);
    if (c->bob_gain < 0.001f) {
        c->bob_gain = 0; c->bob_phase = 0;
    } else {
        c->bob_phase = fmodf(c->bob_phase + 2 * PI * speed / BOB_STRIDE * dt, 2 * PI);
    }
}

float camera_view_offset(const Camera *c) { return c->step_off + c->dip; }
float camera_bob_phase(const Camera *c) { return c->bob_phase; }
float camera_bob_gain(const Camera *c) { return c->bob_gain; }

void camera_set_scene(Camera *c, Vec3 eye, Vec3 target, float fov, bool cut) {
    (void)cut;
    c->mode = CAM_SCENE;
    c->goal_eye = eye; c->goal_target = target; c->goal_fov = fov;
    c->eye = eye; c->target = target; c->fov = fov;
}

void camera_end_scene(Camera *c) { c->mode = CAM_ORBIT; }

void camera_add_shake(Camera *c, float amount) { c->shake = fmaxf(c->shake, amount); }

void camera_update(Camera *c, float dt) {
    c->shake = fmaxf(0, c->shake - dt * 2.2f);
    c->shake_t += dt;
}

Mat4 camera_view_proj(const Camera *c, float aspect) { return camera_view_proj_offset(c, aspect, v3(0, 0, 0)); }

Mat4 camera_view_proj_offset(const Camera *c, float aspect, Vec3 offset) {
    Vec3 eye = v3_add(c->eye, offset);
    if (c->shake > 0.001f) {
        float s = c->shake * 0.12f;
        eye = v3_add(eye, v3(sinf(c->shake_t * 47.0f) * s, cosf(c->shake_t * 61.0f) * s, sinf(c->shake_t * 53.0f) * s * 0.5f));
    }
    Vec3 target = v3_add(c->target, offset);
    Vec3 up = v3(0, 1, 0);
    if (fabsf(c->roll) > 1e-4f) {
        // Rodrigues: rotate up about the forward axis so the horizon tilts with the camera.
        Vec3 k = v3_norm(v3_sub(target, eye));
        float ca = cosf(c->roll), sa = sinf(c->roll);
        up = v3_add(v3_scale(up, ca), v3_add(v3_scale(v3_cross(k, up), sa), v3_scale(k, v3_dot(k, up) * (1 - ca))));
    }
    Mat4 view = m4_look_at(eye, target, up);
    Mat4 proj = m4_perspective(c->fov * DEG2RAD, aspect, 0.1f, c->view_far > 1 ? c->view_far : CAMERA_FAR_DEFAULT);
    return m4_mul(proj, view);
}

Vec3 camera_move_dir(const Camera *c, float in_x, float in_y) {
    float mag = sqrtf(in_x * in_x + in_y * in_y);
    if (mag < 0.05f) return v3(0, 0, 0);
    Vec3 f = v3(sinf(c->yaw), 0, cosf(c->yaw));
    Vec3 r = v3(-f.z, 0, f.x);   // right-handed: facing +Z, right is -X
    return v3_add(v3_scale(r, in_x), v3_scale(f, -in_y));   // stick up (in_y < 0) is forward
}
