// Renderer: stylised forward lighting on SDL_GPU.
// World draws go to an HDR target (16-bit float) with depth. A sky pass fills the background,
// then decals and additive particles. Bloom is extracted and blurred at low resolution, and a
// post pass tone-maps and grades into an LDR target, where the UI is drawn. That is blitted
// to the swapchain with letterboxing. Screenshots read the LDR target.
#pragma once
#include "platform.h"
#include "hmath.h"

typedef struct Vertex { float pos[3], normal[3], uv[2], color[4]; } Vertex;
typedef struct SkinVertex { float pos[3], normal[3], uv[2]; Uint8 joints[4]; float weights[4]; } SkinVertex;
typedef struct PVertex { float pos[3], uv[2], color[4]; } PVertex;      // particles, decals
typedef struct UIVertex { float pos[2], uv[2], color[4]; } UIVertex;

typedef struct Mesh { SDL_GPUBuffer *vb, *ib; Uint32 index_count; } Mesh;
typedef struct Texture { SDL_GPUTexture *tex; int w, h; } Texture;

#define GFX_MAX_LIGHTS 16
typedef struct PointLight { Vec3 pos; float radius; Vec3 color; float intensity; } PointLight;

typedef struct FrameParams {
    Mat4  view_proj; Vec3 cam_pos, cam_right, cam_up;
    Vec3  sun_dir; float sun_intensity; Vec3 sun_color;
    Vec3  sky_ambient, ground_ambient;
    Vec3  fog_color; float fog_density, fog_height_base, fog_height_falloff, fog_scatter, fog_start;
    float toon_softness, shadow_floor, rim_power;
    // sky
    Vec3  sky_zenith, sky_horizon, sky_ground; float sun_glow, stars, sky_fog_blend;
    PointLight lights[GFX_MAX_LIGHTS]; int nlights;
} FrameParams;

typedef struct Material { Vec4 tint; Vec3 emissive; Vec3 rim_color; float rim; float unlit; } Material;

typedef struct PostParams {
    float grain, vignette, fade;
    Vec3  flash_color; float flash;
    float exposure, saturation, contrast, bloom;
    Vec3  lift, gain;
    float bloom_threshold, bloom_knee;
} PostParams;

#define UI_MAX_VERTS 262144
#define P_MAX_VERTS  (4096 * 6)

typedef struct Gfx {
    SDL_GPUDevice *dev;
    int iw, ih;                                   // internal resolution
    SDL_GPUTexture *hdr, *depth, *ldr, *bloom_a, *bloom_b;
    int bw, bh;                                   // bloom resolution
    SDL_GPUTextureFormat swap_format;
    SDL_GPUGraphicsPipeline *pipe_world, *pipe_skin, *pipe_sky, *pipe_particle_add, *pipe_particle_alpha,
                            *pipe_bright, *pipe_blur, *pipe_post, *pipe_ui, *pipe_blit;
    SDL_GPUSampler *samp_nearest, *samp_linear, *samp_clamp;
    SDL_GPUBuffer *ui_vb, *p_vb; SDL_GPUTransferBuffer *ui_xfer, *p_xfer;
    UIVertex *ui_verts; Uint32 ui_count;
    PVertex *p_add, *p_alpha; Uint32 p_add_count, p_alpha_count;
    Texture white, soft;                          // 1x1 white, soft radial disc
    Mesh cube, quad;
    // per-frame
    SDL_GPUCommandBuffer *cmd; SDL_GPURenderPass *pass;
    FrameParams frame; Material material; const Texture *bound_tex;
    SDL_GPUGraphicsPipeline *bound_pipe;
    Vec3 cam_right, cam_up; float sprite_lean; bool planar_next;
    unsigned draw_calls;
} Gfx;

bool gfx_init(Gfx *g, Platform *pf, int internal_w, int internal_h);
void gfx_shutdown(Gfx *g);

Mesh    gfx_mesh_create(Gfx *g, const Vertex *v, Uint32 nv, const Uint16 *idx, Uint32 ni);
Mesh    gfx_skinned_mesh_create(Gfx *g, const SkinVertex *v, Uint32 nv, const Uint16 *idx, Uint32 ni);
void    gfx_mesh_destroy(Gfx *g, Mesh *m);
Texture gfx_texture_create(Gfx *g, const unsigned char *rgba, int w, int h);
Texture gfx_texture_load(Gfx *g, const char *path, int max_size);
void    gfx_texture_destroy(Gfx *g, Texture *t);

Material material_default(void);

// World pass
void gfx_begin(Gfx *g, Platform *pf, const FrameParams *fp);
void gfx_set_material(Gfx *g, const Material *m);          // applies to following draws; NULL = default
void gfx_draw(Gfx *g, const Mesh *m, const Texture *t, Mat4 model, Vec4 tint, Vec4 uv_xform);
void gfx_draw_skinned(Gfx *g, const Mesh *m, const Texture *t, Mat4 model, Vec4 tint, const Mat4 *joints, int njoints);
void gfx_draw_box(Gfx *g, const Texture *t, Vec3 center, Vec3 size, float yaw, Vec4 tint, float uv_tile);
void gfx_draw_box_wire(Gfx *g, Vec3 center, Vec3 size, Vec4 color);
// Upright sprite: a quad w x h metres standing on `foot`, turned about Y to face the camera,
// nearest-sampled, alpha-cutout, lit like everything else. uv is the frame rect (u0 v0 u1 v1).
void gfx_draw_sprite(Gfx *g, const Texture *t, Vec3 foot, float w, float h, const float *uv, Vec4 tint, bool flip_x);
// How far sprites lean back toward a high camera (0 = upright, 1 = fully camera-facing). HD-2D uses ~0.5.
void gfx_set_sprite_lean(Gfx *g, float lean);
Texture gfx_texture_load_exact(Gfx *g, const char *path);   // no downsampling (pixel art)
// Camera-facing quads. Additive ones glow (sparks, fireflies); alpha ones shade (blob shadows, dust).
void gfx_billboard(Gfx *g, Vec3 pos, float size, Vec4 color, bool additive);
// Flat quad on the ground (y up), alpha blended: blob shadows, light pools.
void gfx_ground_quad(Gfx *g, Vec3 center, float radius, Vec4 color, bool additive);

// UI, in internal-resolution pixels
void gfx_ui_rect(Gfx *g, float x, float y, float w, float h, Vec4 color);
void gfx_ui_text(Gfx *g, float x, float y, float scale, Vec4 color, const char *text);
float gfx_ui_text_width(float scale, const char *text);
// Arbitrary quad (4 corners, clockwise or counter-clockwise) and transformed text / rings for card and rhythm UI.
void gfx_ui_quad(Gfx *g, const float *xy8, Vec4 color);
void gfx_ui_text_xf(Gfx *g, float cx, float cy, float scale, float angle, Vec4 color, const char *text);  // centred, rotated
void gfx_ui_ring(Gfx *g, float cx, float cy, float radius, float thickness, Vec4 color);
void gfx_ui_disc(Gfx *g, float cx, float cy, float radius, Vec4 color);

bool gfx_screenshot(Gfx *g, const char *path);
void gfx_end(Gfx *g, Platform *pf, const PostParams *pp, double time);
