#include "gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "vendor/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "vendor/stb_image_write.h"
#define STB_EASY_FONT_IMPLEMENTATION
#include "vendor/stb_easy_font.h"

#define HDR_FMT SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT
#define LDR_FMT SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
#define DEPTH_FMT SDL_GPU_TEXTUREFORMAT_D32_FLOAT

// ---------------------------------------------------------------- uniform layouts (std140)

typedef struct VSUniforms { Mat4 view_proj, model; Vec4 uv_xform; } VSUniforms;
typedef struct FrameUniforms {
    Vec4 cam_pos, sun_dir, sun_color, sky_ambient, ground_ambient, fog_color, fog_height, toon;
    Vec4 lights_pos[GFX_MAX_LIGHTS], lights_color[GFX_MAX_LIGHTS];
    Sint32 counts[4];
} FrameUniforms;
typedef struct MaterialUniforms { Vec4 tint, emissive, rim; } MaterialUniforms;
typedef struct SkyUniforms { Mat4 inv_view_proj; Vec4 cam_pos, sun_dir, sun_color, zenith, horizon, ground, params, fog_color; } SkyUniforms;
typedef struct PostUniforms { Vec4 params, res, flash, grade, lift, gain; } PostUniforms;

// ---------------------------------------------------------------- shaders and helpers

static SDL_GPUShader *load_shader(Gfx *g, const char *name, SDL_GPUShaderStage stage, Uint32 samplers, Uint32 uniforms) {
    SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(g->dev);
    const char *ext, *entry; SDL_GPUShaderFormat fmt;
    if (formats & SDL_GPU_SHADERFORMAT_MSL)        { ext = "msl";  entry = "main0"; fmt = SDL_GPU_SHADERFORMAT_MSL; }
    else if (formats & SDL_GPU_SHADERFORMAT_SPIRV) { ext = "spv";  entry = "main";  fmt = SDL_GPU_SHADERFORMAT_SPIRV; }
    else if (formats & SDL_GPU_SHADERFORMAT_DXIL)  { ext = "dxil"; entry = "main";  fmt = SDL_GPU_SHADERFORMAT_DXIL; }
    else { SDL_SetError("no supported shader format"); return NULL; }
    char path[512]; snprintf(path, sizeof path, "%s/shaders/%s.%s", HOLLOW_ASSET_DIR, name, ext);
    size_t size = 0; void *code = SDL_LoadFile(path, &size);
    if (!code) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "shader missing: %s", path); return NULL; }
    SDL_GPUShader *sh = SDL_CreateGPUShader(g->dev, &(SDL_GPUShaderCreateInfo){
        .code = code, .code_size = size, .entrypoint = entry, .format = fmt, .stage = stage,
        .num_samplers = samplers, .num_uniform_buffers = uniforms });
    SDL_free(code);
    if (!sh) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "shader %s: %s", name, SDL_GetError());
    return sh;
}

static SDL_GPUTexture *make_target(Gfx *g, SDL_GPUTextureFormat fmt, int w, int h, bool depth) {
    return SDL_CreateGPUTexture(g->dev, &(SDL_GPUTextureCreateInfo){
        .type = SDL_GPU_TEXTURETYPE_2D, .format = fmt,
        .usage = depth ? SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET : (SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER),
        .width = (Uint32)w, .height = (Uint32)h, .layer_count_or_depth = 1, .num_levels = 1 });
}

static bool upload(Gfx *g, SDL_GPUBuffer *dst, const void *data, Uint32 size) {
    SDL_GPUTransferBuffer *xfer = SDL_CreateGPUTransferBuffer(g->dev, &(SDL_GPUTransferBufferCreateInfo){ .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = size });
    if (!xfer) return false;
    void *map = SDL_MapGPUTransferBuffer(g->dev, xfer, false); memcpy(map, data, size); SDL_UnmapGPUTransferBuffer(g->dev, xfer);
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g->dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUBuffer(cp, &(SDL_GPUTransferBufferLocation){ .transfer_buffer = xfer }, &(SDL_GPUBufferRegion){ .buffer = dst, .size = size }, false);
    SDL_EndGPUCopyPass(cp); SDL_SubmitGPUCommandBuffer(cmd); SDL_ReleaseGPUTransferBuffer(g->dev, xfer);
    return true;
}

static Mesh mesh_create_raw(Gfx *g, const void *v, Uint32 vsize, const Uint16 *idx, Uint32 ni) {
    Mesh m = {0};
    m.vb = SDL_CreateGPUBuffer(g->dev, &(SDL_GPUBufferCreateInfo){ .usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = vsize });
    m.ib = SDL_CreateGPUBuffer(g->dev, &(SDL_GPUBufferCreateInfo){ .usage = SDL_GPU_BUFFERUSAGE_INDEX, .size = ni * (Uint32)sizeof *idx });
    upload(g, m.vb, v, vsize); upload(g, m.ib, idx, ni * (Uint32)sizeof *idx);
    m.index_count = ni;
    return m;
}
Mesh gfx_mesh_create(Gfx *g, const Vertex *v, Uint32 nv, const Uint16 *idx, Uint32 ni) { return mesh_create_raw(g, v, nv * (Uint32)sizeof *v, idx, ni); }
Mesh gfx_skinned_mesh_create(Gfx *g, const SkinVertex *v, Uint32 nv, const Uint16 *idx, Uint32 ni) { return mesh_create_raw(g, v, nv * (Uint32)sizeof *v, idx, ni); }
void gfx_mesh_destroy(Gfx *g, Mesh *m) {
    if (m->vb) SDL_ReleaseGPUBuffer(g->dev, m->vb);
    if (m->ib) SDL_ReleaseGPUBuffer(g->dev, m->ib);
    memset(m, 0, sizeof *m);
}

Texture gfx_texture_create(Gfx *g, const unsigned char *rgba, int w, int h) {
    Texture t = { .w = w, .h = h };
    t.tex = SDL_CreateGPUTexture(g->dev, &(SDL_GPUTextureCreateInfo){
        .type = SDL_GPU_TEXTURETYPE_2D, .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = (Uint32)w, .height = (Uint32)h, .layer_count_or_depth = 1, .num_levels = 1 });
    Uint32 size = (Uint32)(w * h * 4);
    SDL_GPUTransferBuffer *xfer = SDL_CreateGPUTransferBuffer(g->dev, &(SDL_GPUTransferBufferCreateInfo){ .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = size });
    void *map = SDL_MapGPUTransferBuffer(g->dev, xfer, false); memcpy(map, rgba, size); SDL_UnmapGPUTransferBuffer(g->dev, xfer);
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g->dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUTexture(cp, &(SDL_GPUTextureTransferInfo){ .transfer_buffer = xfer }, &(SDL_GPUTextureRegion){ .texture = t.tex, .w = (Uint32)w, .h = (Uint32)h, .d = 1 }, false);
    SDL_EndGPUCopyPass(cp); SDL_SubmitGPUCommandBuffer(cmd); SDL_ReleaseGPUTransferBuffer(g->dev, xfer);
    return t;
}

Texture gfx_texture_load(Gfx *g, const char *path, int max_size) {
    int w, h, n; unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    if (!px) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "texture load failed: %s", path); return g->white; }
    int f = 1; while ((w / f) > max_size || (h / f) > max_size) f *= 2;
    int dw = w / f, dh = h / f; unsigned char *out = px;
    if (f > 1) {
        out = malloc((size_t)dw * dh * 4);
        for (int y = 0; y < dh; y++) for (int x = 0; x < dw; x++) for (int c = 0; c < 4; c++) {
            int sum = 0;
            for (int yy = 0; yy < f; yy++) for (int xx = 0; xx < f; xx++) sum += px[((y * f + yy) * w + (x * f + xx)) * 4 + c];
            out[(y * dw + x) * 4 + c] = (unsigned char)(sum / (f * f));
        }
    }
    Texture t = gfx_texture_create(g, out, dw, dh);
    if (out != px) free(out);
    stbi_image_free(px);
    return t;
}

void gfx_texture_destroy(Gfx *g, Texture *t) {
    if (t->tex && t->tex != g->white.tex) SDL_ReleaseGPUTexture(g->dev, t->tex);
    memset(t, 0, sizeof *t);
}

Material material_default(void) { return (Material){ .tint = v4(1, 1, 1, 1), .emissive = v3(0, 0, 0), .rim_color = v3(1, 1, 1), .rim = 0 }; }

// ---------------------------------------------------------------- init

static Mesh make_cube(Gfx *g) {
    static const float N[6][3] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    static const float U[6][3] = {{1,0,0},{-1,0,0},{0,0,-1},{0,0,1},{1,0,0},{1,0,0}};
    static const float V[6][3] = {{0,1,0},{0,1,0},{0,1,0},{0,1,0},{0,0,-1},{0,0,1}};
    Vertex v[24]; Uint16 idx[36];
    for (int f = 0; f < 6; f++) {
        for (int i = 0; i < 4; i++) {
            float su = (i == 1 || i == 2) ? 0.5f : -0.5f, sv = (i >= 2) ? 0.5f : -0.5f;
            Vertex *p = &v[f * 4 + i];
            for (int k = 0; k < 3; k++) { p->pos[k] = N[f][k] * 0.5f + U[f][k] * su + V[f][k] * sv; p->normal[k] = N[f][k]; }
            p->uv[0] = su + 0.5f; p->uv[1] = 0.5f - sv;
            p->color[0] = p->color[1] = p->color[2] = p->color[3] = 1.0f;
        }
        Uint16 b = (Uint16)(f * 4); Uint16 *o = &idx[f * 6];
        o[0] = b; o[1] = b + 1; o[2] = b + 2; o[3] = b; o[4] = b + 2; o[5] = b + 3;
    }
    return gfx_mesh_create(g, v, 24, idx, 36);
}

static Texture make_soft_disc(Gfx *g) {
    const int S = 64; unsigned char px[64 * 64 * 4];
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        float dx = (x + 0.5f) / S - 0.5f, dy = (y + 0.5f) / S - 0.5f;
        float r = sqrtf(dx * dx + dy * dy) * 2.0f;
        float a = clampf(1.0f - r, 0, 1); a = a * a * (3 - 2 * a);
        int i = (y * S + x) * 4; px[i] = px[i + 1] = px[i + 2] = 255; px[i + 3] = (unsigned char)(a * 255);
    }
    return gfx_texture_create(g, px, S, S);
}

typedef struct PipeDesc {
    SDL_GPUShader *vs, *fs;
    const SDL_GPUVertexBufferDescription *vb; const SDL_GPUVertexAttribute *attrs; Uint32 nattrs;
    SDL_GPUTextureFormat color_fmt; bool depth_test, depth_write; SDL_GPUCompareOp cmp; SDL_GPUCullMode cull;
    int blend;   // 0 none, 1 alpha, 2 additive
} PipeDesc;

static SDL_GPUGraphicsPipeline *make_pipe(Gfx *g, const PipeDesc *d) {
    SDL_GPUColorTargetDescription ct = { .format = d->color_fmt };
    if (d->blend) {
        ct.blend_state = (SDL_GPUColorTargetBlendState){ .enable_blend = true,
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = d->blend == 2 ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE, .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD };
    }
    SDL_GPUGraphicsPipelineCreateInfo ci = {
        .vertex_shader = d->vs, .fragment_shader = d->fs,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = { .fill_mode = SDL_GPU_FILLMODE_FILL, .cull_mode = d->cull, .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE },
        .depth_stencil_state = { .enable_depth_test = d->depth_test, .enable_depth_write = d->depth_write, .compare_op = d->cmp },
        .target_info = { .color_target_descriptions = &ct, .num_color_targets = 1,
                         .has_depth_stencil_target = d->depth_test, .depth_stencil_format = DEPTH_FMT } };
    if (d->vb) ci.vertex_input_state = (SDL_GPUVertexInputState){ .vertex_buffer_descriptions = d->vb, .num_vertex_buffers = 1, .vertex_attributes = d->attrs, .num_vertex_attributes = d->nattrs };
    SDL_GPUGraphicsPipeline *p = SDL_CreateGPUGraphicsPipeline(g->dev, &ci);
    if (!p) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "pipeline: %s", SDL_GetError());
    return p;
}

bool gfx_init(Gfx *g, Platform *pf, int iw, int ih) {
    memset(g, 0, sizeof *g);
    g->dev = pf->gpu; g->iw = iw; g->ih = ih; g->bw = iw / 4; g->bh = ih / 4;
    g->hdr = make_target(g, HDR_FMT, iw, ih, false);
    g->depth = make_target(g, DEPTH_FMT, iw, ih, true);
    g->ldr = make_target(g, LDR_FMT, iw, ih, false);
    g->bloom_a = make_target(g, HDR_FMT, g->bw, g->bh, false);
    g->bloom_b = make_target(g, HDR_FMT, g->bw, g->bh, false);
    if (!g->hdr || !g->depth || !g->ldr || !g->bloom_a || !g->bloom_b) return false;
    g->swap_format = SDL_GetGPUSwapchainTextureFormat(g->dev, pf->window);

    g->samp_nearest = SDL_CreateGPUSampler(g->dev, &(SDL_GPUSamplerCreateInfo){ .min_filter = SDL_GPU_FILTER_NEAREST, .mag_filter = SDL_GPU_FILTER_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT, .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT });
    g->samp_linear = SDL_CreateGPUSampler(g->dev, &(SDL_GPUSamplerCreateInfo){ .min_filter = SDL_GPU_FILTER_LINEAR, .mag_filter = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR, .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT, .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT });
    g->samp_clamp = SDL_CreateGPUSampler(g->dev, &(SDL_GPUSamplerCreateInfo){ .min_filter = SDL_GPU_FILTER_LINEAR, .mag_filter = SDL_GPU_FILTER_LINEAR,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE, .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE });

    SDL_GPUShader *world_vs = load_shader(g, "world.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader *skin_vs = load_shader(g, "skin.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 2);
    SDL_GPUShader *lit_fs = load_shader(g, "lit.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 2);
    SDL_GPUShader *sky_vs = load_shader(g, "sky.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
    SDL_GPUShader *sky_fs = load_shader(g, "sky.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
    SDL_GPUShader *part_vs = load_shader(g, "particle.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader *part_fs = load_shader(g, "particle.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
    SDL_GPUShader *fs_vs = load_shader(g, "fs.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
    SDL_GPUShader *bright_fs = load_shader(g, "bright.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    SDL_GPUShader *blur_fs = load_shader(g, "blur.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    SDL_GPUShader *post_fs = load_shader(g, "post.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);
    SDL_GPUShader *blit_fs = load_shader(g, "blit.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
    SDL_GPUShader *ui_vs = load_shader(g, "ui.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader *ui_fs = load_shader(g, "ui.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
    SDL_GPUShader *all[] = { world_vs, skin_vs, lit_fs, sky_vs, sky_fs, part_vs, part_fs, fs_vs, bright_fs, blur_fs, post_fs, blit_fs, ui_vs, ui_fs };
    for (size_t i = 0; i < sizeof all / sizeof *all; i++) if (!all[i]) return false;

    SDL_GPUVertexAttribute world_attrs[] = {
        { .location = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 0 }, { .location = 1, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 12 },
        { .location = 2, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 24 }, { .location = 3, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 32 } };
    SDL_GPUVertexAttribute skin_attrs[] = {
        { .location = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 0 }, { .location = 1, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 12 },
        { .location = 2, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 24 }, { .location = 3, .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4, .offset = 32 },
        { .location = 4, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 36 } };
    SDL_GPUVertexAttribute p_attrs[] = {
        { .location = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 0 }, { .location = 1, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 12 },
        { .location = 2, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 20 } };
    SDL_GPUVertexAttribute ui_attrs[] = {
        { .location = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 0 }, { .location = 1, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 8 },
        { .location = 2, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 16 } };
    SDL_GPUVertexBufferDescription world_vb = { .pitch = sizeof(Vertex) }, skin_vb = { .pitch = sizeof(SkinVertex) }, p_vb = { .pitch = sizeof(PVertex) }, ui_vb = { .pitch = sizeof(UIVertex) };

    g->pipe_world = make_pipe(g, &(PipeDesc){ world_vs, lit_fs, &world_vb, world_attrs, 4, HDR_FMT, true, true, SDL_GPU_COMPAREOP_LESS, SDL_GPU_CULLMODE_BACK, 0 });
    g->pipe_skin = make_pipe(g, &(PipeDesc){ skin_vs, lit_fs, &skin_vb, skin_attrs, 5, HDR_FMT, true, true, SDL_GPU_COMPAREOP_LESS, SDL_GPU_CULLMODE_BACK, 0 });
    g->pipe_sky = make_pipe(g, &(PipeDesc){ sky_vs, sky_fs, NULL, NULL, 0, HDR_FMT, true, false, SDL_GPU_COMPAREOP_LESS_OR_EQUAL, SDL_GPU_CULLMODE_NONE, 0 });
    g->pipe_particle_add = make_pipe(g, &(PipeDesc){ part_vs, part_fs, &p_vb, p_attrs, 3, HDR_FMT, true, false, SDL_GPU_COMPAREOP_LESS, SDL_GPU_CULLMODE_NONE, 2 });
    g->pipe_particle_alpha = make_pipe(g, &(PipeDesc){ part_vs, part_fs, &p_vb, p_attrs, 3, HDR_FMT, true, false, SDL_GPU_COMPAREOP_LESS, SDL_GPU_CULLMODE_NONE, 1 });
    g->pipe_bright = make_pipe(g, &(PipeDesc){ fs_vs, bright_fs, NULL, NULL, 0, HDR_FMT, false, false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_CULLMODE_NONE, 0 });
    g->pipe_blur = make_pipe(g, &(PipeDesc){ fs_vs, blur_fs, NULL, NULL, 0, HDR_FMT, false, false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_CULLMODE_NONE, 0 });
    g->pipe_post = make_pipe(g, &(PipeDesc){ fs_vs, post_fs, NULL, NULL, 0, LDR_FMT, false, false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_CULLMODE_NONE, 0 });
    g->pipe_ui = make_pipe(g, &(PipeDesc){ ui_vs, ui_fs, &ui_vb, ui_attrs, 3, LDR_FMT, false, false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_CULLMODE_NONE, 1 });
    g->pipe_blit = make_pipe(g, &(PipeDesc){ fs_vs, blit_fs, NULL, NULL, 0, g->swap_format, false, false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_CULLMODE_NONE, 0 });
    for (size_t i = 0; i < sizeof all / sizeof *all; i++) SDL_ReleaseGPUShader(g->dev, all[i]);
    if (!g->pipe_world || !g->pipe_skin || !g->pipe_sky || !g->pipe_particle_add || !g->pipe_particle_alpha || !g->pipe_bright || !g->pipe_blur || !g->pipe_post || !g->pipe_ui || !g->pipe_blit) return false;

    g->ui_vb = SDL_CreateGPUBuffer(g->dev, &(SDL_GPUBufferCreateInfo){ .usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = UI_MAX_VERTS * sizeof(UIVertex) });
    g->ui_xfer = SDL_CreateGPUTransferBuffer(g->dev, &(SDL_GPUTransferBufferCreateInfo){ .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = UI_MAX_VERTS * sizeof(UIVertex) });
    g->ui_verts = malloc(UI_MAX_VERTS * sizeof(UIVertex));
    g->p_vb = SDL_CreateGPUBuffer(g->dev, &(SDL_GPUBufferCreateInfo){ .usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = 2 * P_MAX_VERTS * sizeof(PVertex) });
    g->p_xfer = SDL_CreateGPUTransferBuffer(g->dev, &(SDL_GPUTransferBufferCreateInfo){ .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = 2 * P_MAX_VERTS * sizeof(PVertex) });
    g->p_add = malloc(P_MAX_VERTS * sizeof(PVertex)); g->p_alpha = malloc(P_MAX_VERTS * sizeof(PVertex));

    unsigned char white[4] = {255, 255, 255, 255};
    g->white = gfx_texture_create(g, white, 1, 1);
    g->soft = make_soft_disc(g);
    g->cube = make_cube(g);
    g->material = material_default();
    return true;
}

void gfx_shutdown(Gfx *g) {
    gfx_mesh_destroy(g, &g->cube);
    gfx_texture_destroy(g, &g->soft); gfx_texture_destroy(g, &g->white);
    free(g->ui_verts); free(g->p_add); free(g->p_alpha);
    SDL_ReleaseGPUTransferBuffer(g->dev, g->ui_xfer); SDL_ReleaseGPUBuffer(g->dev, g->ui_vb);
    SDL_ReleaseGPUTransferBuffer(g->dev, g->p_xfer); SDL_ReleaseGPUBuffer(g->dev, g->p_vb);
    SDL_GPUGraphicsPipeline *pipes[] = { g->pipe_world, g->pipe_skin, g->pipe_sky, g->pipe_particle_add, g->pipe_particle_alpha, g->pipe_bright, g->pipe_blur, g->pipe_post, g->pipe_ui, g->pipe_blit };
    for (size_t i = 0; i < sizeof pipes / sizeof *pipes; i++) SDL_ReleaseGPUGraphicsPipeline(g->dev, pipes[i]);
    SDL_ReleaseGPUSampler(g->dev, g->samp_nearest); SDL_ReleaseGPUSampler(g->dev, g->samp_linear); SDL_ReleaseGPUSampler(g->dev, g->samp_clamp);
    SDL_ReleaseGPUTexture(g->dev, g->hdr); SDL_ReleaseGPUTexture(g->dev, g->depth); SDL_ReleaseGPUTexture(g->dev, g->ldr);
    SDL_ReleaseGPUTexture(g->dev, g->bloom_a); SDL_ReleaseGPUTexture(g->dev, g->bloom_b);
}

// ---------------------------------------------------------------- world pass

static void push_material(Gfx *g, Vec4 tint) {
    const Material *m = &g->material;
    MaterialUniforms u = { .tint = v4(tint.x * m->tint.x, tint.y * m->tint.y, tint.z * m->tint.z, tint.w * m->tint.w),
                           .emissive = v4(m->emissive.x, m->emissive.y, m->emissive.z, 0), .rim = v4(m->rim_color.x, m->rim_color.y, m->rim_color.z, m->rim) };
    SDL_PushGPUFragmentUniformData(g->cmd, 1, &u, sizeof u);
}

void gfx_begin(Gfx *g, Platform *pf, const FrameParams *fp) {
    g->frame = *fp; g->ui_count = 0; g->p_add_count = g->p_alpha_count = 0; g->draw_calls = 0;
    g->bound_tex = NULL; g->bound_pipe = NULL; g->pass = NULL; g->cmd = pf->cmd;
    g->material = material_default();
    g->cam_right = fp->cam_right; g->cam_up = fp->cam_up;
    if (!pf->cmd) return;
    SDL_GPUColorTargetInfo ct = { .texture = g->hdr, .load_op = SDL_GPU_LOADOP_CLEAR, .store_op = SDL_GPU_STOREOP_STORE, .clear_color = { fp->fog_color.x, fp->fog_color.y, fp->fog_color.z, 1 } };
    SDL_GPUDepthStencilTargetInfo dt = { .texture = g->depth, .load_op = SDL_GPU_LOADOP_CLEAR, .store_op = SDL_GPU_STOREOP_DONT_CARE,
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE, .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE, .clear_depth = 1.0f, .cycle = true };
    g->pass = SDL_BeginGPURenderPass(pf->cmd, &ct, 1, &dt);

    FrameUniforms u = {
        .cam_pos = v4(fp->cam_pos.x, fp->cam_pos.y, fp->cam_pos.z, 0),
        .sun_dir = v4(fp->sun_dir.x, fp->sun_dir.y, fp->sun_dir.z, fp->sun_intensity),
        .sun_color = v4(fp->sun_color.x, fp->sun_color.y, fp->sun_color.z, 0),
        .sky_ambient = v4(fp->sky_ambient.x, fp->sky_ambient.y, fp->sky_ambient.z, 0),
        .ground_ambient = v4(fp->ground_ambient.x, fp->ground_ambient.y, fp->ground_ambient.z, 0),
        .fog_color = v4(fp->fog_color.x, fp->fog_color.y, fp->fog_color.z, fp->fog_density),
        .fog_height = v4(fp->fog_height_base, fp->fog_height_falloff, fp->fog_scatter, fp->fog_start),
        .toon = v4(fp->toon_softness, fp->shadow_floor, fp->rim_power, 0),
        .counts = { fp->nlights > GFX_MAX_LIGHTS ? GFX_MAX_LIGHTS : fp->nlights, 0, 0, 0 } };
    for (int i = 0; i < u.counts[0]; i++) {
        const PointLight *l = &fp->lights[i];
        u.lights_pos[i] = v4(l->pos.x, l->pos.y, l->pos.z, l->radius);
        u.lights_color[i] = v4(l->color.x * l->intensity, l->color.y * l->intensity, l->color.z * l->intensity, 0);
    }
    SDL_PushGPUFragmentUniformData(pf->cmd, 0, &u, sizeof u);
    push_material(g, v4(1, 1, 1, 1));
}

void gfx_set_material(Gfx *g, const Material *m) { g->material = m ? *m : material_default(); }

static void bind_pipe(Gfx *g, SDL_GPUGraphicsPipeline *p) {
    if (g->bound_pipe != p) { SDL_BindGPUGraphicsPipeline(g->pass, p); g->bound_pipe = p; g->bound_tex = NULL; }
}
static void bind_tex(Gfx *g, const Texture *t, SDL_GPUSampler *s) {
    if (t != g->bound_tex) { SDL_BindGPUFragmentSamplers(g->pass, 0, &(SDL_GPUTextureSamplerBinding){ .texture = t->tex, .sampler = s }, 1); g->bound_tex = t; }
}

void gfx_draw(Gfx *g, const Mesh *m, const Texture *t, Mat4 model, Vec4 tint, Vec4 uv_xform) {
    if (!g->pass) return;
    bind_pipe(g, g->pipe_world);
    VSUniforms u = { g->frame.view_proj, model, uv_xform };
    SDL_PushGPUVertexUniformData(g->cmd, 0, &u, sizeof u);
    push_material(g, tint);
    bind_tex(g, t, g->samp_linear);
    SDL_BindGPUVertexBuffers(g->pass, 0, &(SDL_GPUBufferBinding){ .buffer = m->vb }, 1);
    SDL_BindGPUIndexBuffer(g->pass, &(SDL_GPUBufferBinding){ .buffer = m->ib }, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(g->pass, m->index_count, 1, 0, 0, 0);
    g->draw_calls++;
}

void gfx_draw_skinned(Gfx *g, const Mesh *m, const Texture *t, Mat4 model, Vec4 tint, const Mat4 *joints, int njoints) {
    if (!g->pass) return;
    bind_pipe(g, g->pipe_skin);
    VSUniforms u = { g->frame.view_proj, model, v4(1, 1, 0, 0) };
    SDL_PushGPUVertexUniformData(g->cmd, 0, &u, sizeof u);
    static Mat4 tmp[64];
    memset(tmp, 0, sizeof tmp);
    memcpy(tmp, joints, (size_t)(njoints > 64 ? 64 : njoints) * sizeof(Mat4));
    SDL_PushGPUVertexUniformData(g->cmd, 1, tmp, sizeof tmp);
    push_material(g, tint);
    bind_tex(g, t, g->samp_linear);
    SDL_BindGPUVertexBuffers(g->pass, 0, &(SDL_GPUBufferBinding){ .buffer = m->vb }, 1);
    SDL_BindGPUIndexBuffer(g->pass, &(SDL_GPUBufferBinding){ .buffer = m->ib }, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(g->pass, m->index_count, 1, 0, 0, 0);
    g->draw_calls++;
}

void gfx_draw_box(Gfx *g, const Texture *t, Vec3 center, Vec3 size, float yaw, Vec4 tint, float uv_tile) {
    Vec4 xf = uv_tile > 0 ? v4(-uv_tile, 0, 0, 0) : v4(1, 1, 0, 0);
    gfx_draw(g, &g->cube, t, m4_trs(center, yaw, size), tint, xf);
}

void gfx_draw_box_wire(Gfx *g, Vec3 c, Vec3 s, Vec4 color) {
    const float th = 0.02f;
    Material m = material_default(); m.emissive = v3(color.x, color.y, color.z);
    Material saved = g->material; g->material = m;
    float hx = s.x * 0.5f, hy = s.y * 0.5f, hz = s.z * 0.5f;
    for (int i = 0; i < 4; i++) {
        float a = (i & 1) ? 1 : -1, b = (i & 2) ? 1 : -1;
        gfx_draw(g, &g->cube, &g->white, m4_trs(v3(c.x, c.y + a * hy, c.z + b * hz), 0, v3(s.x, th, th)), color, v4(1, 1, 0, 0));
        gfx_draw(g, &g->cube, &g->white, m4_trs(v3(c.x + a * hx, c.y, c.z + b * hz), 0, v3(th, s.y, th)), color, v4(1, 1, 0, 0));
        gfx_draw(g, &g->cube, &g->white, m4_trs(v3(c.x + a * hx, c.y + b * hy, c.z), 0, v3(th, th, s.z)), color, v4(1, 1, 0, 0));
    }
    g->material = saved;
}

// ---------------------------------------------------------------- billboards and decals

static void p_push(PVertex *list, Uint32 *count, Vec3 p, float u, float v, Vec4 c) {
    if (*count >= P_MAX_VERTS) return;
    PVertex *o = &list[(*count)++];
    o->pos[0] = p.x; o->pos[1] = p.y; o->pos[2] = p.z; o->uv[0] = u; o->uv[1] = v;
    o->color[0] = c.x; o->color[1] = c.y; o->color[2] = c.z; o->color[3] = c.w;
}
static void p_quad(PVertex *list, Uint32 *count, Vec3 c, Vec3 r, Vec3 u, Vec4 color) {
    Vec3 a = v3_sub(v3_sub(c, r), u), b = v3_sub(v3_add(c, r), u), d = v3_add(v3_add(c, r), u), e = v3_add(v3_sub(c, r), u);
    p_push(list, count, a, 0, 0, color); p_push(list, count, b, 1, 0, color); p_push(list, count, d, 1, 1, color);
    p_push(list, count, a, 0, 0, color); p_push(list, count, d, 1, 1, color); p_push(list, count, e, 0, 1, color);
}

void gfx_billboard(Gfx *g, Vec3 pos, float size, Vec4 color, bool additive) {
    Vec3 r = v3_scale(g->cam_right, size * 0.5f), u = v3_scale(g->cam_up, size * 0.5f);
    if (additive) p_quad(g->p_add, &g->p_add_count, pos, r, u, color);
    else p_quad(g->p_alpha, &g->p_alpha_count, pos, r, u, color);
}

void gfx_ground_quad(Gfx *g, Vec3 c, float radius, Vec4 color, bool additive) {
    Vec3 r = v3(radius, 0, 0), u = v3(0, 0, radius);
    if (additive) p_quad(g->p_add, &g->p_add_count, c, r, u, color);
    else p_quad(g->p_alpha, &g->p_alpha_count, c, r, u, color);
}

// ---------------------------------------------------------------- ui

static void ui_push(Gfx *g, float x, float y, float u, float v, Vec4 c) {
    if (g->ui_count >= UI_MAX_VERTS) return;
    UIVertex *o = &g->ui_verts[g->ui_count++];
    o->pos[0] = x; o->pos[1] = y; o->uv[0] = u; o->uv[1] = v;
    o->color[0] = c.x; o->color[1] = c.y; o->color[2] = c.z; o->color[3] = c.w;
}
void gfx_ui_rect(Gfx *g, float x, float y, float w, float h, Vec4 c) {
    ui_push(g, x, y, 0, 0, c); ui_push(g, x + w, y, 1, 0, c); ui_push(g, x + w, y + h, 1, 1, c);
    ui_push(g, x, y, 0, 0, c); ui_push(g, x + w, y + h, 1, 1, c); ui_push(g, x, y + h, 0, 1, c);
}
void gfx_ui_text(Gfx *g, float x, float y, float scale, Vec4 c, const char *text) {
    static char buf[64 * 1024];
    int quads = stb_easy_font_print(0, 0, (char *)text, NULL, buf, sizeof buf);
    const float *q = (const float *)buf;
    for (int i = 0; i < quads; i++) {
        float px[4], py[4];
        for (int k = 0; k < 4; k++) { px[k] = x + q[(i * 4 + k) * 4 + 0] * scale; py[k] = y + q[(i * 4 + k) * 4 + 1] * scale; }
        ui_push(g, px[0], py[0], 0, 0, c); ui_push(g, px[1], py[1], 0, 0, c); ui_push(g, px[2], py[2], 0, 0, c);
        ui_push(g, px[0], py[0], 0, 0, c); ui_push(g, px[2], py[2], 0, 0, c); ui_push(g, px[3], py[3], 0, 0, c);
    }
}
float gfx_ui_text_width(float scale, const char *text) { return stb_easy_font_width((char *)text) * scale; }

// ---------------------------------------------------------------- end of frame

static void fullscreen_pass(Gfx *g, SDL_GPUCommandBuffer *cmd, SDL_GPUGraphicsPipeline *pipe, SDL_GPUTexture *dst,
                            const SDL_GPUTextureSamplerBinding *samplers, Uint32 nsamplers, const void *uniforms, Uint32 usize, const SDL_GPUViewport *vpt) {
    SDL_GPUColorTargetInfo ct = { .texture = dst, .load_op = SDL_GPU_LOADOP_CLEAR, .store_op = SDL_GPU_STOREOP_STORE, .clear_color = {0, 0, 0, 1} };
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &ct, 1, NULL);
    if (vpt) SDL_SetGPUViewport(pass, vpt);
    SDL_BindGPUGraphicsPipeline(pass, pipe);
    if (uniforms) SDL_PushGPUFragmentUniformData(cmd, 0, uniforms, usize);
    SDL_BindGPUFragmentSamplers(pass, 0, samplers, nsamplers);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
}

void gfx_end(Gfx *g, Platform *pf, const PostParams *pp, double time) {
    if (!pf->cmd || !g->pass) return;
    const FrameParams *fp = &g->frame;

    // Sky behind everything
    {
        Mat4 inv = m4_inverse(fp->view_proj);
        SkyUniforms su = { .inv_view_proj = inv, .cam_pos = v4(fp->cam_pos.x, fp->cam_pos.y, fp->cam_pos.z, 0),
            .sun_dir = v4(fp->sun_dir.x, fp->sun_dir.y, fp->sun_dir.z, 0), .sun_color = v4(fp->sun_color.x, fp->sun_color.y, fp->sun_color.z, fp->sun_glow),
            .zenith = v4(fp->sky_zenith.x, fp->sky_zenith.y, fp->sky_zenith.z, 0), .horizon = v4(fp->sky_horizon.x, fp->sky_horizon.y, fp->sky_horizon.z, 0),
            .ground = v4(fp->sky_ground.x, fp->sky_ground.y, fp->sky_ground.z, 0), .params = v4((float)time, fp->stars, fp->sky_fog_blend, 0),
            .fog_color = v4(fp->fog_color.x, fp->fog_color.y, fp->fog_color.z, 0) };
        bind_pipe(g, g->pipe_sky);
        SDL_PushGPUFragmentUniformData(pf->cmd, 0, &su, sizeof su);
        SDL_DrawGPUPrimitives(g->pass, 3, 1, 0, 0);
    }
    // Decals then particles, from the per-frame lists
    if (g->p_alpha_count + g->p_add_count > 0) {
        // Upload has to happen outside the render pass: end it, copy, and reopen with LOAD.
        SDL_EndGPURenderPass(g->pass); g->pass = NULL;
        Uint8 *map = SDL_MapGPUTransferBuffer(g->dev, g->p_xfer, true);
        memcpy(map, g->p_alpha, g->p_alpha_count * sizeof(PVertex));
        memcpy(map + P_MAX_VERTS * sizeof(PVertex), g->p_add, g->p_add_count * sizeof(PVertex));
        SDL_UnmapGPUTransferBuffer(g->dev, g->p_xfer);
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(pf->cmd);
        SDL_UploadToGPUBuffer(cp, &(SDL_GPUTransferBufferLocation){ .transfer_buffer = g->p_xfer }, &(SDL_GPUBufferRegion){ .buffer = g->p_vb, .size = (Uint32)(2 * P_MAX_VERTS * sizeof(PVertex)) }, true);
        SDL_EndGPUCopyPass(cp);
        SDL_GPUColorTargetInfo ct = { .texture = g->hdr, .load_op = SDL_GPU_LOADOP_LOAD, .store_op = SDL_GPU_STOREOP_STORE };
        SDL_GPUDepthStencilTargetInfo dt = { .texture = g->depth, .load_op = SDL_GPU_LOADOP_LOAD, .store_op = SDL_GPU_STOREOP_DONT_CARE,
            .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE, .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE };
        g->pass = SDL_BeginGPURenderPass(pf->cmd, &ct, 1, &dt);
        g->bound_pipe = NULL; g->bound_tex = NULL;
        Mat4 vpm = fp->view_proj;
        SDL_PushGPUVertexUniformData(pf->cmd, 0, &vpm, sizeof vpm);
        if (g->p_alpha_count) {
            bind_pipe(g, g->pipe_particle_alpha); bind_tex(g, &g->soft, g->samp_clamp);
            SDL_BindGPUVertexBuffers(g->pass, 0, &(SDL_GPUBufferBinding){ .buffer = g->p_vb, .offset = 0 }, 1);
            SDL_DrawGPUPrimitives(g->pass, g->p_alpha_count, 1, 0, 0);
        }
        if (g->p_add_count) {
            bind_pipe(g, g->pipe_particle_add); bind_tex(g, &g->soft, g->samp_clamp);
            SDL_BindGPUVertexBuffers(g->pass, 0, &(SDL_GPUBufferBinding){ .buffer = g->p_vb, .offset = (Uint32)(P_MAX_VERTS * sizeof(PVertex)) }, 1);
            SDL_DrawGPUPrimitives(g->pass, g->p_add_count, 1, 0, 0);
        }
    }
    SDL_EndGPURenderPass(g->pass); g->pass = NULL;

    // Bloom: bright extract to quarter res, then two blur ping-pongs
    {
        Vec4 bp = v4(pp->bloom_threshold, pp->bloom_knee, 0, 0);
        fullscreen_pass(g, pf->cmd, g->pipe_bright, g->bloom_a, &(SDL_GPUTextureSamplerBinding){ .texture = g->hdr, .sampler = g->samp_clamp }, 1, &bp, sizeof bp, NULL);
        for (int i = 0; i < 2; i++) {
            Vec4 dh = v4(1.0f / g->bw * (1.0f + i), 0, 0, 0), dv = v4(0, 1.0f / g->bh * (1.0f + i), 0, 0);
            fullscreen_pass(g, pf->cmd, g->pipe_blur, g->bloom_b, &(SDL_GPUTextureSamplerBinding){ .texture = g->bloom_a, .sampler = g->samp_clamp }, 1, &dh, sizeof dh, NULL);
            fullscreen_pass(g, pf->cmd, g->pipe_blur, g->bloom_a, &(SDL_GPUTextureSamplerBinding){ .texture = g->bloom_b, .sampler = g->samp_clamp }, 1, &dv, sizeof dv, NULL);
        }
    }
    // Tone map and grade into LDR
    {
        PostUniforms u = { .params = v4((float)time, pp->grain, pp->vignette, pp->fade), .res = v4((float)g->iw, (float)g->ih, 0, 0),
            .flash = v4(pp->flash_color.x, pp->flash_color.y, pp->flash_color.z, pp->flash),
            .grade = v4(pp->exposure, pp->saturation, pp->contrast, pp->bloom),
            .lift = v4(pp->lift.x, pp->lift.y, pp->lift.z, 0), .gain = v4(pp->gain.x, pp->gain.y, pp->gain.z, 0) };
        SDL_GPUTextureSamplerBinding s[2] = { { .texture = g->hdr, .sampler = g->samp_clamp }, { .texture = g->bloom_a, .sampler = g->samp_clamp } };
        fullscreen_pass(g, pf->cmd, g->pipe_post, g->ldr, s, 2, &u, sizeof u, NULL);
    }
    // UI over the LDR image
    if (g->ui_count > 0) {
        void *map = SDL_MapGPUTransferBuffer(g->dev, g->ui_xfer, true);
        memcpy(map, g->ui_verts, g->ui_count * sizeof(UIVertex));
        SDL_UnmapGPUTransferBuffer(g->dev, g->ui_xfer);
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(pf->cmd);
        SDL_UploadToGPUBuffer(cp, &(SDL_GPUTransferBufferLocation){ .transfer_buffer = g->ui_xfer }, &(SDL_GPUBufferRegion){ .buffer = g->ui_vb, .size = g->ui_count * (Uint32)sizeof(UIVertex) }, true);
        SDL_EndGPUCopyPass(cp);
        SDL_GPUColorTargetInfo ct = { .texture = g->ldr, .load_op = SDL_GPU_LOADOP_LOAD, .store_op = SDL_GPU_STOREOP_STORE };
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(pf->cmd, &ct, 1, NULL);
        SDL_BindGPUGraphicsPipeline(pass, g->pipe_ui);
        SDL_PushGPUVertexUniformData(pf->cmd, 0, &(Vec4){ (float)g->iw, (float)g->ih, 0, 0 }, sizeof(Vec4));
        SDL_BindGPUFragmentSamplers(pass, 0, &(SDL_GPUTextureSamplerBinding){ .texture = g->white.tex, .sampler = g->samp_nearest }, 1);
        SDL_BindGPUVertexBuffers(pass, 0, &(SDL_GPUBufferBinding){ .buffer = g->ui_vb }, 1);
        SDL_DrawGPUPrimitives(pass, g->ui_count, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
    }
    // Blit to the swapchain, letterboxed
    if (pf->swapchain) {
        float ta = (float)g->iw / (float)g->ih, sw = (float)pf->swap_w, sh = (float)pf->swap_h;
        float vw = sw, vh = sw / ta; if (vh > sh) { vh = sh; vw = sh * ta; }
        SDL_GPUViewport vpt = { .x = (sw - vw) * 0.5f, .y = (sh - vh) * 0.5f, .w = vw, .h = vh, .min_depth = 0, .max_depth = 1 };
        fullscreen_pass(g, pf->cmd, g->pipe_blit, pf->swapchain, &(SDL_GPUTextureSamplerBinding){ .texture = g->ldr, .sampler = g->samp_clamp }, 1, NULL, 0, &vpt);
    }
}

bool gfx_screenshot(Gfx *g, const char *path) {
    Uint32 size = (Uint32)(g->iw * g->ih * 4);
    SDL_GPUTransferBuffer *xfer = SDL_CreateGPUTransferBuffer(g->dev, &(SDL_GPUTransferBufferCreateInfo){ .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, .size = size });
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g->dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_DownloadFromGPUTexture(cp, &(SDL_GPUTextureRegion){ .texture = g->ldr, .w = (Uint32)g->iw, .h = (Uint32)g->ih, .d = 1 }, &(SDL_GPUTextureTransferInfo){ .transfer_buffer = xfer });
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(g->dev, true, &fence, 1); SDL_ReleaseGPUFence(g->dev, fence);
    void *map = SDL_MapGPUTransferBuffer(g->dev, xfer, false);
    int ok = stbi_write_png(path, g->iw, g->ih, 4, map, g->iw * 4);
    SDL_UnmapGPUTransferBuffer(g->dev, xfer); SDL_ReleaseGPUTransferBuffer(g->dev, xfer);
    return ok != 0;
}
