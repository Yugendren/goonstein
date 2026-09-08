#include "render_world.h"
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- procedural textures

static unsigned hash2(int x, int y, unsigned seed) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + seed * 982451653u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static float noise01(int x, int y, unsigned seed) { return (hash2(x, y, seed) & 0xffff) / 65535.0f; }
// Value noise with wrap at size so textures tile.
static float vnoise(float x, float y, int size, int cell, unsigned seed) {
    int n = size / cell;
    float fx = x / cell, fy = y / cell;
    int ix = (int)fx, iy = (int)fy;
    float tx = smoothstep(fx - ix), ty = smoothstep(fy - iy);
    float a = noise01(ix % n, iy % n, seed), b = noise01((ix + 1) % n, iy % n, seed);
    float c = noise01(ix % n, (iy + 1) % n, seed), d = noise01((ix + 1) % n, (iy + 1) % n, seed);
    return lerpf(lerpf(a, b, tx), lerpf(c, d, tx), ty);
}
static float fbm(float x, float y, int size, unsigned seed) {
    return vnoise(x, y, size, 32, seed) * 0.5f + vnoise(x, y, size, 16, seed + 1) * 0.3f + vnoise(x, y, size, 8, seed + 2) * 0.2f;
}
static void put(unsigned char *px, int i, float r, float g, float b) {
    // Quantise to 5 bits per channel: the PS2 palette crunch.
    px[i * 4 + 0] = (unsigned char)(clampf(r, 0, 1) * 31) * 8;
    px[i * 4 + 1] = (unsigned char)(clampf(g, 0, 1) * 31) * 8;
    px[i * 4 + 2] = (unsigned char)(clampf(b, 0, 1) * 31) * 8;
    px[i * 4 + 3] = 255;
}

static Texture make_texture(Gfx *g, int id) {
    const int S = 64;
    unsigned char *px = malloc((size_t)S * S * 4);
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        int i = y * S + x;
        float n = fbm((float)x, (float)y, S, 7 + id * 13);
        switch (id) {
        case TEX_STONE: {  // big blocks with dark mortar
            int bx = (x / 32 + ((y / 16) & 1)) & 1; (void)bx;
            int mx = ((x + ((y / 16) & 1) * 16) % 32) < 2, my = (y % 16) < 2;
            float v = 0.42f + n * 0.3f;
            if (mx || my) v *= 0.45f;
            put(px, i, v * 0.95f, v * 0.93f, v);
        } break;
        case TEX_TILE: {
            int line = (x % 16) == 0 || (y % 16) == 0;
            float v = 0.36f + n * 0.25f;
            if (line) v *= 0.55f;
            put(px, i, v * 0.85f, v * 0.9f, v);
        } break;
        case TEX_WOOD: {
            float grain = vnoise((float)x * 6, (float)y, S * 6, 32, 99) ;
            float v = 0.3f + grain * 0.3f + n * 0.1f;
            put(px, i, v, v * 0.7f, v * 0.45f);
        } break;
        case TEX_METAL: {
            float scratch = noise01(x, y / 4, 5) > 0.93f ? 0.15f : 0.0f;
            float v = 0.2f + n * 0.15f + scratch;
            int rivet = ((x % 32) < 3 && (y % 32) < 3);
            if (rivet) v += 0.2f;
            put(px, i, v * 0.9f, v * 0.95f, v);
        } break;
        case TEX_FLESH: {
            float vein = vnoise((float)x, (float)y, S, 8, 31) > 0.7f ? 0.15f : 0.0f;
            float v = 0.3f + n * 0.35f;
            put(px, i, v + 0.2f + vein, v * 0.45f, v * 0.5f);
        } break;
        default: {  // plaster
            float v = 0.55f + n * 0.25f;
            float stain = vnoise((float)x, (float)y, S, 16, 77);
            if (stain > 0.75f) v *= 0.7f;
            put(px, i, v, v * 0.97f, v * 0.9f);
        } break;
        }
    }
    Texture t = gfx_texture_create(g, px, S, S);
    free(px);
    return t;
}

void world_textures_create(Gfx *g, WorldTextures *wt) {
    for (int i = 0; i < TEX_COUNT; i++) wt->tex[i] = make_texture(g, i);
}
void world_textures_destroy(Gfx *g, WorldTextures *wt) {
    for (int i = 0; i < TEX_COUNT; i++) gfx_texture_destroy(g, &wt->tex[i]);
}

void draw_level(Gfx *g, const Level *lv, const WorldTextures *wt) {
    for (int i = 0; i < lv->nblocks; i++) {
        const Block *b = &lv->blocks[i];
        int t = (b->tex >= 0 && b->tex < TEX_COUNT) ? b->tex : TEX_PLASTER;
        gfx_draw_box(g, &wt->tex[t], b->center, b->size, 0, b->tint, b->uv_tile);
    }
}

// ---------------------------------------------------------------- characters

// A body part: box at local offset, rotated by pitch (about X) and roll (about Z) around a pivot.
static void part(Gfx *g, const Texture *skin, Mat4 base, Vec3 pivot, Vec3 offset, Vec3 size, float pitch, float roll, Vec4 tint) {
    Mat4 m = m4_mul(base, m4_mul(m4_translate(pivot), m4_mul(m4_mul(m4_rotate_z(roll), m4_rotate_x(pitch)), m4_mul(m4_translate(offset), m4_scale(size)))));
    gfx_draw(g, &g->cube, skin, m, tint, v4(1, 1, 0, 0));
}

typedef struct Pose {
    float torso_pitch, torso_roll, torso_y, torso_yaw;
    float head_pitch;
    float arm_l_pitch, arm_l_roll, arm_r_pitch, arm_r_roll;
    float leg_l_pitch, leg_r_pitch;
    float crouch;     // lowers the whole body
    float lie;        // 0..1 rotate the whole body to lying
} Pose;

static Pose pose_for(const Character *c, bool is_boss) {
    Pose p = {0};
    float t = c->anim_t;
    float sw = sinf(c->walk_phase);
    float bob = sinf(t * 1.6f) * 0.02f;
    switch (c->anim) {
    case ANIM_IDLE:
        p.torso_y = bob; p.arm_l_pitch = 0.08f; p.arm_r_pitch = 0.08f;
        if (is_boss) { p.torso_pitch = 0.35f; p.head_pitch = -0.3f; p.arm_l_pitch = 0.5f; p.arm_r_pitch = 0.5f; }
        break;
    case ANIM_WALK:
        p.leg_l_pitch = sw * 0.6f; p.leg_r_pitch = -sw * 0.6f;
        p.arm_l_pitch = -sw * 0.5f; p.arm_r_pitch = sw * 0.5f;
        p.torso_y = fabsf(sw) * 0.04f;
        if (is_boss) { p.torso_pitch = 0.4f; p.head_pitch = -0.3f; }
        break;
    case ANIM_ATTACK: {
        // wind back then swing across, weight shifts forward
        float k = t < 0.16f ? -t / 0.16f : fminf(1, (t - 0.16f) / 0.12f);
        p.arm_r_pitch = k < 0 ? -2.4f * -k : -2.4f + (2.4f + 0.8f) * k;
        p.arm_r_roll = k < 0 ? 0.6f * -k : 0.6f - 0.9f * k;
        p.torso_yaw = k < 0 ? 0.5f * -k : 0.5f - 1.0f * k;
        p.torso_pitch = k > 0 ? 0.25f * k : 0;
        p.leg_l_pitch = -0.3f; p.leg_r_pitch = 0.3f;
    } break;
    case ANIM_PARRY:
        p.arm_l_pitch = -1.9f; p.arm_r_pitch = -1.9f; p.arm_l_roll = 0.5f; p.arm_r_roll = -0.5f;
        p.torso_pitch = -0.1f; p.crouch = 0.08f;
        break;
    case ANIM_PARRY_HIT:
        p.arm_l_pitch = -2.2f; p.arm_r_pitch = -2.2f; p.arm_l_roll = 0.9f; p.arm_r_roll = -0.9f;
        p.torso_pitch = -0.3f; p.crouch = 0.15f;
        break;
    case ANIM_DODGE:
        p.torso_pitch = 0.9f; p.crouch = 0.5f; p.leg_l_pitch = 0.8f; p.leg_r_pitch = -0.6f;
        p.arm_l_pitch = 0.8f; p.arm_r_pitch = -0.6f;
        break;
    case ANIM_HURT:
        p.torso_pitch = -0.5f; p.head_pitch = -0.4f; p.arm_l_pitch = -0.8f; p.arm_r_pitch = -0.8f;
        p.arm_l_roll = 0.6f; p.arm_r_roll = -0.6f; p.leg_l_pitch = 0.3f; p.leg_r_pitch = -0.3f;
        break;
    case ANIM_KNEEL:
        p.crouch = 0.4f; p.torso_pitch = 0.3f; p.head_pitch = 0.45f;
        p.leg_l_pitch = 1.35f; p.leg_r_pitch = -0.15f; p.arm_l_pitch = 0.5f; p.arm_r_pitch = 0.5f;
        break;
    case ANIM_DEAD:
        p.lie = fminf(1, t / 0.6f); p.arm_l_pitch = -0.5f; p.arm_r_pitch = -0.7f; p.arm_l_roll = 0.9f; p.arm_r_roll = -0.9f;
        break;
    case ANIM_ROAR: {
        float k = fminf(1, t / 0.4f);
        p.torso_pitch = -0.4f * k; p.head_pitch = -0.7f * k;
        p.arm_l_pitch = -2.6f * k; p.arm_r_pitch = -2.6f * k; p.arm_l_roll = 1.0f * k; p.arm_r_roll = -1.0f * k;
        p.torso_y = 0.05f * sinf(t * 30) * k;
    } break;
    case ANIM_STAGGER:
        p.crouch = 0.6f; p.torso_pitch = 0.9f; p.head_pitch = 0.6f;
        p.leg_l_pitch = 1.4f; p.leg_r_pitch = -0.4f;
        p.arm_l_pitch = 1.0f; p.arm_r_pitch = 1.0f; p.arm_l_roll = 0.5f; p.arm_r_roll = -0.5f;
        p.torso_roll = sinf(t * 25) * 0.05f;
        break;
    case ANIM_WINDUP: {
        // Each move has its own silhouette so the player can read it. move_id is the index.
        float k = fminf(1, t * 1.6f);
        switch (c->move_id % 4) {
        case 0: p.torso_pitch = -0.6f * k; p.crouch = 0.35f * k; p.arm_r_pitch = -2.8f * k; p.arm_l_pitch = 0.9f * k; break;          // lunge: coil back, arm raised
        case 1: p.torso_yaw = -1.2f * k; p.arm_r_pitch = -1.3f * k; p.arm_r_roll = -1.6f * k; p.arm_l_pitch = 0.5f * k; break;          // sweep: arm out to the side
        case 2: p.arm_l_pitch = -3.0f * k; p.arm_r_pitch = -3.0f * k; p.torso_pitch = -0.5f * k; p.head_pitch = -0.5f * k; break;       // slam: both arms overhead
        default: p.arm_l_pitch = -1.5f * k; p.arm_r_pitch = -1.5f * k; p.torso_pitch = 0.6f * k; p.head_pitch = 0.3f * k; break;        // grab: arms forward, leaning in
        }
        p.torso_y = sinf(t * 40) * 0.01f * k;
    } break;
    case ANIM_STRIKE: {
        switch (c->move_id % 4) {
        case 0: p.torso_pitch = 0.7f; p.arm_r_pitch = -0.2f; p.arm_l_pitch = 0.8f; p.leg_l_pitch = -0.7f; p.leg_r_pitch = 0.7f; break;
        case 1: p.torso_yaw = 1.2f; p.arm_r_pitch = -1.3f; p.arm_r_roll = -1.2f; break;
        case 2: p.arm_l_pitch = -0.6f; p.arm_r_pitch = -0.6f; p.torso_pitch = 0.9f; p.head_pitch = 0.4f; break;
        default: p.arm_l_pitch = -1.5f; p.arm_r_pitch = -1.5f; p.arm_l_roll = -0.5f; p.arm_r_roll = 0.5f; p.torso_pitch = 0.9f; break;
        }
    } break;
    default: break;
    }
    return p;
}

void draw_character(Gfx *g, const Character *c, Vec3 base_color, Vec3 size, bool is_boss, const Texture *skin) {
    Pose p = pose_for(c, is_boss);
    float h = size.y;                                // total height
    float w = size.x;
    // Proportions as fractions of height
    float leg_h = h * (is_boss ? 0.36f : 0.45f), torso_h = h * (is_boss ? 0.42f : 0.36f), head_s = h * (is_boss ? 0.10f : 0.13f);
    float torso_w = w * (is_boss ? 1.5f : 1.0f), torso_d = w * (is_boss ? 0.9f : 0.55f);
    float limb = w * (is_boss ? 0.42f : 0.3f);
    float arm_h = h * (is_boss ? 0.5f : 0.38f);

    Vec4 tint = v4(base_color.x, base_color.y, base_color.z, 1);
    // Hit flash and telegraph glow
    tint.x = lerpf(tint.x, 1.0f, c->flash); tint.y = lerpf(tint.y, 1.0f, c->flash); tint.z = lerpf(tint.z, 1.0f, c->flash);
    if (c->tell > 0) {
        float k = c->tell * c->tell * (0.6f + 0.4f * sinf(c->anim_t * 30.0f));
        tint.x = lerpf(tint.x, c->tell_color.x * 1.4f, k); tint.y = lerpf(tint.y, c->tell_color.y * 1.4f, k); tint.z = lerpf(tint.z, c->tell_color.z * 1.4f, k);
    }
    Vec4 dark = v4(tint.x * 0.75f, tint.y * 0.75f, tint.z * 0.75f, 1);

    // Root: position, facing, crouch, and lying down for death.
    float root_y = c->pos.y - p.crouch * leg_h;
    Mat4 base = m4_mul(m4_translate(v3(c->pos.x, root_y, c->pos.z)), m4_rotate_y(c->yaw));
    if (p.lie > 0) {
        // Rotate about the feet, sink slightly so the body rests on the ground.
        float a = p.lie * (PI * 0.5f - 0.05f);
        base = m4_mul(base, m4_mul(m4_translate(v3(0, -p.lie * 0.15f, 0)), m4_rotate_x(-a)));
    }

    // Legs hang from the hip pivot
    float hip = leg_h;
    part(g, skin, base, v3(-limb * 0.6f, hip, 0), v3(0, -leg_h * 0.5f, 0), v3(limb, leg_h, limb), p.leg_l_pitch, 0, dark);
    part(g, skin, base, v3( limb * 0.6f, hip, 0), v3(0, -leg_h * 0.5f, 0), v3(limb, leg_h, limb), p.leg_r_pitch, 0, dark);

    // Torso pivots at the hip
    Mat4 torso = m4_mul(base, m4_mul(m4_translate(v3(0, hip + p.torso_y, 0)), m4_mul(m4_rotate_y(p.torso_yaw), m4_mul(m4_rotate_z(p.torso_roll), m4_rotate_x(p.torso_pitch)))));
    part(g, skin, torso, v3(0, 0, 0), v3(0, torso_h * 0.5f, 0), v3(torso_w, torso_h, torso_d), 0, 0, tint);
    // Head
    part(g, skin, torso, v3(0, torso_h, 0), v3(0, head_s * 0.6f, is_boss ? torso_d * 0.3f : 0), v3(head_s, head_s, head_s), p.head_pitch, 0, tint);
    // Arms hang from the shoulders
    float sh_y = torso_h * 0.92f, sh_x = torso_w * 0.5f + limb * 0.5f;
    part(g, skin, torso, v3(-sh_x, sh_y, 0), v3(0, -arm_h * 0.5f, 0), v3(limb, arm_h, limb), p.arm_l_pitch, p.arm_l_roll, dark);
    part(g, skin, torso, v3( sh_x, sh_y, 0), v3(0, -arm_h * 0.5f, 0), v3(limb, arm_h, limb), p.arm_r_pitch, p.arm_r_roll, dark);
    if (is_boss) {
        // A jagged crown of spikes so the silhouette reads from any angle.
        for (int i = 0; i < 4; i++) {
            float a = i * PI * 0.5f;
            part(g, skin, torso, v3(cosf(a) * torso_w * 0.35f, torso_h + head_s * 0.9f, sinf(a) * torso_d * 0.35f), v3(0, head_s * 0.4f, 0), v3(limb * 0.3f, head_s * 0.9f, limb * 0.3f), 0, 0, dark);
        }
    }
}
