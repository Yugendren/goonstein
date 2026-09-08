// Renderer: PS2-style forward pipeline on SDL_GPU.
// World draws go to a low-resolution offscreen target with depth, UI is drawn on top of it,
// then a post pass upscales to the swapchain with grain and vignette.
#pragma once
#include "platform.h"
#include "hmath.h"

typedef struct Vertex { float pos[3], normal[3], uv[2], color[4]; } Vertex;
typedef struct UIVertex { float pos[2], uv[2], color[4]; } UIVertex;

typedef struct Mesh { SDL_GPUBuffer *vb, *ib; Uint32 index_count; } Mesh;
typedef struct Texture { SDL_GPUTexture *tex; int w, h; } Texture;

typedef struct FrameParams {
    Mat4  view_proj;
    Vec3  fog_color;   float fog_near, fog_far;
    Vec3  light_dir;   float ambient;
    Vec3  light_color;
} FrameParams;

typedef struct PostParams { float grain, vignette, fade; } PostParams;

#define UI_MAX_VERTS 65536

typedef struct Gfx {
    SDL_GPUDevice *dev;
    int   iw, ih;                       // internal resolution
    SDL_GPUTexture *color, *depth;      // offscreen targets
    SDL_GPUGraphicsPipeline *pipe_world, *pipe_ui, *pipe_post;
    SDL_GPUSampler *samp_nearest, *samp_linear;
    SDL_GPUBuffer *ui_vb;
    SDL_GPUTransferBuffer *ui_xfer;
    UIVertex *ui_verts; Uint32 ui_count;
    Texture white;
    Mesh cube;                          // unit cube centred on origin
    // Per-frame state
    SDL_GPUCommandBuffer *cmd;
    SDL_GPURenderPass *pass;
    FrameParams frame;
    const Texture *bound_tex;
    unsigned draw_calls;
} Gfx;

bool gfx_init(Gfx *g, Platform *pf, int internal_w, int internal_h);
void gfx_shutdown(Gfx *g);

Mesh    gfx_mesh_create(Gfx *g, const Vertex *v, Uint32 nv, const Uint16 *idx, Uint32 ni);
void    gfx_mesh_destroy(Gfx *g, Mesh *m);
Texture gfx_texture_create(Gfx *g, const unsigned char *rgba, int w, int h);
Texture gfx_texture_load(Gfx *g, const char *path, int max_size);  // downsamples to PS2 sizes
void    gfx_texture_destroy(Gfx *g, Texture *t);

// World pass
void gfx_begin(Gfx *g, Platform *pf, const FrameParams *fp);
void gfx_draw(Gfx *g, const Mesh *m, const Texture *t, Mat4 model, Vec4 tint, Vec4 uv_xform);
void gfx_draw_box(Gfx *g, const Texture *t, Vec3 center, Vec3 size, float yaw, Vec4 tint, float uv_tile);
void gfx_draw_box_wire(Gfx *g, Vec3 center, Vec3 size, Vec4 color);
// Ambient for subsequent draws this frame (characters use a higher floor than the level).
void gfx_set_ambient(Gfx *g, float ambient);

// UI, in internal-resolution pixels, drawn after the world
void gfx_ui_rect(Gfx *g, float x, float y, float w, float h, Vec4 color);
void gfx_ui_text(Gfx *g, float x, float y, float scale, Vec4 color, const char *text);
float gfx_ui_text_width(float scale, const char *text);

// Save the internal-resolution frame as PNG. Call after gfx_end; it waits for the GPU.
bool gfx_screenshot(Gfx *g, const char *path);

// Finish: UI pass, post pass to swapchain, submit
void gfx_end(Gfx *g, Platform *pf, const PostParams *pp, double time);
