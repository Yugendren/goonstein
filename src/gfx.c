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
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "vendor/stb_truetype.h"

#define HDR_FMT SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT
#define LDR_FMT SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
#define DEPTH_FMT SDL_GPU_TEXTUREFORMAT_D32_FLOAT

// ---------------------------------------------------------------- uniform layouts (std140)

typedef struct VSUniforms { Mat4 view_proj, model; Vec4 uv_xform, flags; } VSUniforms;
typedef struct FrameUniforms {
    Vec4 cam_pos, sun_dir, sun_color, sky_ambient, ground_ambient, fog_color, fog_height, toon;
    Vec4 lights_pos[GFX_MAX_LIGHTS], lights_color[GFX_MAX_LIGHTS];
    Sint32 counts[4];
    Mat4 sun_vp; Vec4 shadow;
} FrameUniforms;
typedef struct MaterialUniforms { Vec4 tint, emissive, rim, water; } MaterialUniforms;
typedef struct SkyUniforms { Mat4 inv_view_proj; Vec4 cam_pos, sun_dir, sun_color, zenith, horizon, ground, params, fog_color; } SkyUniforms;
typedef struct PostUniforms { Vec4 params, res, flash, grade, lift, gain, style; Vec4 pal[64]; Sint32 npal[4]; } PostUniforms;
typedef struct PixUniforms { Vec4 res, offset, params; Vec4 pal[64]; Sint32 npal[4]; } PixUniforms;

// ---------------------------------------------------------------- shaders and helpers

// Which compiled shader files a device can eat, best first. tools/shaders.sh writes .spv, .msl and
// .hlsl on any Unix; .metallib needs the full Xcode Metal toolchain; .dxil comes from
// tools/shaders.ps1 on Windows (signed DXIL needs Microsoft's dxil.dll and cannot be produced on
// macOS or Linux). A format the driver accepts but that is not on disk is skipped, so a partial
// asset set still boots wherever it is complete.
typedef struct ShaderKind { SDL_GPUShaderFormat fmt; const char *ext, *entry, *label; } ShaderKind;

static const ShaderKind SHADER_KINDS[] = {
    // Metal: a prebuilt library first (no runtime MSL compile at startup), then MSL source.
    { SDL_GPU_SHADERFORMAT_METALLIB, "metallib", "main0", "Metal library" },
    { SDL_GPU_SHADERFORMAT_MSL,      "msl",      "main0", "MSL source" },
    { SDL_GPU_SHADERFORMAT_SPIRV,    "spv",      "main",  "SPIR-V" },
    { SDL_GPU_SHADERFORMAT_DXIL,     "dxil",     "main",  "DXIL" },
    { SDL_GPU_SHADERFORMAT_DXBC,     "dxbc",     "main",  "DXBC" },
};
#define SHADER_KIND_COUNT ((int)(sizeof SHADER_KINDS / sizeof *SHADER_KINDS))

// Decided once, on the first shader, and logged: the startup log always names the live path.
static int shader_kind_index(SDL_GPUDevice *dev) {
    static int chosen = -2;
    if (chosen != -2) return chosen;
    SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(dev);
    chosen = -1;
    for (int i = 0; i < SHADER_KIND_COUNT; i++) {
        if (!(formats & SHADER_KINDS[i].fmt)) continue;
        char probe[512]; SDL_PathInfo info;
        snprintf(probe, sizeof probe, "%s/shaders/blit.frag.%s", HOLLOW_ASSET_DIR, SHADER_KINDS[i].ext);
        if (!SDL_GetPathInfo(probe, &info)) continue;   // format supported, but not shipped
        chosen = i;
        break;
    }
    if (chosen < 0)
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "no usable shaders: driver '%s' accepts formats 0x%x but none of them are in "
                     "%s/shaders (run tools/shaders.sh, or tools/shaders.ps1 for DXIL on Windows)",
                     SDL_GetGPUDeviceDriver(dev), (unsigned)formats, HOLLOW_ASSET_DIR);
    else
        SDL_Log("shaders: %s (.%s) on GPU driver %s", SHADER_KINDS[chosen].label,
                SHADER_KINDS[chosen].ext, SDL_GetGPUDeviceDriver(dev));
    return chosen;
}

static SDL_GPUShader *load_shader(Gfx *g, const char *name, SDL_GPUShaderStage stage, Uint32 samplers, Uint32 uniforms) {
    int k = shader_kind_index(g->dev);
    if (k < 0) { SDL_SetError("no supported shader format"); return NULL; }
    const char *ext = SHADER_KINDS[k].ext, *entry = SHADER_KINDS[k].entry;
    SDL_GPUShaderFormat fmt = SHADER_KINDS[k].fmt;
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
        .usage = depth ? (SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER) : (SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER),
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

static Mesh make_quad(Gfx *g) {
    // unit quad in the XY plane, x in [-0.5, 0.5], y in [0, 1], facing +Z
    Vertex v[4] = {
        { {-0.5f, 0, 0}, {0, 0, 1}, {0, 1}, {1, 1, 1, 1} }, { {0.5f, 0, 0}, {0, 0, 1}, {1, 1}, {1, 1, 1, 1} },
        { {0.5f, 1, 0}, {0, 0, 1}, {1, 0}, {1, 1, 1, 1} }, { {-0.5f, 1, 0}, {0, 0, 1}, {0, 0}, {1, 1, 1, 1} } };
    Uint16 idx[6] = { 0, 1, 2, 0, 2, 3 };
    return gfx_mesh_create(g, v, 4, idx, 6);
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
    SDL_GPUShader *lit_fs = load_shader(g, "lit.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 2);
    SDL_GPUShader *shadow_fs = load_shader(g, "shadow.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
    SDL_GPUShader *sky_vs = load_shader(g, "sky.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
    SDL_GPUShader *sky_fs = load_shader(g, "sky.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
    SDL_GPUShader *part_vs = load_shader(g, "particle.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader *part_fs = load_shader(g, "particle.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
    SDL_GPUShader *fs_vs = load_shader(g, "fs.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
    SDL_GPUShader *bright_fs = load_shader(g, "bright.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    SDL_GPUShader *blur_fs = load_shader(g, "blur.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    SDL_GPUShader *post_fs = load_shader(g, "post.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 3, 1);
    SDL_GPUShader *blit_fs = load_shader(g, "blit.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
    SDL_GPUShader *pixcomp_fs = load_shader(g, "pixcomp.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);
    SDL_GPUShader *ui_vs = load_shader(g, "ui.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader *ui_fs = load_shader(g, "ui.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
    SDL_GPUShader *all[] = { world_vs, skin_vs, lit_fs, sky_vs, sky_fs, part_vs, part_fs, fs_vs, bright_fs, blur_fs, post_fs, blit_fs, ui_vs, ui_fs, pixcomp_fs, shadow_fs };
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
    g->pipe_ui_swap = make_pipe(g, &(PipeDesc){ ui_vs, ui_fs, &ui_vb, ui_attrs, 3, g->swap_format, false, false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_CULLMODE_NONE, 1 });
    g->pipe_blit = make_pipe(g, &(PipeDesc){ fs_vs, blit_fs, NULL, NULL, 0, g->swap_format, false, false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_CULLMODE_NONE, 0 });
    g->pipe_pixcomp = make_pipe(g, &(PipeDesc){ fs_vs, pixcomp_fs, NULL, NULL, 0, HDR_FMT, true, true, SDL_GPU_COMPAREOP_LESS, SDL_GPU_CULLMODE_NONE, 0 });
    // depth-only shadow pipelines: no colour target
    {
        SDL_GPUGraphicsPipelineCreateInfo ci = {
            .vertex_shader = world_vs, .fragment_shader = shadow_fs, .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .rasterizer_state = { .fill_mode = SDL_GPU_FILLMODE_FILL, .cull_mode = SDL_GPU_CULLMODE_NONE, .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
                                  .enable_depth_bias = true, .depth_bias_constant_factor = 1.5f, .depth_bias_slope_factor = 2.5f },
            .depth_stencil_state = { .enable_depth_test = true, .enable_depth_write = true, .compare_op = SDL_GPU_COMPAREOP_LESS },
            .target_info = { .num_color_targets = 0, .has_depth_stencil_target = true, .depth_stencil_format = DEPTH_FMT },
            .vertex_input_state = { .vertex_buffer_descriptions = &world_vb, .num_vertex_buffers = 1, .vertex_attributes = world_attrs, .num_vertex_attributes = 4 } };
        g->pipe_shadow = SDL_CreateGPUGraphicsPipeline(g->dev, &ci);
        ci.vertex_shader = skin_vs; ci.vertex_input_state = (SDL_GPUVertexInputState){ .vertex_buffer_descriptions = &skin_vb, .num_vertex_buffers = 1, .vertex_attributes = skin_attrs, .num_vertex_attributes = 5 };
        g->pipe_shadow_skin = SDL_CreateGPUGraphicsPipeline(g->dev, &ci);
        if (!g->pipe_shadow || !g->pipe_shadow_skin) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "shadow pipeline: %s", SDL_GetError());
        g->shadow_size = 2048;
        g->shadow_tex = SDL_CreateGPUTexture(g->dev, &(SDL_GPUTextureCreateInfo){ .type = SDL_GPU_TEXTURETYPE_2D, .format = DEPTH_FMT,
            .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, .width = 2048, .height = 2048, .layer_count_or_depth = 1, .num_levels = 1 });
        if (!g->shadow_tex) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "shadow map: %s", SDL_GetError());
        g->sun_vp = m4_identity(); g->shadow_strength = 0.85f; g->shadow_bias = 0.0025f;
    }
    for (size_t i = 0; i < sizeof all / sizeof *all; i++) SDL_ReleaseGPUShader(g->dev, all[i]);
    if (!g->pipe_world || !g->pipe_skin || !g->pipe_sky || !g->pipe_particle_add || !g->pipe_particle_alpha || !g->pipe_bright || !g->pipe_blur || !g->pipe_post || !g->pipe_ui || !g->pipe_ui_swap || !g->pipe_blit || !g->pipe_pixcomp) return false;

    g->ui_vb = SDL_CreateGPUBuffer(g->dev, &(SDL_GPUBufferCreateInfo){ .usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = UI_MAX_VERTS * sizeof(UIVertex) });
    g->ui_xfer = SDL_CreateGPUTransferBuffer(g->dev, &(SDL_GPUTransferBufferCreateInfo){ .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = UI_MAX_VERTS * sizeof(UIVertex) });
    g->ui_verts = malloc(UI_MAX_VERTS * sizeof(UIVertex));
    g->p_vb = SDL_CreateGPUBuffer(g->dev, &(SDL_GPUBufferCreateInfo){ .usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = 2 * P_MAX_VERTS * sizeof(PVertex) });
    g->p_xfer = SDL_CreateGPUTransferBuffer(g->dev, &(SDL_GPUTransferBufferCreateInfo){ .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = 2 * P_MAX_VERTS * sizeof(PVertex) });
    g->p_add = malloc(P_MAX_VERTS * sizeof(PVertex)); g->p_alpha = malloc(P_MAX_VERTS * sizeof(PVertex));
    g->ui2_vb = SDL_CreateGPUBuffer(g->dev, &(SDL_GPUBufferCreateInfo){ .usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = UI_MAX_VERTS * sizeof(UIVertex) });
    g->ui2_xfer = SDL_CreateGPUTransferBuffer(g->dev, &(SDL_GPUTransferBufferCreateInfo){ .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = UI_MAX_VERTS * sizeof(UIVertex) });
    g->ui2_verts = malloc(UI_MAX_VERTS * sizeof(UIVertex));

    unsigned char white[4] = {255, 255, 255, 255};
    g->white = gfx_texture_create(g, white, 1, 1);
    g->soft = make_soft_disc(g);
    g->cube = make_cube(g);
    g->quad = make_quad(g);
    g->material = material_default();
    g->sprite_lean = SDL_getenv("HOLLOW_LEAN") ? (float)atof(SDL_getenv("HOLLOW_LEAN")) : 0.5f;
    g->pix_levels = 8; g->pix_outline = 1; g->pix_inner = 0.6f;
    g->ui2_scale = 1;
    { char fp[512]; snprintf(fp, sizeof fp, "%s/fonts/VT323-Regular.ttf", HOLLOW_ASSET_DIR); size_t n = 0; g->ttf = SDL_LoadFile(fp, &n);
      if (!g->ttf) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "tool font missing (%s); using the debug font", fp); }
    return true;
}

void gfx_shutdown(Gfx *g) {
    gfx_mesh_destroy(g, &g->cube); gfx_mesh_destroy(g, &g->quad);
    gfx_texture_destroy(g, &g->soft); gfx_texture_destroy(g, &g->white);
    free(g->ui_verts); free(g->ui2_verts); free(g->p_add); free(g->p_alpha);
    SDL_ReleaseGPUTransferBuffer(g->dev, g->ui_xfer); SDL_ReleaseGPUBuffer(g->dev, g->ui_vb);
    SDL_ReleaseGPUTransferBuffer(g->dev, g->ui2_xfer); SDL_ReleaseGPUBuffer(g->dev, g->ui2_vb); SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_ui_swap);
    SDL_ReleaseGPUTransferBuffer(g->dev, g->p_xfer); SDL_ReleaseGPUBuffer(g->dev, g->p_vb);
    SDL_GPUGraphicsPipeline *pipes[] = { g->pipe_world, g->pipe_skin, g->pipe_sky, g->pipe_particle_add, g->pipe_particle_alpha, g->pipe_bright, g->pipe_blur, g->pipe_post, g->pipe_ui, g->pipe_blit };
    for (size_t i = 0; i < sizeof pipes / sizeof *pipes; i++) SDL_ReleaseGPUGraphicsPipeline(g->dev, pipes[i]);
    SDL_ReleaseGPUSampler(g->dev, g->samp_nearest); SDL_ReleaseGPUSampler(g->dev, g->samp_linear); SDL_ReleaseGPUSampler(g->dev, g->samp_clamp);
    SDL_ReleaseGPUTexture(g->dev, g->hdr); SDL_ReleaseGPUTexture(g->dev, g->depth); SDL_ReleaseGPUTexture(g->dev, g->ldr);
    SDL_ReleaseGPUTexture(g->dev, g->bloom_a); SDL_ReleaseGPUTexture(g->dev, g->bloom_b);
    if (g->pix) SDL_ReleaseGPUTexture(g->dev, g->pix); if (g->pix_depth) SDL_ReleaseGPUTexture(g->dev, g->pix_depth);
    for (int i = 0; i < g->nfonts; i++) { gfx_texture_destroy(g, &g->fonts[i].tex); free(g->fonts[i].cdata); }
    if (g->ttf) SDL_free(g->ttf);
    if (g->tool_shot) SDL_ReleaseGPUTexture(g->dev, g->tool_shot);
    if (g->shadow_tex) SDL_ReleaseGPUTexture(g->dev, g->shadow_tex);
    if (g->pipe_shadow) SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_shadow); if (g->pipe_shadow_skin) SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_shadow_skin);
    if (g->por_hdr) SDL_ReleaseGPUTexture(g->dev, g->por_hdr); if (g->por_depth) SDL_ReleaseGPUTexture(g->dev, g->por_depth);
    if (g->por_comp) SDL_ReleaseGPUTexture(g->dev, g->por_comp); if (g->por_comp_depth) SDL_ReleaseGPUTexture(g->dev, g->por_comp_depth);
    if (g->portrait.tex) SDL_ReleaseGPUTexture(g->dev, g->portrait.tex);
    SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_pixcomp);
}

// ---------------------------------------------------------------- world pass

static void push_frame_uniforms(Gfx *g, const FrameParams *fp);
static void fill_palette(Gfx *g, Vec4 *pal, Sint32 *npal);
static void fullscreen_pass(Gfx *g, SDL_GPUCommandBuffer *cmd, SDL_GPUGraphicsPipeline *pipe, SDL_GPUTexture *dst, const SDL_GPUTextureSamplerBinding *samplers, Uint32 nsamplers, const void *uniforms, Uint32 usize, const SDL_GPUViewport *vpt);
static void push_material(Gfx *g, Vec4 tint) {
    const Material *m = &g->material;
    MaterialUniforms u = { .tint = v4(tint.x * m->tint.x, tint.y * m->tint.y, tint.z * m->tint.z, tint.w * m->tint.w),
                           .emissive = v4(m->emissive.x, m->emissive.y, m->emissive.z, m->unlit), .rim = v4(m->rim_color.x, m->rim_color.y, m->rim_color.z, m->rim),
                           .water = v4(m->water, m->water_origin.x, m->water_origin.y, m->water_origin.z) };
    SDL_PushGPUFragmentUniformData(g->cmd, 1, &u, sizeof u);
}

void gfx_begin(Gfx *g, Platform *pf, const FrameParams *fp) {
    gfx_palette_update(g);
    g->frame = *fp; g->ui_count = 0; g->ui_nbatches = 0; g->ui2_count = 0; g->ui2_nbatches = 0; g->ui_target = 0; g->p_add_count = g->p_alpha_count = 0; g->draw_calls = 0;
    g->bound_tex = NULL; g->bound_pipe = NULL; g->pass = NULL; g->cmd = pf->cmd;
    g->material = material_default();
    g->cam_right = fp->cam_right; g->cam_up = fp->cam_up;
    if (!pf->cmd) return;
    SDL_GPUColorTargetInfo ct = { .texture = g->hdr, .load_op = SDL_GPU_LOADOP_CLEAR, .store_op = SDL_GPU_STOREOP_STORE, .clear_color = { fp->fog_color.x, fp->fog_color.y, fp->fog_color.z, 1 } };
    SDL_GPUDepthStencilTargetInfo dt = { .texture = g->depth, .load_op = SDL_GPU_LOADOP_CLEAR, .store_op = SDL_GPU_STOREOP_STORE,
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE, .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE, .clear_depth = 1.0f, .cycle = true };
    g->pass = SDL_BeginGPURenderPass(pf->cmd, &ct, 1, &dt);
    g->in_pix = false; g->main_vp = fp->view_proj;
    push_frame_uniforms(g, fp);
}

static void push_frame_uniforms(Gfx *g, const FrameParams *fp) {
    FrameUniforms u = {
        .cam_pos = v4(fp->cam_pos.x, fp->cam_pos.y, fp->cam_pos.z, 0),
        .sun_dir = v4(fp->sun_dir.x, fp->sun_dir.y, fp->sun_dir.z, fp->sun_intensity),
        .sun_color = v4(fp->sun_color.x, fp->sun_color.y, fp->sun_color.z, 0),
        .sky_ambient = v4(fp->sky_ambient.x, fp->sky_ambient.y, fp->sky_ambient.z, 0),
        .ground_ambient = v4(fp->ground_ambient.x, fp->ground_ambient.y, fp->ground_ambient.z, 0),
        .fog_color = v4(fp->fog_color.x, fp->fog_color.y, fp->fog_color.z, fp->fog_density),
        .fog_height = v4(fp->fog_height_base, fp->fog_height_falloff, fp->fog_scatter, fp->fog_start),
        .toon = v4(fp->toon_softness, fp->shadow_floor, fp->rim_power, fp->time),
        .counts = { fp->nlights > GFX_MAX_LIGHTS ? GFX_MAX_LIGHTS : fp->nlights, 0, 0, 0 },
        .sun_vp = g->sun_vp, .shadow = v4(g->shadow_size > 0 ? 1.0f / g->shadow_size : 0, g->shadow_bias, g->shadow_valid ? g->shadow_strength : 0, 0.075f) };
    // shadow.w: the shadow fades to nothing over the outer 15% of the map's half-extent (0.5 in
    // UV), so where the fitted box ends the world simply stops being shadowed instead of showing
    // a straight line across the ground.
    for (int i = 0; i < u.counts[0]; i++) {
        const PointLight *l = &fp->lights[i];
        u.lights_pos[i] = v4(l->pos.x, l->pos.y, l->pos.z, l->radius);
        u.lights_color[i] = v4(l->color.x * l->intensity, l->color.y * l->intensity, l->color.z * l->intensity, 0);
    }
    SDL_PushGPUFragmentUniformData(g->cmd, 0, &u, sizeof u);
    memcpy(g->frame_uniforms, &u, sizeof u); g->frame_uniforms_size = sizeof u;
    push_material(g, v4(1, 1, 1, 1));
}

// ---------------------------------------------------------------- sun shadow map

void gfx_shadow_begin(Gfx *g, Platform *pf, Mat4 sun_vp, float strength, float bias) {
    g->shadow_valid = false; g->shadow_strength = strength; g->shadow_bias = bias; g->sun_vp = sun_vp;
    if (!pf->cmd || !g->shadow_tex || !g->pipe_shadow || strength <= 0) return;
    g->cmd = pf->cmd; g->bound_pipe = NULL; g->bound_tex = NULL;
    g->frame.view_proj = sun_vp;
    SDL_GPUDepthStencilTargetInfo dt = { .texture = g->shadow_tex, .load_op = SDL_GPU_LOADOP_CLEAR, .store_op = SDL_GPU_STOREOP_STORE,
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE, .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE, .clear_depth = 1.0f, .cycle = true };
    g->pass = SDL_BeginGPURenderPass(pf->cmd, NULL, 0, &dt);
    g->in_shadow = g->pass != NULL;
}

void gfx_shadow_end(Gfx *g) {
    if (!g->in_shadow) return;
    SDL_EndGPURenderPass(g->pass); g->pass = NULL; g->in_shadow = false; g->shadow_valid = true;
    g->bound_pipe = NULL; g->bound_tex = NULL;
}

// ---------------------------------------------------------------- portrait camera

void gfx_portrait_begin(Gfx *g, Platform *pf, const FrameParams *fp, int size, Vec3 backdrop) {
    if (!pf->cmd || g->in_portrait) return;
    if (size < 16) size = 16; if (size > 256) size = 256;
    if (size != g->por_size) {
        if (g->por_hdr) SDL_ReleaseGPUTexture(g->dev, g->por_hdr); if (g->por_depth) SDL_ReleaseGPUTexture(g->dev, g->por_depth);
        if (g->por_comp) SDL_ReleaseGPUTexture(g->dev, g->por_comp); if (g->por_comp_depth) SDL_ReleaseGPUTexture(g->dev, g->por_comp_depth);
        if (g->portrait.tex) SDL_ReleaseGPUTexture(g->dev, g->portrait.tex);
        g->por_hdr = make_target(g, HDR_FMT, size, size, false);
        g->por_comp = make_target(g, HDR_FMT, size, size, false);
        g->portrait.tex = make_target(g, LDR_FMT, size, size, false); g->portrait.w = g->portrait.h = size;
        SDL_GPUTextureCreateInfo di = { .type = SDL_GPU_TEXTURETYPE_2D, .format = DEPTH_FMT, .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                        .width = (Uint32)size, .height = (Uint32)size, .layer_count_or_depth = 1, .num_levels = 1 };
        g->por_depth = SDL_CreateGPUTexture(g->dev, &di);
        g->por_comp_depth = SDL_CreateGPUTexture(g->dev, &di);
        g->por_size = size;
    }
    g->cmd = pf->cmd; g->frame = *fp; g->material = material_default(); g->bound_pipe = NULL; g->bound_tex = NULL;
    g->cam_right = fp->cam_right; g->cam_up = fp->cam_up;
    SDL_GPUColorTargetInfo ct = { .texture = g->por_hdr, .load_op = SDL_GPU_LOADOP_CLEAR, .store_op = SDL_GPU_STOREOP_STORE, .clear_color = { 0, 0, 0, 0 }, .cycle = true };
    SDL_GPUDepthStencilTargetInfo dt = { .texture = g->por_depth, .load_op = SDL_GPU_LOADOP_CLEAR, .store_op = SDL_GPU_STOREOP_STORE,
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE, .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE, .clear_depth = 1.0f, .cycle = true };
    g->pass = SDL_BeginGPURenderPass(pf->cmd, &ct, 1, &dt);
    g->in_portrait = true;
    g->por_backdrop_lin = v3(powf(backdrop.x, 2.2f), powf(backdrop.y, 2.2f), powf(backdrop.z, 2.2f));
    push_frame_uniforms(g, fp);
}

void gfx_portrait_end(Gfx *g) {
    if (!g->in_portrait || !g->pass) return;
    SDL_EndGPURenderPass(g->pass); g->pass = NULL; g->in_portrait = false;
    // composite with outline and palette onto the backdrop
    {
        SDL_GPUColorTargetInfo ct = { .texture = g->por_comp, .load_op = SDL_GPU_LOADOP_CLEAR, .store_op = SDL_GPU_STOREOP_STORE, .clear_color = { g->por_backdrop_lin.x, g->por_backdrop_lin.y, g->por_backdrop_lin.z, 1 }, .cycle = true };
        SDL_GPUDepthStencilTargetInfo dt = { .texture = g->por_comp_depth, .load_op = SDL_GPU_LOADOP_CLEAR, .store_op = SDL_GPU_STOREOP_DONT_CARE,
            .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE, .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE, .clear_depth = 1.0f, .cycle = true };
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(g->cmd, &ct, 1, &dt);
        PixUniforms u = { .res = v4((float)g->por_size, (float)g->por_size, 1.0f / g->por_size, 1.0f / g->por_size), .offset = v4(0, 0, 0, 0),
                          .params = v4(g->pix_levels, g->pix_outline, g->pix_palette, g->pix_inner) };
        fill_palette(g, u.pal, u.npal);
        SDL_BindGPUGraphicsPipeline(pass, g->pipe_pixcomp);
        SDL_PushGPUFragmentUniformData(g->cmd, 0, &u, sizeof u);
        SDL_GPUTextureSamplerBinding sb[2] = { { .texture = g->por_hdr, .sampler = g->samp_nearest }, { .texture = g->por_depth, .sampler = g->samp_nearest } };
        SDL_BindGPUFragmentSamplers(pass, 0, sb, 2);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
    }
    // tone map into the UI texture (neutral grade)
    {
        PostUniforms u = { .params = v4(0, 0, 0, 1), .res = v4((float)g->por_size, (float)g->por_size, 0, 0), .flash = v4(0, 0, 0, 0),
                           .grade = v4(1, 1, 1, 0), .lift = v4(0, 0, 0, 0), .gain = v4(1, 1, 1, 0), .style = v4(0, 0, 0, 1) };
        fill_palette(g, u.pal, u.npal);
        SDL_GPUTextureSamplerBinding sb[3] = { { .texture = g->por_comp, .sampler = g->samp_nearest }, { .texture = g->por_comp, .sampler = g->samp_nearest }, { .texture = g->por_comp_depth, .sampler = g->samp_nearest } };
        fullscreen_pass(g, g->cmd, g->pipe_post, g->portrait.tex, sb, 3, &u, sizeof u, NULL);
    }
}

// ---------------------------------------------------------------- pixel-art layer

void gfx_set_pixel_look(Gfx *g, int scale, float levels, float outline, float palette, float inner) {
    if (scale < 0) scale = 0; if (scale > 8) scale = 8;
    if (scale != g->pixel_scale) {
        if (g->pix) { SDL_ReleaseGPUTexture(g->dev, g->pix); g->pix = NULL; }
        if (g->pix_depth) { SDL_ReleaseGPUTexture(g->dev, g->pix_depth); g->pix_depth = NULL; }
        g->pixel_scale = scale;
        if (scale > 0) {
            g->pw = g->iw / scale; g->ph = g->ih / scale;
            g->pix = make_target(g, HDR_FMT, g->pw, g->ph, false);
            g->pix_depth = SDL_CreateGPUTexture(g->dev, &(SDL_GPUTextureCreateInfo){
                .type = SDL_GPU_TEXTURETYPE_2D, .format = DEPTH_FMT, .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                .width = (Uint32)g->pw, .height = (Uint32)g->ph, .layer_count_or_depth = 1, .num_levels = 1 });
            if (!g->pix || !g->pix_depth) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "pixel layer targets: %s", SDL_GetError()); g->pixel_scale = 0; }
        }
    }
    g->pix_levels = levels; g->pix_outline = outline; g->pix_palette = palette; g->pix_inner = inner;
}

void gfx_palette_update(Gfx *g) {
    Uint64 now = SDL_GetTicks(); if (g->npalette && now - g->palette_check < 1000) return; g->palette_check = now;
    char path[640]; snprintf(path, sizeof path, "%s/palette.txt", HOLLOW_ASSET_DIR);
    SDL_PathInfo info; long long m = SDL_GetPathInfo(path, &info) ? (long long)info.modify_time : 0;
    if (g->npalette && m == g->palette_mtime) return;
    g->palette_mtime = m;
    size_t n = 0; char *text = SDL_LoadFile(path, &n);
    int count = 0;
    if (text) {
        char *cur = text;
        while (*cur && count < 64) {
            char *line = cur; char *nl = strchr(cur, '\n'); if (nl) { *nl = 0; cur = nl + 1; } else cur += strlen(cur);
            char *hash = strchr(line, '#'); if (hash) *hash = 0;
            while (*line == ' ' || *line == '\t') line++;
            unsigned v; if (sscanf(line, "%x", &v) != 1 || strlen(line) < 6) continue;
            g->palette[count++] = v4(((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f, 1);
        }
        SDL_free(text);
    }
    if (count == 0) {   // no file: Endesga 32
        static const unsigned E[32] = { 0xbe4a2f,0xd77643,0xead4aa,0xe4a672,0xb86f50,0x733e39,0x3e2731,0xa22633,0xe43b44,0xf77622,0xfeae34,0xfee761,0x63c74d,0x3e8948,0x265c42,0x193c3e,0x124e89,0x0099db,0x2ce8f5,0xffffff,0xc0cbdc,0x8b9bb4,0x5a6988,0x3a4466,0x262b44,0x181425,0xff0044,0x68386c,0xb55088,0xf6757a,0xe8b796,0xc28569 };
        for (int i = 0; i < 32; i++) g->palette[i] = v4(((E[i] >> 16) & 255) / 255.0f, ((E[i] >> 8) & 255) / 255.0f, (E[i] & 255) / 255.0f, 1);
        count = 32;
    }
    g->npalette = count;
    SDL_Log("palette: %d colours", count);
}
static void fill_palette(Gfx *g, Vec4 *pal, Sint32 *npal) { memcpy(pal, g->palette, sizeof g->palette); npal[0] = g->npalette; npal[1] = npal[2] = npal[3] = 0; }

static void repush_frame(Gfx *g) {
    SDL_PushGPUFragmentUniformData(g->cmd, 0, g->frame_uniforms, g->frame_uniforms_size);
    push_material(g, v4(1, 1, 1, 1));
    g->bound_pipe = NULL; g->bound_tex = NULL;
}

void gfx_pixel_begin(Gfx *g, Mat4 view_proj, float off_x, float off_y) {
    if (!g->pass || g->in_pix || g->pixel_scale <= 0 || !g->pix) return;
    SDL_EndGPURenderPass(g->pass);
    SDL_GPUColorTargetInfo ct = { .texture = g->pix, .load_op = SDL_GPU_LOADOP_CLEAR, .store_op = SDL_GPU_STOREOP_STORE, .clear_color = { 0, 0, 0, 0 }, .cycle = true };
    SDL_GPUDepthStencilTargetInfo dt = { .texture = g->pix_depth, .load_op = SDL_GPU_LOADOP_CLEAR, .store_op = SDL_GPU_STOREOP_STORE,
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE, .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE, .clear_depth = 1.0f, .cycle = true };
    g->pass = SDL_BeginGPURenderPass(g->cmd, &ct, 1, &dt);
    g->in_pix = true; g->frame.view_proj = view_proj; g->pix_off_x = off_x; g->pix_off_y = off_y;
    repush_frame(g);
}

void gfx_pixel_end(Gfx *g) {
    if (!g->pass || !g->in_pix) return;
    SDL_EndGPURenderPass(g->pass);
    g->in_pix = false; g->frame.view_proj = g->main_vp;
    SDL_GPUColorTargetInfo ct = { .texture = g->hdr, .load_op = SDL_GPU_LOADOP_LOAD, .store_op = SDL_GPU_STOREOP_STORE };
    SDL_GPUDepthStencilTargetInfo dt = { .texture = g->depth, .load_op = SDL_GPU_LOADOP_LOAD, .store_op = SDL_GPU_STOREOP_STORE,
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE, .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE };
    g->pass = SDL_BeginGPURenderPass(g->cmd, &ct, 1, &dt);
    g->bound_pipe = NULL; g->bound_tex = NULL;
    // composite the layer, writing its depth into the world's depth buffer
    PixUniforms u = { .res = v4((float)g->pw, (float)g->ph, 1.0f / g->pw, 1.0f / g->ph),
                      .offset = v4(g->pix_off_x / g->pw, g->pix_off_y / g->ph, 0, 0),
                      .params = v4(g->pix_levels, g->pix_outline, g->pix_palette, g->pix_inner) };
    fill_palette(g, u.pal, u.npal);
    SDL_BindGPUGraphicsPipeline(g->pass, g->pipe_pixcomp);
    SDL_PushGPUFragmentUniformData(g->cmd, 0, &u, sizeof u);
    SDL_GPUTextureSamplerBinding sb[2] = { { .texture = g->pix, .sampler = g->samp_nearest }, { .texture = g->pix_depth, .sampler = g->samp_nearest } };
    SDL_BindGPUFragmentSamplers(g->pass, 0, sb, 2);
    SDL_DrawGPUPrimitives(g->pass, 3, 1, 0, 0);
    g->draw_calls++;
    repush_frame(g);   // the composite's uniforms sat in the frame slot
}

void gfx_set_material(Gfx *g, const Material *m) { g->material = m ? *m : material_default(); }
void gfx_set_sprite_lean(Gfx *g, float lean) { g->sprite_lean = lean; }

static void bind_pipe(Gfx *g, SDL_GPUGraphicsPipeline *p) {
    if (g->bound_pipe != p) { SDL_BindGPUGraphicsPipeline(g->pass, p); g->bound_pipe = p; g->bound_tex = NULL; }
}
static void bind_tex(Gfx *g, const Texture *t, SDL_GPUSampler *s) {
    if (g->in_shadow) return;   // the depth pass samples nothing
    if (t != g->bound_tex) {
        SDL_GPUTextureSamplerBinding b[2] = { { .texture = t->tex, .sampler = s }, { .texture = g->shadow_tex, .sampler = g->samp_clamp } };
        SDL_BindGPUFragmentSamplers(g->pass, 0, b, 2); g->bound_tex = t;
    }
}

void gfx_draw(Gfx *g, const Mesh *m, const Texture *t, Mat4 model, Vec4 tint, Vec4 uv_xform) {
    if (!g->pass) return;
    bind_pipe(g, g->in_shadow ? g->pipe_shadow : g->pipe_world);
    VSUniforms u = { g->frame.view_proj, model, uv_xform, v4(g->planar_next ? 1.0f : 0.0f, 0, 0, 0) };
    SDL_PushGPUVertexUniformData(g->cmd, 0, &u, sizeof u);
    if (!g->in_shadow) push_material(g, tint);
    bind_tex(g, t, g->samp_linear);
    SDL_BindGPUVertexBuffers(g->pass, 0, &(SDL_GPUBufferBinding){ .buffer = m->vb }, 1);
    SDL_BindGPUIndexBuffer(g->pass, &(SDL_GPUBufferBinding){ .buffer = m->ib }, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(g->pass, m->index_count, 1, 0, 0, 0);
    g->draw_calls++;
}

void gfx_draw_skinned(Gfx *g, const Mesh *m, const Texture *t, Mat4 model, Vec4 tint, const Mat4 *joints, int njoints) {
    if (!g->pass) return;
    bind_pipe(g, g->in_shadow ? g->pipe_shadow_skin : g->pipe_skin);
    VSUniforms u = { g->frame.view_proj, model, v4(1, 1, 0, 0), v4(0, 0, 0, 0) };
    SDL_PushGPUVertexUniformData(g->cmd, 0, &u, sizeof u);
    static Mat4 tmp[64];
    memset(tmp, 0, sizeof tmp);
    memcpy(tmp, joints, (size_t)(njoints > 64 ? 64 : njoints) * sizeof(Mat4));
    SDL_PushGPUVertexUniformData(g->cmd, 1, tmp, sizeof tmp);
    if (!g->in_shadow) push_material(g, tint);
    bind_tex(g, t, g->samp_linear);
    SDL_BindGPUVertexBuffers(g->pass, 0, &(SDL_GPUBufferBinding){ .buffer = m->vb }, 1);
    SDL_BindGPUIndexBuffer(g->pass, &(SDL_GPUBufferBinding){ .buffer = m->ib }, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(g->pass, m->index_count, 1, 0, 0, 0);
    g->draw_calls++;
}

Texture gfx_texture_load_exact(Gfx *g, const char *path) {
    int w, h, n; unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    if (!px) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "texture load failed: %s", path); return g->white; }
    Texture t = gfx_texture_create(g, px, w, h);
    stbi_image_free(px);
    return t;
}

void gfx_draw_sprite(Gfx *g, const Texture *t, Vec3 foot, float w, float h, const float *uv, Vec4 tint, bool flip_x) {
    if (g->in_shadow) return;
    if (!g->pass) return;
    // Basis: right = camera right (horizontal), up = world up, normal = toward the camera, tilted up a little
    Vec3 r = v3_norm(v3(g->cam_right.x, 0, g->cam_right.z));
    Vec3 to_cam = v3_sub(g->frame.cam_pos, foot);
    // Lean the quad back, away from a high camera, by a fraction of the camera's elevation (feet stay put).
    // Leaning away is what keeps the sprite's full height visible from above, the HD-2D look.
    Vec3 horiz = v3_norm(v3(to_cam.x, 0, to_cam.z));
    float elev = atan2f(to_cam.y, v3_len(v3(to_cam.x, 0, to_cam.z)));
    float lean = elev * g->sprite_lean;
    Vec3 u = v3_norm(v3_sub(v3_scale(v3(0, 1, 0), cosf(lean)), v3_scale(horiz, sinf(lean))));
    Vec3 n = v3_norm(v3_cross(r, u));
    bool mirrored = false;
    // Keep the basis right-handed (so the face is never culled): if the front faces away, flip
    // right and normal together and un-mirror the image through the uvs.
    if (v3_dot(n, to_cam) < 0) { r = v3_scale(r, -1); n = v3_scale(n, -1); mirrored = true; }
    if (flip_x) mirrored = !mirrored;
    Mat4 m = m4_identity();
    m.m[0] = r.x * w; m.m[1] = r.y * w; m.m[2] = r.z * w;
    m.m[4] = u.x * h; m.m[5] = u.y * h; m.m[6] = u.z * h;
    m.m[8] = n.x;     m.m[9] = n.y;     m.m[10] = n.z;
    m.m[12] = foot.x; m.m[13] = foot.y + 0.03f; m.m[14] = foot.z;   // a hair above the ground so feet never z-fight
    // uv_xform: scale then offset: uv' = uv * (u1-u0, v1-v0) + (u0, v0)
    Vec4 xf = mirrored ? v4(uv[0] - uv[2], uv[3] - uv[1], uv[2], uv[1]) : v4(uv[2] - uv[0], uv[3] - uv[1], uv[0], uv[1]);
    bind_pipe(g, g->pipe_world);
    VSUniforms vu = { g->frame.view_proj, m, xf, v4(0, 0, 0, 0) };
    SDL_PushGPUVertexUniformData(g->cmd, 0, &vu, sizeof vu);
    Material saved = g->material; if (g->material.unlit <= 0) g->material.unlit = 0.8f;
    push_material(g, tint); g->material = saved;
    // nearest sampling for crisp pixels; force a rebind since the sampler differs
    SDL_BindGPUFragmentSamplers(g->pass, 0, &(SDL_GPUTextureSamplerBinding){ .texture = t->tex, .sampler = g->samp_nearest }, 1);
    g->bound_tex = NULL;
    SDL_BindGPUVertexBuffers(g->pass, 0, &(SDL_GPUBufferBinding){ .buffer = g->quad.vb }, 1);
    SDL_BindGPUIndexBuffer(g->pass, &(SDL_GPUBufferBinding){ .buffer = g->quad.ib }, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(g->pass, 6, 1, 0, 0, 0);
    g->draw_calls++;
}

void gfx_draw_box(Gfx *g, const Texture *t, Vec3 center, Vec3 size, float yaw, Vec4 tint, float uv_tile) {
    Vec4 xf = uv_tile > 0 ? v4(uv_tile, 0, 0, 0) : v4(1, 1, 0, 0);
    g->planar_next = uv_tile > 0;
    gfx_draw(g, &g->cube, t, m4_trs(center, yaw, size), tint, xf);
    g->planar_next = false;
}

void gfx_draw_box_wire(Gfx *g, Vec3 c, Vec3 s, Vec4 color) {
    if (g->in_shadow) return;
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

static void ui_batch(Gfx *g, const Texture *t);
static void ui_push(Gfx *g, float x, float y, float u, float v, Vec4 c);
static Gfx *font_gfx; static bool font_mode;   // gfx_ui_text_width has no Gfx parameter; text mode follows the UI target
void gfx_ui_target(Gfx *g, int target) { g->ui_target = target; font_gfx = g; font_mode = target == 1 && g->ttf != NULL; }
void gfx_ui_set_transform(Gfx *g, float scale, float ox, float oy) { g->ui2_scale = scale > 0 ? scale : 1; g->ui2_ox = ox; g->ui2_oy = oy; }

// ---------------------------------------------------------------- tool window font
#define FONT_ATLAS 512
#define FONT_PX(scale) ((int)((scale) * 17.0f + 0.5f))
static struct UiFont *font_get(Gfx *g, int px) {
    if (!g->ttf) return NULL;
    if (px < 8) px = 8; if (px > 64) px = 64;
    for (int i = 0; i < g->nfonts; i++) if (g->fonts[i].px == px) return &g->fonts[i];
    if (g->nfonts >= 8) return &g->fonts[0];
    struct UiFont *f = &g->fonts[g->nfonts];
    unsigned char *alpha = malloc(FONT_ATLAS * FONT_ATLAS);
    stbtt_bakedchar *cd = malloc(96 * sizeof *cd);
    if (!alpha || !cd) { free(alpha); free(cd); return NULL; }
    if (stbtt_BakeFontBitmap(g->ttf, 0, (float)px, alpha, FONT_ATLAS, FONT_ATLAS, 32, 96, cd) <= 0) { free(alpha); free(cd); return NULL; }
    unsigned char *rgba = malloc(FONT_ATLAS * FONT_ATLAS * 4);
    for (int i = 0; i < FONT_ATLAS * FONT_ATLAS; i++) { rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255; rgba[i * 4 + 3] = alpha[i]; }
    f->tex = gfx_texture_create(g, rgba, FONT_ATLAS, FONT_ATLAS); free(rgba); free(alpha);
    f->cdata = cd; f->px = px;
    stbtt_fontinfo info; stbtt_InitFont(&info, g->ttf, 0);
    int asc, desc, gap; stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
    f->ascent = (float)asc * stbtt_ScaleForPixelHeight(&info, (float)px);
    g->nfonts++;
    return f;
}
static float font_width(Gfx *g, int px, const char *text) {
    struct UiFont *f = font_get(g, px); if (!f) return 0;
    const stbtt_bakedchar *cd = f->cdata; float w = 0;
    for (const unsigned char *c = (const unsigned char *)text; *c; c++) { int ch = *c < 32 || *c > 126 ? '?' : *c; w += cd[ch - 32].xadvance; }
    return w;
}
static void font_draw(Gfx *g, float x, float y, int px, Vec4 color, const char *text) {
    struct UiFont *f = font_get(g, px); if (!f) return;
    ui_batch(g, &f->tex);
    float pen_x = floorf(x), pen_y = floorf(y + f->ascent);
    for (const unsigned char *c = (const unsigned char *)text; *c; c++) {
        int ch = *c < 32 || *c > 126 ? '?' : *c;
        stbtt_aligned_quad q; stbtt_GetBakedQuad((stbtt_bakedchar *)f->cdata, FONT_ATLAS, FONT_ATLAS, ch - 32, &pen_x, &pen_y, &q, 1);
        ui_push(g, q.x0, q.y0, q.s0, q.t0, color); ui_push(g, q.x1, q.y0, q.s1, q.t0, color); ui_push(g, q.x1, q.y1, q.s1, q.t1, color);
        ui_push(g, q.x0, q.y0, q.s0, q.t0, color); ui_push(g, q.x1, q.y1, q.s1, q.t1, color); ui_push(g, q.x0, q.y1, q.s0, q.t1, color);
    }
}
float gfx_ui_line_h(float scale) { return font_mode ? (float)FONT_PX(scale) : 7.0f * scale + 2.0f; }
void gfx_tool_screenshot_request(Gfx *g, int w, int h) { g->want_tool_shot = true; g->tool_shot_w = w; g->tool_shot_h = h; }

static void ui_batch(Gfx *g, const Texture *t) {
    if (g->ui_target == 1) {
        if (g->ui2_nbatches > 0 && g->ui2_batches[g->ui2_nbatches - 1].tex == t) return;
        if (g->ui2_nbatches >= UI_MAX_BATCHES) return;
        g->ui2_batches[g->ui2_nbatches].tex = t; g->ui2_batches[g->ui2_nbatches].start = g->ui2_count; g->ui2_batches[g->ui2_nbatches].count = 0;
        g->ui2_nbatches++;
        return;
    }
    // Extend the current batch if it uses the same texture, else start a new one
    if (g->ui_nbatches > 0 && g->ui_batches[g->ui_nbatches - 1].tex == t) return;
    if (g->ui_nbatches >= UI_MAX_BATCHES) return;
    g->ui_batches[g->ui_nbatches].tex = t; g->ui_batches[g->ui_nbatches].start = g->ui_count; g->ui_batches[g->ui_nbatches].count = 0;
    g->ui_nbatches++;
}
static void ui_push(Gfx *g, float x, float y, float u, float v, Vec4 c) {
    if (g->ui_target == 1) {
        if (g->ui2_count >= UI_MAX_VERTS) return;
        if (g->ui2_nbatches == 0) ui_batch(g, &g->white);
        g->ui2_batches[g->ui2_nbatches - 1].count++;
        UIVertex *o2 = &g->ui2_verts[g->ui2_count++];
        o2->pos[0] = x * g->ui2_scale + g->ui2_ox; o2->pos[1] = y * g->ui2_scale + g->ui2_oy; o2->uv[0] = u; o2->uv[1] = v;
        o2->color[0] = c.x; o2->color[1] = c.y; o2->color[2] = c.z; o2->color[3] = c.w;
        return;
    }
    if (g->ui_count >= UI_MAX_VERTS) return;
    if (g->ui_nbatches == 0) ui_batch(g, &g->white);
    g->ui_batches[g->ui_nbatches - 1].count++;
    UIVertex *o = &g->ui_verts[g->ui_count++];
    o->pos[0] = x; o->pos[1] = y; o->uv[0] = u; o->uv[1] = v;
    o->color[0] = c.x; o->color[1] = c.y; o->color[2] = c.z; o->color[3] = c.w;
}
void gfx_ui_rect(Gfx *g, float x, float y, float w, float h, Vec4 c) {
    ui_batch(g, &g->white);
    ui_push(g, x, y, 0, 0, c); ui_push(g, x + w, y, 1, 0, c); ui_push(g, x + w, y + h, 1, 1, c);
    ui_push(g, x, y, 0, 0, c); ui_push(g, x + w, y + h, 1, 1, c); ui_push(g, x, y + h, 0, 1, c);
}
void gfx_ui_text(Gfx *g, float x, float y, float scale, Vec4 c, const char *text) {
    if (g->ui_target == 1 && g->ttf) { font_draw(g, x, y, FONT_PX(scale), c, text); return; }
    ui_batch(g, &g->white);
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
float gfx_ui_text_width(float scale, const char *text) { if (font_mode && font_gfx) return font_width(font_gfx, FONT_PX(scale), text); return stb_easy_font_width((char *)text) * scale; }

void gfx_ui_quad(Gfx *g, const float *q, Vec4 c) {
    ui_batch(g, &g->white);
    ui_push(g, q[0], q[1], 0, 0, c); ui_push(g, q[2], q[3], 1, 0, c); ui_push(g, q[4], q[5], 1, 1, c);
    ui_push(g, q[0], q[1], 0, 0, c); ui_push(g, q[4], q[5], 1, 1, c); ui_push(g, q[6], q[7], 0, 1, c);
}

void gfx_ui_text_xf(Gfx *g, float cx, float cy, float scale, float angle, Vec4 c, const char *text) {
    ui_batch(g, &g->white);
    static char buf[64 * 1024];
    int quads = stb_easy_font_print(0, 0, (char *)text, NULL, buf, sizeof buf);
    const float *q = (const float *)buf;
    float w = stb_easy_font_width((char *)text), h = 7.0f;
    float ca = cosf(angle), sa = sinf(angle);
    for (int i = 0; i < quads; i++) {
        float px[4], py[4];
        for (int k = 0; k < 4; k++) {
            float lx = (q[(i * 4 + k) * 4 + 0] - w * 0.5f) * scale, ly = (q[(i * 4 + k) * 4 + 1] - h * 0.5f) * scale;
            px[k] = cx + lx * ca - ly * sa; py[k] = cy + lx * sa + ly * ca;
        }
        ui_push(g, px[0], py[0], 0, 0, c); ui_push(g, px[1], py[1], 0, 0, c); ui_push(g, px[2], py[2], 0, 0, c);
        ui_push(g, px[0], py[0], 0, 0, c); ui_push(g, px[2], py[2], 0, 0, c); ui_push(g, px[3], py[3], 0, 0, c);
    }
}

void gfx_ui_ring(Gfx *g, float cx, float cy, float r, float th, Vec4 c) {
    ui_batch(g, &g->white);
    int n = r > 80 ? 48 : 32;
    float r0 = r - th * 0.5f, r1 = r + th * 0.5f;
    for (int i = 0; i < n; i++) {
        float a0 = (float)i / n * 2 * PI, a1 = (float)(i + 1) / n * 2 * PI;
        float q[8] = { cx + cosf(a0) * r0, cy + sinf(a0) * r0, cx + cosf(a0) * r1, cy + sinf(a0) * r1,
                       cx + cosf(a1) * r1, cy + sinf(a1) * r1, cx + cosf(a1) * r0, cy + sinf(a1) * r0 };
        gfx_ui_quad(g, q, c);
    }
}

void gfx_ui_disc(Gfx *g, float cx, float cy, float r, Vec4 c) {
    ui_batch(g, &g->white);
    int n = 32;
    for (int i = 0; i < n; i++) {
        float a0 = (float)i / n * 2 * PI, a1 = (float)(i + 1) / n * 2 * PI;
        ui_push(g, cx, cy, 0, 0, c); ui_push(g, cx + cosf(a0) * r, cy + sinf(a0) * r, 0, 0, c); ui_push(g, cx + cosf(a1) * r, cy + sinf(a1) * r, 0, 0, c);
    }
}

void gfx_ui_image(Gfx *g, const Texture *t, float x, float y, float w, float h, const float *uv, Vec4 c) {
    float u0 = uv ? uv[0] : 0, v0 = uv ? uv[1] : 0, u1 = uv ? uv[2] : 1, v1 = uv ? uv[3] : 1;
    ui_batch(g, t);
    ui_push(g, x, y, u0, v0, c); ui_push(g, x + w, y, u1, v0, c); ui_push(g, x + w, y + h, u1, v1, c);
    ui_push(g, x, y, u0, v0, c); ui_push(g, x + w, y + h, u1, v1, c); ui_push(g, x, y + h, u0, v1, c);
}

// ---------------------------------------------------------------- end of frame

static void fullscreen_pass(Gfx *g, SDL_GPUCommandBuffer *cmd, SDL_GPUGraphicsPipeline *pipe, SDL_GPUTexture *dst,
                            const SDL_GPUTextureSamplerBinding *samplers, Uint32 nsamplers, const void *uniforms, Uint32 usize, const SDL_GPUViewport *vpt) {
    (void)g;   // kept in the signature so every pass reads the same
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
            .lift = v4(pp->lift.x, pp->lift.y, pp->lift.z, 0), .gain = v4(pp->gain.x, pp->gain.y, pp->gain.z, 0),
            .style = v4(pp->style_snap, pp->style_outline, pp->style_levels, pp->style_pixel) };
        fill_palette(g, u.pal, u.npal);
        SDL_GPUTextureSamplerBinding s[3] = { { .texture = g->hdr, .sampler = g->samp_clamp }, { .texture = g->bloom_a, .sampler = g->samp_clamp }, { .texture = g->depth, .sampler = g->samp_nearest } };
        fullscreen_pass(g, pf->cmd, g->pipe_post, g->ldr, s, 3, &u, sizeof u, NULL);
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
        SDL_BindGPUVertexBuffers(pass, 0, &(SDL_GPUBufferBinding){ .buffer = g->ui_vb }, 1);
        for (int i = 0; i < g->ui_nbatches; i++) {
            if (g->ui_batches[i].count == 0) continue;
            SDL_BindGPUFragmentSamplers(pass, 0, &(SDL_GPUTextureSamplerBinding){ .texture = g->ui_batches[i].tex->tex, .sampler = g->samp_nearest }, 1);
            SDL_DrawGPUPrimitives(pass, g->ui_batches[i].count, 1, g->ui_batches[i].start, 0);
        }
        SDL_EndGPURenderPass(pass);
    }
    // Blit to the swapchain, letterboxed
    if (pf->swapchain) {
        float ta = (float)g->iw / (float)g->ih, sw = (float)pf->swap_w, sh = (float)pf->swap_h;
        float vw = sw, vh = sw / ta; if (vh > sh) { vh = sh; vw = sh * ta; }
        SDL_GPUViewport vpt = { .x = (sw - vw) * 0.5f, .y = (sh - vh) * 0.5f, .w = vw, .h = vh, .min_depth = 0, .max_depth = 1 };
        fullscreen_pass(g, pf->cmd, g->pipe_blit, pf->swapchain, &(SDL_GPUTextureSamplerBinding){ .texture = g->ldr, .sampler = g->samp_clamp }, 1, NULL, 0, &vpt);
    }
    // Tool window screenshot: the same UI list into an offscreen texture
    if (g->want_tool_shot && g->ui2_count > 0) {
        if (!g->tool_shot) g->tool_shot = make_target(g, LDR_FMT, g->tool_shot_w, g->tool_shot_h, false);
        void *map = SDL_MapGPUTransferBuffer(g->dev, g->ui2_xfer, true);
        memcpy(map, g->ui2_verts, g->ui2_count * sizeof(UIVertex));
        SDL_UnmapGPUTransferBuffer(g->dev, g->ui2_xfer);
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(pf->cmd);
        SDL_UploadToGPUBuffer(cp, &(SDL_GPUTransferBufferLocation){ .transfer_buffer = g->ui2_xfer }, &(SDL_GPUBufferRegion){ .buffer = g->ui2_vb, .size = g->ui2_count * (Uint32)sizeof(UIVertex) }, true);
        SDL_EndGPUCopyPass(cp);
        SDL_GPUColorTargetInfo ct = { .texture = g->tool_shot, .load_op = SDL_GPU_LOADOP_CLEAR, .store_op = SDL_GPU_STOREOP_STORE, .clear_color = { 0.05f, 0.05f, 0.07f, 1 } };
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(pf->cmd, &ct, 1, NULL);
        SDL_BindGPUGraphicsPipeline(pass, g->pipe_ui);
        SDL_PushGPUVertexUniformData(pf->cmd, 0, &(Vec4){ (float)g->tool_shot_w, (float)g->tool_shot_h, 0, 0 }, sizeof(Vec4));
        SDL_BindGPUVertexBuffers(pass, 0, &(SDL_GPUBufferBinding){ .buffer = g->ui2_vb }, 1);
        for (int i = 0; i < g->ui2_nbatches; i++) {
            if (g->ui2_batches[i].count == 0) continue;
            SDL_BindGPUFragmentSamplers(pass, 0, &(SDL_GPUTextureSamplerBinding){ .texture = g->ui2_batches[i].tex->tex, .sampler = g->samp_nearest }, 1);
            SDL_DrawGPUPrimitives(pass, g->ui2_batches[i].count, 1, g->ui2_batches[i].start, 0);
        }
        SDL_EndGPURenderPass(pass);
    }
    // Debugger window: its own UI list, drawn straight into its swapchain in a 720x820 coordinate space
    if (pf->console_swap) {
        SDL_GPUColorTargetInfo ct = { .texture = pf->console_swap, .load_op = SDL_GPU_LOADOP_CLEAR, .store_op = SDL_GPU_STOREOP_STORE, .clear_color = { 0.05f, 0.05f, 0.07f, 1 } };
        SDL_GPURenderPass *pass = NULL;
        if (g->ui2_count > 0) {
            void *map = SDL_MapGPUTransferBuffer(g->dev, g->ui2_xfer, true);
            memcpy(map, g->ui2_verts, g->ui2_count * sizeof(UIVertex));
            SDL_UnmapGPUTransferBuffer(g->dev, g->ui2_xfer);
            SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(pf->cmd);
            SDL_UploadToGPUBuffer(cp, &(SDL_GPUTransferBufferLocation){ .transfer_buffer = g->ui2_xfer }, &(SDL_GPUBufferRegion){ .buffer = g->ui2_vb, .size = g->ui2_count * (Uint32)sizeof(UIVertex) }, true);
            SDL_EndGPUCopyPass(cp);
        }
        pass = SDL_BeginGPURenderPass(pf->cmd, &ct, 1, NULL);
        if (g->ui2_count > 0) {
            SDL_BindGPUGraphicsPipeline(pass, g->pipe_ui_swap);
            SDL_PushGPUVertexUniformData(pf->cmd, 0, &(Vec4){ (float)(pf->tool_w > 0 ? pf->tool_w : 720), (float)(pf->tool_h > 0 ? pf->tool_h : 820), 0, 0 }, sizeof(Vec4));
            SDL_BindGPUVertexBuffers(pass, 0, &(SDL_GPUBufferBinding){ .buffer = g->ui2_vb }, 1);
            for (int i = 0; i < g->ui2_nbatches; i++) {
                if (g->ui2_batches[i].count == 0) continue;
                SDL_BindGPUFragmentSamplers(pass, 0, &(SDL_GPUTextureSamplerBinding){ .texture = g->ui2_batches[i].tex->tex, .sampler = g->samp_nearest }, 1);
                SDL_DrawGPUPrimitives(pass, g->ui2_batches[i].count, 1, g->ui2_batches[i].start, 0);
            }
        }
        SDL_EndGPURenderPass(pass);
    }
}

static bool save_texture(Gfx *g, SDL_GPUTexture *tex, int w, int h, const char *path) {
    Uint32 size = (Uint32)(w * h * 4);
    SDL_GPUTransferBuffer *xfer = SDL_CreateGPUTransferBuffer(g->dev, &(SDL_GPUTransferBufferCreateInfo){ .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, .size = size });
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g->dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_DownloadFromGPUTexture(cp, &(SDL_GPUTextureRegion){ .texture = tex, .w = (Uint32)w, .h = (Uint32)h, .d = 1 }, &(SDL_GPUTextureTransferInfo){ .transfer_buffer = xfer });
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(g->dev, true, &fence, 1); SDL_ReleaseGPUFence(g->dev, fence);
    void *map = SDL_MapGPUTransferBuffer(g->dev, xfer, false);
    int ok = stbi_write_png(path, w, h, 4, map, w * 4);
    SDL_UnmapGPUTransferBuffer(g->dev, xfer); SDL_ReleaseGPUTransferBuffer(g->dev, xfer);
    return ok != 0;
}
bool gfx_tool_screenshot_save(Gfx *g, const char *path) { return g->tool_shot && save_texture(g, g->tool_shot, g->tool_shot_w, g->tool_shot_h, path); }

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
