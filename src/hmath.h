// Small vector / matrix library. Matrices are column-major, depth range 0..1, y-up NDC,
// which is what SDL_GPU expects on every backend.
#pragma once
#include <math.h>
#include <stdbool.h>

#define PI 3.14159265358979f
#define DEG2RAD (PI / 180.0f)

typedef struct Vec2 { float x, y; } Vec2;
typedef struct Vec3 { float x, y, z; } Vec3;
typedef struct Vec4 { float x, y, z, w; } Vec4;
typedef struct Mat4 { float m[16]; } Mat4;  // m[col*4 + row]

static inline Vec3 v3(float x, float y, float z) { return (Vec3){x, y, z}; }
static inline Vec4 v4(float x, float y, float z, float w) { return (Vec4){x, y, z, w}; }
static inline Vec3 v3_add(Vec3 a, Vec3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline Vec3 v3_sub(Vec3 a, Vec3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline Vec3 v3_scale(Vec3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3_dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 v3_cross(Vec3 a, Vec3 b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float v3_len(Vec3 a) { return sqrtf(v3_dot(a, a)); }
static inline Vec3 v3_norm(Vec3 a) { float l = v3_len(a); return l > 1e-6f ? v3_scale(a, 1.0f / l) : a; }
static inline Vec3 v3_lerp(Vec3 a, Vec3 b, float t) { return v3_add(a, v3_scale(v3_sub(b, a), t)); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float smoothstep(float t) { t = clampf(t, 0, 1); return t * t * (3.0f - 2.0f * t); }
static inline float ease_in_out(float t) { t = clampf(t, 0, 1); return t < 0.5f ? 2 * t * t : 1 - powf(-2 * t + 2, 2) / 2; }
// Move v toward target by at most step
static inline float approach(float v, float target, float step) {
    if (v < target) return fminf(v + step, target);
    return fmaxf(v - step, target);
}
// Exponential smoothing toward target, frame-rate independent
static inline float damp(float v, float target, float lambda, float dt) {
    return lerpf(v, target, 1.0f - expf(-lambda * dt));
}
static inline Vec3 v3_damp(Vec3 v, Vec3 t, float lambda, float dt) {
    return v3_lerp(v, t, 1.0f - expf(-lambda * dt));
}
static inline float angle_wrap(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }
static inline float angle_damp(float a, float target, float lambda, float dt) {
    return a + angle_wrap(target - a) * (1.0f - expf(-lambda * dt));
}

static inline Mat4 m4_identity(void) {
    Mat4 r = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    return r;
}
static inline Mat4 m4_mul(Mat4 a, Mat4 b) {  // a * b
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int rw = 0; rw < 4; rw++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rw] * b.m[c * 4 + k];
            r.m[c * 4 + rw] = s;
        }
    return r;
}
static inline Vec3 m4_mul_point(Mat4 a, Vec3 p) {
    return v3(a.m[0] * p.x + a.m[4] * p.y + a.m[8] * p.z + a.m[12],
              a.m[1] * p.x + a.m[5] * p.y + a.m[9] * p.z + a.m[13],
              a.m[2] * p.x + a.m[6] * p.y + a.m[10] * p.z + a.m[14]);
}
static inline Mat4 m4_translate(Vec3 t) {
    Mat4 r = m4_identity();
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}
static inline Mat4 m4_scale(Vec3 s) {
    Mat4 r = m4_identity();
    r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
    return r;
}
static inline Mat4 m4_rotate_y(float a) {
    Mat4 r = m4_identity();
    float c = cosf(a), s = sinf(a);
    r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
    return r;
}
static inline Mat4 m4_rotate_x(float a) {
    Mat4 r = m4_identity();
    float c = cosf(a), s = sinf(a);
    r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
    return r;
}
static inline Mat4 m4_rotate_z(float a) {
    Mat4 r = m4_identity();
    float c = cosf(a), s = sinf(a);
    r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
    return r;
}
// translate * rotateY * scale, the usual object transform
static inline Mat4 m4_trs(Vec3 t, float yaw, Vec3 s) {
    return m4_mul(m4_translate(t), m4_mul(m4_rotate_y(yaw), m4_scale(s)));
}
// Right-handed view matrix, camera looks down -Z.
static inline Mat4 m4_look_at(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 f = v3_norm(v3_sub(target, eye));
    Vec3 s = v3_norm(v3_cross(f, up));
    Vec3 u = v3_cross(s, f);
    Mat4 r = m4_identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -v3_dot(s, eye);
    r.m[13] = -v3_dot(u, eye);
    r.m[14] = v3_dot(f, eye);
    return r;
}
// Perspective with depth mapped to [0, 1].
static inline Mat4 m4_perspective(float fov_y_rad, float aspect, float zn, float zf) {
    float f = 1.0f / tanf(fov_y_rad * 0.5f);
    Mat4 r = {{0}};
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = zf / (zn - zf);
    r.m[11] = -1.0f;
    r.m[14] = (zn * zf) / (zn - zf);
    return r;
}

// ---------------------------------------------------------------- quaternions (x, y, z, w)
typedef struct Quat { float x, y, z, w; } Quat;
static inline Quat quat_identity(void) { return (Quat){0, 0, 0, 1}; }
static inline Quat quat_norm(Quat q) {
    float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l < 1e-8f) return quat_identity();
    return (Quat){q.x / l, q.y / l, q.z / l, q.w / l};
}
static inline Quat quat_slerp(Quat a, Quat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) { b = (Quat){-b.x, -b.y, -b.z, -b.w}; d = -d; }
    if (d > 0.9995f) return quat_norm((Quat){lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t), lerpf(a.w, b.w, t)});
    float th = acosf(d), s = sinf(th);
    float wa = sinf((1 - t) * th) / s, wb = sinf(t * th) / s;
    return (Quat){a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}
static inline Mat4 m4_ortho(float l, float r, float b, float t, float zn, float zf) {   // depth 0..1
    Mat4 m = {{0}};
    m.m[0] = 2.0f / (r - l); m.m[5] = 2.0f / (t - b); m.m[10] = 1.0f / (zn - zf);
    m.m[12] = -(r + l) / (r - l); m.m[13] = -(t + b) / (t - b); m.m[14] = zn / (zn - zf); m.m[15] = 1.0f;
    return m;
}
static inline Mat4 m4_from_quat(Quat q) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    Mat4 r = m4_identity();
    r.m[0] = 1 - 2 * (y * y + z * z); r.m[1] = 2 * (x * y + z * w);     r.m[2] = 2 * (x * z - y * w);
    r.m[4] = 2 * (x * y - z * w);     r.m[5] = 1 - 2 * (x * x + z * z); r.m[6] = 2 * (y * z + x * w);
    r.m[8] = 2 * (x * z + y * w);     r.m[9] = 2 * (y * z - x * w);     r.m[10] = 1 - 2 * (x * x + y * y);
    return r;
}
static inline Mat4 m4_from_trs(Vec3 t, Quat r, Vec3 s) {
    return m4_mul(m4_translate(t), m4_mul(m4_from_quat(r), m4_scale(s)));
}

// General 4x4 inverse (for sky ray reconstruction)
static inline Mat4 m4_inverse(Mat4 a) {
    const float *m = a.m; float inv[16];
    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    Mat4 r; float id = det != 0 ? 1.0f / det : 0;
    for (int i = 0; i < 16; i++) r.m[i] = inv[i] * id;
    return r;
}

// ---------------------------------------------------------------- frustum culling
// Six inward-facing clip planes pulled out of a view_proj (Gribb/Hartmann), so the same test culls
// against the camera's perspective matrix and the sun's orthographic one. Depth range is 0..1, so
// the near plane is row2 alone; planes are normalised, making the sphere test metric.
typedef struct Frustum { Vec4 p[6]; } Frustum;   // p.xyz = normal, p.w = d; inside when n.q + d >= 0

static inline Vec4 frustum_plane_norm(float a, float b, float c, float d) {
    float l = sqrtf(a * a + b * b + c * c);
    if (l < 1e-8f) return v4(0, 0, 0, 1);   // degenerate: never culls
    return v4(a / l, b / l, c / l, d / l);
}
static inline Frustum frustum_from_view_proj(Mat4 vp) {
    const float *m = vp.m;   // row i = (m[i], m[4+i], m[8+i], m[12+i])
    float r0[4] = {m[0], m[4], m[8],  m[12]}, r1[4] = {m[1], m[5], m[9],  m[13]};
    float r2[4] = {m[2], m[6], m[10], m[14]}, r3[4] = {m[3], m[7], m[11], m[15]};
    Frustum f;
    f.p[0] = frustum_plane_norm(r3[0] + r0[0], r3[1] + r0[1], r3[2] + r0[2], r3[3] + r0[3]);   // left
    f.p[1] = frustum_plane_norm(r3[0] - r0[0], r3[1] - r0[1], r3[2] - r0[2], r3[3] - r0[3]);   // right
    f.p[2] = frustum_plane_norm(r3[0] + r1[0], r3[1] + r1[1], r3[2] + r1[2], r3[3] + r1[3]);   // bottom
    f.p[3] = frustum_plane_norm(r3[0] - r1[0], r3[1] - r1[1], r3[2] - r1[2], r3[3] - r1[3]);   // top
    f.p[4] = frustum_plane_norm(r2[0], r2[1], r2[2], r2[3]);                                   // near (z >= 0)
    f.p[5] = frustum_plane_norm(r3[0] - r2[0], r3[1] - r2[1], r3[2] - r2[2], r3[3] - r2[3]);   // far
    return f;
}
// Conservative: true when the sphere is inside or straddles the frustum.
static inline bool frustum_sees_sphere(const Frustum *f, Vec3 c, float r) {
    for (int i = 0; i < 6; i++)
        if (f->p[i].x * c.x + f->p[i].y * c.y + f->p[i].z * c.z + f->p[i].w < -r) return false;
    return true;
}
