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
typedef struct Texture { SDL_GPUTexture *tex; int w, h; float mean[3]; } Texture;   // mean = average sRGB colour, for `look flat`

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
    // camcorder / flat / sketch layers; all zero = the plain look
    float ink_width;    // depth+luma edge width in pixels (1 = the old one-pixel ink)
    float ink_wobble;   // edge sample wobble in pixels, stepped at 8 Hz ("boiling" lines)
    float ink_luma;     // 0..1 weight of the luminance edge added to the depth edge
    float paper;        // 0..1 paper texture multiplied over the frame
    float chroma;       // chromatic aberration at the corners, in pixels
    float dither;       // ordered 4x4 dither added before the colour crunch
    float hatch;        // 0..1 screen-space hatching in the shadows
} PostParams;

#define UI_MAX_VERTS 262144
#define UI_MAX_BATCHES 2048   // text and rects alternate textures, so panels make many small batches
#define P_MAX_VERTS  (4096 * 6)
#define GFX_UI_FONTS 16   // baked VT323 sizes cached at once; the menu wants a few big ones the tool window never asks for

// ---------------------------------------------------------------- instancing
// One draw per (mesh, texture, uv mapping, glow) instead of one per object. A palm is thirty
// boxes and cylinders in a .part file and the island stands 115 of them, which used to be 3450
// draw calls for two meshes and two textures; it is now two.
//
// The instance stream is uploaded once a frame, and an upload is a copy pass, and a copy pass
// cannot run inside a render pass. So the whole frame -- the shadow pass's culled set AND the
// camera pass's, which are different sets -- has to be collected before either pass opens:
//
//     gfx_instances_begin(g);
//     props_collect(..., GFX_SET_SHADOW, sun_vp, ...);   // fills the two lists
//     props_collect(..., GFX_SET_WORLD,  view_proj, ...);
//     gfx_instances_upload(g, pf);                        // one copy pass on the frame's cmd buffer
//     gfx_shadow_begin(...); gfx_instances_draw(g, GFX_SET_SHADOW); gfx_shadow_end(g);
//     gfx_begin(...);        gfx_instances_draw(g, GFX_SET_WORLD);  ...
//
// Per-instance data is the model matrix and a tint. The tint reaches the fragment shader through
// the vertex colour, which is exactly where lit.frag already multiplies it in, so an instanced
// prop and a singly drawn one come out the same colour -- which is the point: this is a draw-call
// change, not a look change.
typedef enum GfxInstSet { GFX_SET_SHADOW = 0, GFX_SET_WORLD = 1, GFX_SET_COUNT } GfxInstSet;

#define GFX_MAX_INSTANCES    49152   // ~4 MB of stream; the island's heaviest view uses about 9000
#define GFX_MAX_INST_BATCHES 1024
#define GFX_INST_HASH        4096    // power of two, open addressed; must be > 2 * GFX_MAX_INST_BATCHES

typedef struct GfxInstance { float model[16]; float tint[4]; } GfxInstance;

typedef struct Gfx {
    SDL_GPUDevice *dev;
    int iw, ih;                                   // internal render resolution (gfx_set_render_scale)
    int uiw, uih;                                  // fixed UI coordinate space gfx_init was called with; never changes
    float render_scale; bool render_nearest;       // gfx_set_render_scale
    int tex_cap;                                   // gfx_set_texture_cap; 0 = no override
    float flat;                                    // gfx_set_flat: 0..1 blend of every material toward its texture's mean colour
    Texture paper;                                 // repeating paper grain for the sketch look (assets/textures/paper.png)
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
    struct UiFont { int px; Texture tex; void *cdata; float ascent; } fonts[GFX_UI_FONTS]; int nfonts;
    unsigned char *ttf;
    SDL_GPUTexture *tool_shot; int tool_shot_w, tool_shot_h; bool want_tool_shot;
    PVertex *p_add, *p_alpha; Uint32 p_add_count, p_alpha_count;
    // instancing: see gfx_instances_begin. Two pipelines (lit and depth-only), one growable
    // instance stream, and this frame's batches.
    SDL_GPUGraphicsPipeline *pipe_world_inst, *pipe_shadow_inst;
    SDL_GPUBuffer *inst_vb; SDL_GPUTransferBuffer *inst_xfer;
    struct GfxInstItem *inst_items; Uint32 inst_nitems;   // arrival order, sorted into batches at upload
    struct GfxInstBatch { struct InstKey { const void *mesh, *tex; float uv[4], glow[3]; Uint32 set_planar; } key;
                          const Mesh *mesh; const Texture *tex; Vec4 uv_xform; Vec3 glow;
                          Uint8 set, planar; Uint32 count, first; } inst_batches[GFX_MAX_INST_BATCHES];
    int inst_nbatches; int inst_hash[GFX_INST_HASH];
    Uint32 inst_total; bool inst_open, inst_ready;
    unsigned inst_overflow;   // instances dropped this frame because the stream was full
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
// Internal render resolution as a fraction (0.25..1) of the fixed UI space gfx_init was called
// with; the UI itself stays crisp at full size. nearest picks hard-edged upscale blit over the
// default soft bilinear one. Rebuilds the render targets, so only call this between frames.
void gfx_set_render_scale(Gfx *g, float scale, bool nearest);
// Caps every subsequently loaded texture's longest edge; 0 disables the cap. A memory/bandwidth
// lever for weak hardware, not a look.
void gfx_set_texture_cap(Gfx *g, int cap);
// `look flat`: blend every lit material toward its bound texture's mean colour, 0..1.
void gfx_set_flat(Gfx *g, float amount);

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

// Start this frame's instance collection. Clears both sets. Call once, before any render pass.
void gfx_instances_begin(Gfx *g);
// Queue one instance into `set`. mesh/tex/uv_xform/planar/glow together pick the batch; model and
// tint are what varies per instance. planar and uv_xform mirror gfx_draw_planar and gfx_draw's
// uv_xform; glow is the material emissive. Silently drops (and counts) once the stream is full.
void gfx_instance(Gfx *g, GfxInstSet set, const Mesh *mesh, const Texture *tex, Mat4 model, Vec4 tint, Vec4 uv_xform, bool planar, Vec3 glow);
// The same in two halves, for a caller that queues many instances into the same batch: find the
// batch once, then add. -1 means "no batch" and gfx_instance_add ignores it.
int  gfx_instance_batch(Gfx *g, GfxInstSet set, const Mesh *mesh, const Texture *tex, Vec4 uv_xform, bool planar, Vec3 glow);
void gfx_instance_add(Gfx *g, int batch, Mat4 model, Vec4 tint);
// Sort the collected instances into contiguous per-batch runs and upload them, in one copy pass on
// this frame's command buffer. Must be called after the last gfx_instance and before the first
// render pass of the frame.
void gfx_instances_upload(Gfx *g, Platform *pf);
// Issue one instanced draw per batch of `set`. Call inside the matching open render pass.
void gfx_instances_draw(Gfx *g, GfxInstSet set);
// How many batches and instances `set` holds (the F1 overlay and the benchmark report these).
void gfx_instances_stats(const Gfx *g, GfxInstSet set, unsigned *batches, unsigned *instances);
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
// VT323 at an explicit pixel height, in whichever UI target is current — including the game
// window, where gfx_ui_text still draws the small debug font. Falls back to that debug font when
// assets/fonts/VT323-Regular.ttf is missing.
void  gfx_ui_text_px(Gfx *g, float x, float y, int px, Vec4 color, const char *text);
float gfx_ui_text_px_width(Gfx *g, int px, const char *text);
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
