#include "camera.h"
#include <string.h>

void camera_init(Camera *c) {
    memset(c, 0, sizeof *c);
    c->fov = c->goal_fov = c->from_fov = 60;
    c->blend = 1;
    c->follow_dist = 5.4f; c->follow_height = 2.9f; c->follow_lambda = 6.0f;
    c->eye = c->goal_eye = v3(0, 2, -4); c->target = c->goal_target = v3(0, 1, 0);
}

static void start_blend(Camera *c, bool cut) {
    if (cut) { c->blend = 1; c->eye = c->goal_eye; c->target = c->goal_target; c->fov = c->goal_fov; return; }
    c->from_eye = c->eye; c->from_target = c->target; c->from_fov = c->fov;
    c->blend = 0;
}

void camera_set_fixed(Camera *c, const Level *lv, Vec3 p) {
    const CamVolume *v = level_camera_at(lv, p);
    bool changed = (c->mode != CAM_FIXED) || (v != c->vol);
    c->mode = CAM_FIXED;
    if (!v) { c->vol = NULL; return; }  // keep the last angle if we walked out of every volume
    c->vol = v;
    c->goal_eye = v->eye; c->goal_target = v->target; c->goal_fov = v->fov;
    if (changed) start_blend(c, true);
}

void camera_set_follow(Camera *c, Vec3 pp, float pyaw, Vec3 bp, float look_x, const Level *lv) {
    bool entering = c->mode != CAM_FOLLOW;
    c->mode = CAM_FOLLOW; c->vol = NULL;
    c->orbit += look_x * 0.004f;
    c->orbit = angle_damp(c->orbit, 0, 1.5f, 1.0f / 60.0f);   // drift back to centre
    Vec3 to_boss = v3_sub(bp, pp); to_boss.y = 0;
    float yaw = v3_len(to_boss) > 0.5f ? atan2f(to_boss.x, to_boss.z) : pyaw;
    yaw += c->orbit;
    Vec3 back = v3(-sinf(yaw), 0, -cosf(yaw));
    Vec3 eye = v3_add(pp, v3_scale(back, c->follow_dist)); eye.y = pp.y + c->follow_height;
    // Keep the eye inside the arena so walls never swallow the camera.
    eye.x = clampf(eye.x, lv->arena_min.x + 0.4f, lv->arena_max.x - 0.4f);
    eye.z = clampf(eye.z, lv->arena_min.z + 0.4f, lv->arena_max.z - 0.4f);
    eye.y = clampf(eye.y, lv->arena_min.y + 0.4f, lv->arena_max.y - 0.4f);
    Vec3 mid = v3_lerp(v3_add(pp, v3(0, 1.0f, 0)), v3_add(bp, v3(0, 1.4f, 0)), 0.45f);
    c->goal_eye = eye; c->goal_target = mid; c->goal_fov = 58;
    if (entering) { start_blend(c, false); }
}

void camera_set_scene(Camera *c, Vec3 eye, Vec3 target, float fov, bool cut) {
    bool entering = c->mode != CAM_SCENE;
    c->mode = CAM_SCENE; c->vol = NULL;
    c->goal_eye = eye; c->goal_target = target; c->goal_fov = fov;
    if (entering || cut) start_blend(c, true);
}

void camera_add_shake(Camera *c, float amount) { c->shake = fmaxf(c->shake, amount); }

void camera_update(Camera *c, float dt) {
    if (c->blend < 1) {
        c->blend = fminf(1, c->blend + dt / 0.8f);
        float k = ease_in_out(c->blend);
        c->eye = v3_lerp(c->from_eye, c->goal_eye, k);
        c->target = v3_lerp(c->from_target, c->goal_target, k);
        c->fov = lerpf(c->from_fov, c->goal_fov, k);
    } else if (c->mode == CAM_FOLLOW) {
        c->eye = v3_damp(c->eye, c->goal_eye, c->follow_lambda, dt);
        c->target = v3_damp(c->target, c->goal_target, c->follow_lambda * 1.5f, dt);
        c->fov = damp(c->fov, c->goal_fov, 4, dt);
    } else {
        c->eye = c->goal_eye; c->target = c->goal_target; c->fov = c->goal_fov;
    }
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

Vec3 camera_move_dir(Camera *c, float in_x, float in_y) {
    float mag = sqrtf(in_x * in_x + in_y * in_y);
    if (mag < 0.05f) { c->basis_locked = false; return v3(0, 0, 0); }
    if (!c->basis_locked) {
        Vec3 f = v3_sub(c->target, c->eye); f.y = 0; f = v3_norm(f);
        if (v3_len(f) < 0.01f) f = v3(0, 0, 1);
        c->basis_fwd = f; c->basis_right = v3(f.z, 0, -f.x);
        c->basis_locked = true;
    }
    // in_y is +down on the stick, so forward is -in_y
    return v3_add(v3_scale(c->basis_right, in_x), v3_scale(c->basis_fwd, -in_y));
}
