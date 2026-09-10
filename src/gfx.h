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
    Mat4  view_proj; Vec3 cam_pos, cam_right, cam_up; float time;   // seconds, for anything that animates in a shader (the sea)
    Vec3  sun_dir; float sun_intensity; Vec3 sun_color;
    Vec3  sky_ambient, ground_ambient;
    Vec3  fog_color; float fog_density, fog_height_base, fog_height_falloff, fog_scatter, fog_start;
    float toon_softness, shadow_floor, rim_power;
    // sky
    Vec3  sky_zenith, sky_horizon, sky_ground; float sun_glow, stars, sky_fog_blend;
    PointLight lights[GFX_MAX_LIGHTS]; int nlights;
} FrameParams;

// water: 1 shades the surface as sea -- two crossing ripple layers, sky at grazing angles, a sun
// glint, and a coast read from the bound texture (red = depth, green = foam), which water_origin
// locates in the world: x/y are its origin in xz and z is one over its span. See terrain_draw_water.
typedef struct Material { Vec4 tint; Vec3 emissive; Vec3 rim_color; float rim; float unlit; float water; Vec3 water_origin; } Material;

typedef struct PostParams {
    float grain, vignette, fade;
    Vec3  flash_color; float flash;
    float exposure, saturation, contrast, bloom;
    Vec3  lift, gain;
    float bloom_threshold, bloom_knee;
    float style_snap, style_outline, style_levels, style_pixel;   // world style layer: palette snap, depth-edge ink, colour levels, pixel size
} PostParams;

#define UI_MAX_VERTS 262144
#define UI_MAX_BATCHES 2048   // text and rects alternate textures, so panels make many small batches
#define P_MAX_VERTS  (4096 * 6)

typedef struct Gfx {
    SDL_GPUDevice *dev;
    int iw, ih;                                   // internal resolution
    SDL_GPUTexture *hdr, *depth, *ldr, *bloom_a, *bloom_b;
    int bw, bh;                                   // bloom resolution
    // pixel-art character layer: a small target whose texels become art pixels (see gfx_pixel_begin)
    SDL_GPUTexture *pix, *pix_depth; int pw, ph, pixel_scale;
    float pix_levels, pix_outline, pix_palette, pix_inner;
    SDL_GPUGraphicsPipeline *pipe_pixcomp;
    // sun shadow map (depth only, orthographic), drawn before the main pass
    SDL_GPUTexture *shadow_tex; int shadow_size; SDL_GPUGraphicsPipeline *pipe_shadow, *pipe_shadow_skin; bool in_shadow;
    Mat4 sun_vp; float shadow_strength, shadow_bias; bool shadow_valid;
    Mat4 main_vp; float pix_off_x, pix_off_y; bool in_pix;
    // portrait camera: a character head rendered through the pixel pass into a small UI texture
    SDL_GPUTexture *por_hdr, *por_depth, *por_comp, *por_comp_depth; int por_size; Texture portrait; bool in_portrait; Vec3 por_backdrop_lin;
    SDL_GPUTextureFormat swap_format;
    SDL_GPUGraphicsPipeline *pipe_world, *pipe_skin, *pipe_sky, *pipe_particle_add, *pipe_particle_alpha,
                            *pipe_bright, *pipe_blur, *pipe_post, *pipe_ui, *pipe_blit;
    SDL_GPUSampler *samp_nearest, *samp_linear, *samp_clamp;
    SDL_GPUBuffer *ui_vb, *p_vb; SDL_GPUTransferBuffer *ui_xfer, *p_xfer;
    UIVertex *ui_verts; Uint32 ui_count;
    struct { const Texture *tex; Uint32 start, count; } ui_batches[UI_MAX_BATCHES]; int ui_nbatches;
    // second UI list for the debugger window
    UIVertex *ui2_verts; Uint32 ui2_count; struct { const Texture *tex; Uint32 start, count; } ui2_batches[UI_MAX_BATCHES]; int ui2_nbatches;
    SDL_GPUBuffer *ui2_vb; SDL_GPUTransferBuffer *ui2_xfer; SDL_GPUGraphicsPipeline *pipe_ui_swap;
    int ui_target;   // 0 = game screen, 1 = debugger window
    float ui2_scale, ui2_ox, ui2_oy;   // transform applied to tool-window UI coordinates (gfx_ui_set_transform)
    // tool window font: VT323 baked at a few pixel sizes on demand
    struct UiFont { int px; Texture tex; void *cdata; float ascent; } fonts[8]; int nfonts;
    unsigned char *ttf;
    SDL_GPUTexture *tool_shot; int tool_shot_w, tool_shot_h; bool want_tool_shot;
    PVertex *p_add, *p_alpha; Uint32 p_add_count, p_alpha_count;
    Texture white, soft;                          // 1x1 white, soft radial disc
    Mesh cube, quad;
    // per-frame
    SDL_GPUCommandBuffer *cmd; SDL_GPURenderPass *pass;
    FrameParams frame; Material material; const Texture *bound_tex;
    SDL_GPUGraphicsPipeline *bound_pipe;
    Vec3 cam_right, cam_up; float sprite_lean; bool planar_next;
    unsigned char frame_uniforms[2048]; Uint32 frame_uniforms_size;   // re-pushed when a pass reopens
    Vec4 palette[64]; int npalette; long long palette_mtime; Uint64 palette_check;   // assets/palette.txt
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
// Draw with world-space planar UVs at `tile` repeats per metre, ignoring the mesh's own uvs
// (the editor's shape .objs have none). Same as gfx_draw_box's uv_tile path.
void gfx_draw_planar(Gfx *g, const Mesh *m, const Texture *t, Mat4 model, Vec4 tint, float tile);
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
// Route subsequent UI calls to the game screen (0) or the tool window (1). In the tool window,
// text is drawn with the VT323 font (scale 1 = 17 px) instead of the debug font.
void gfx_ui_target(Gfx *g, int target);
// Scale and offset applied to all tool-window UI coordinates (for panels laid out in a fixed logical size).
void gfx_ui_set_transform(Gfx *g, float scale, float ox, float oy);
// Font metrics for layout: line height of text at `scale` for the current target.
float gfx_ui_line_h(float scale);
// Save the tool window's UI as a PNG (rendered offscreen at the next gfx_end).
void gfx_tool_screenshot_request(Gfx *g, int w, int h);
bool gfx_tool_screenshot_save(Gfx *g, const char *path);
// Textured UI image (nearest sampled, alpha blended). uv = u0 v0 u1 v1, or NULL for the whole texture.
void gfx_ui_image(Gfx *g, const Texture *t, float x, float y, float w, float h, const float *uv, Vec4 color);

// Pixel-art layer. Between begin and end, draws go to a target 1/scale the size of the screen
// (scale 0 disables the layer: draws stay in the main pass). view_proj is the camera snapped
// to that target's texel grid and off_x/off_y the remaining sub-texel shift in texels; end
// composites the layer over the world with a one-pixel outline and crunched colours, depth-tested.
void gfx_set_pixel_look(Gfx *g, int scale, float levels, float outline, float palette, float inner);
// Sun shadows: call before gfx_begin. Everything drawn between begin and end goes into the shadow
// map through the sun's orthographic view_proj; the main pass then darkens what the sun cannot see.
// strength 0 turns shadows off for the frame.
void gfx_shadow_begin(Gfx *g, Platform *pf, Mat4 sun_vp, float strength, float bias);
void gfx_shadow_end(Gfx *g);
void gfx_pixel_begin(Gfx *g, Mat4 view_proj, float off_x, float off_y);
// Portrait: call before gfx_begin. Opens a pass on a size x size art-pixel target using fp's
// lighting and view; draw the character; end composites it (outline, palette) and tone-maps it into
// g->portrait, a UI texture. backdrop is the colour behind the character (sRGB-ish 0..1).
void gfx_portrait_begin(Gfx *g, Platform *pf, const FrameParams *fp, int size, Vec3 backdrop);
void gfx_portrait_end(Gfx *g);
void gfx_pixel_end(Gfx *g);
// Reload assets/palette.txt if it changed (called once a frame; checks the file once a second).
void gfx_palette_update(Gfx *g);
bool gfx_screenshot(Gfx *g, const char *path);
void gfx_end(Gfx *g, Platform *pf, const PostParams *pp, double time);
