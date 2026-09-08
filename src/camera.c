#include "camera.h"
#include <string.h>

#define MOUSE_SENS 0.0022f
#define PITCH_MIN (-0.35f)
#define PITCH_MAX (1.05f)

void camera_init(Camera *c) {
    memset(c, 0, sizeof *c);
    c->fov = c->goal_fov = 55;
    c->yaw = 0; c->pitch = 0.32f;
    c->dist = 5.5f; c->height = 1.5f; c->cur_dist = c->dist;
    c->eye = v3(0, 2, -5); c->target = v3(0, 1, 0);
}

static Vec3 orbit_dir(float yaw, float pitch) {
    // direction from pivot to eye
    return v3(-sinf(yaw) * cosf(pitch), sinf(pitch), -cosf(yaw) * cosf(pitch));
}

void camera_orbit(Camera *c, Vec3 pp, float look_x, float look_y, bool has_lock, Vec3 lock_pos, const Level *lv, float dt) {
    c->mode = CAM_ORBIT;
    c->has_lock = has_lock; c->lock_pos = lock_pos;
    if (!has_lock) c->locked = false;
    Vec3 pivot = v3(pp.x, pp.y + c->height, pp.z);
    if (c->locked) {
        // Face the target: yaw toward it, pitch settles to a readable angle, the pivot leans toward it.
        Vec3 to = v3_sub(lock_pos, pp); to.y = 0;
        float want = v3_len(to) > 0.3f ? atan2f(to.x, to.z) : c->yaw;
        c->yaw = angle_damp(c->yaw, want, 8, dt);
        c->pitch = damp(c->pitch, 0.42f, 4, dt);
        pivot = v3_lerp(pivot, v3(lock_pos.x, lock_pos.y + 1.4f, lock_pos.z), 0.3f);
        // Big targets need room: pull back as the distance to the target grows.
        float far_k = clampf((v3_len(to) - 3.0f) / 8.0f, 0, 1);
        c->dist = lerpf(8.5f, 11.0f, far_k);
    } else {
        c->yaw -= look_x * MOUSE_SENS;   // mouse right turns the view toward the right hand (-X when facing +Z)
        c->pitch = clampf(c->pitch + look_y * MOUSE_SENS, PITCH_MIN, PITCH_MAX);
        c->dist = 5.5f;
    }
    Vec3 want_eye = v3_add(pivot, v3_scale(orbit_dir(c->yaw, c->pitch), c->dist));
    // Wall collision: shorten along the ray until clear, with a margin so we don't clip geometry.
    float t = level_ray_solid(lv, pivot, want_eye, 0.35f);
    float target_dist = fmaxf(0.8f, c->dist * t);
    c->cur_dist = target_dist < c->cur_dist ? target_dist : damp(c->cur_dist, target_dist, 6, dt);
    Vec3 eye = v3_add(pivot, v3_scale(orbit_dir(c->yaw, c->pitch), c->cur_dist));
    c->eye = v3_damp(c->eye, eye, 30, dt);
    c->target = v3_damp(c->target, pivot, 30, dt);
    c->fov = damp(c->fov, 55, 4, dt);
}

void camera_toggle_lock(Camera *c) { if (c->has_lock) c->locked = !c->locked; }

#define ISO_YAW   (-35.0f * DEG2RAD)
#define ISO_PITCH (36.0f * DEG2RAD)
#define ISO_DIST  14.0f
void camera_iso(Camera *c, Vec3 pp, const Level *lv, float dt) {
    (void)lv;
    bool entering = c->mode != CAM_ISO;
    c->mode = CAM_ISO;
    c->yaw = ISO_YAW; c->pitch = ISO_PITCH; c->dist = c->cur_dist = ISO_DIST;
    Vec3 pivot = v3(pp.x, pp.y + 1.0f, pp.z);
    Vec3 eye = v3_add(pivot, v3_scale(orbit_dir(c->yaw, c->pitch), c->dist));
    if (entering) { c->eye = eye; c->target = pivot; }
    c->eye = v3_damp(c->eye, eye, 8, dt);
    c->target = v3_damp(c->target, pivot, 8, dt);
    c->fov = damp(c->fov, 32, 4, dt);   // long lens flattens the view, the isometric feel
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

Mat4 camera_view_proj(const Camera *c, float aspect) {
    Vec3 eye = c->eye;
    if (c->shake > 0.001f) {
        float s = c->shake * 0.12f;
        eye = v3_add(eye, v3(sinf(c->shake_t * 47.0f) * s, cosf(c->shake_t * 61.0f) * s, sinf(c->shake_t * 53.0f) * s * 0.5f));
    }
    Mat4 view = m4_look_at(eye, c->target, v3(0, 1, 0));
    Mat4 proj = m4_perspective(c->fov * DEG2RAD, aspect, 0.1f, 80.0f);
    return m4_mul(proj, view);
}

Vec3 camera_move_dir(const Camera *c, float in_x, float in_y) {
    float mag = sqrtf(in_x * in_x + in_y * in_y);
    if (mag < 0.05f) return v3(0, 0, 0);
    Vec3 f = v3(sinf(c->yaw), 0, cosf(c->yaw));
    Vec3 r = v3(-f.z, 0, f.x);   // right-handed: facing +Z, right is -X
    return v3_add(v3_scale(r, in_x), v3_scale(f, -in_y));   // stick up (in_y < 0) is forward
}
