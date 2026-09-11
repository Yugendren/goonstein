#include "gfx.h"
#include "prof.h"
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
#define SMALL_HDR_FMT SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT
#define LDR_FMT SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
#define DEPTH_FMT SDL_GPU_TEXTUREFORMAT_D32_FLOAT

// ---------------------------------------------------------------- uniform layouts (std140)

typedef struct VSUniforms { Mat4 view_proj, model; Vec4 uv_xform, flags; } VSUniforms;
// One queued instance: which batch it belongs to, and the bytes the GPU will read (see gfx_instance).
struct GfxInstItem { int batch; GfxInstance data; };
typedef struct FrameUniforms {
    Vec4 cam_pos, sun_dir, sun_color, sky_ambient, ground_ambient, fog_color, fog_height, toon;
    Vec4 lights_pos[GFX_MAX_LIGHTS], lights_color[GFX_MAX_LIGHTS];
    Sint32 counts[4];
    Mat4 sun_vp; Vec4 shadow;
} FrameUniforms;
typedef struct MaterialUniforms { Vec4 tint, emissive, rim, water, flat; } MaterialUniforms;
typedef struct SkyUniforms { Mat4 inv_view_proj; Vec4 cam_pos, sun_dir, sun_color, zenith, horizon, ground, params, fog_color; } SkyUniforms;
typedef struct PostUniforms { Vec4 params, res, flash, grade, lift, gain, style, ink, film; Vec4 pal[64]; Sint32 npal[4]; } PostUniforms;
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
    // Average colour, for `look flat`. Opaque texels only: a cutout leaf texture is mostly
    // transparent, and averaging the transparent texels in would drag every plant toward black.
    {
        long sum[3] = {0, 0, 0}, sum_all[3] = {0, 0, 0}; long n = 0, n_all = (long)w * h;
        for (long i = 0; i < n_all; i++) {
            const unsigned char *p = rgba + i * 4;
            sum_all[0] += p[0]; sum_all[1] += p[1]; sum_all[2] += p[2];
            if (p[3] >= 128) { sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2]; n++; }
        }
        if (n == 0) { n = n_all; sum[0] = sum_all[0]; sum[1] = sum_all[1]; sum[2] = sum_all[2]; }
        if (n > 0) { t.mean[0] = (float)sum[0] / n / 255.0f; t.mean[1] = (float)sum[1] / n / 255.0f; t.mean[2] = (float)sum[2] / n / 255.0f; }
    }
    // Mip levels. Until now every texture in this game was one level, which meant a 1024-pixel
    // photoscan on a shrub forty metres away was sampling one texel in twenty from a texture the
    // cache could not hold: the worst case for bandwidth and the worst case for aliasing at the
    // same time. Mips are the rare change that is both faster and better looking, so everything
    // gets them -- and the two samplers that must stay hard-edged (nearest, for pixel art and the
    // UI; clamp, for the single-level render targets and the shadow map) simply never ask for a
    // level above zero, so nothing about the pixel look changes.
    int levels = 1;
    for (int m = w > h ? w : h; m > 1; m >>= 1) levels++;
    t.tex = SDL_CreateGPUTexture(g->dev, &(SDL_GPUTextureCreateInfo){
        .type = SDL_GPU_TEXTURETYPE_2D, .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        // GenerateMipmaps renders each level from the one above it, so the texture has to be usable
        // as a colour target as well as a sampler. That is SDL's rule, not a choice.
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (levels > 1 ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0u),
        .width = (Uint32)w, .height = (Uint32)h, .layer_count_or_depth = 1, .num_levels = (Uint32)levels });
    if (!t.tex && levels > 1) {   // a driver that will not make it a colour target still gets a texture
        levels = 1;
        t.tex = SDL_CreateGPUTexture(g->dev, &(SDL_GPUTextureCreateInfo){
            .type = SDL_GPU_TEXTURETYPE_2D, .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
            .width = (Uint32)w, .height = (Uint32)h, .layer_count_or_depth = 1, .num_levels = 1 });
        static bool said = false;
        if (!said) { said = true; SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "no mipmaps on '%s': %s", SDL_GetGPUDeviceDriver(g->dev), SDL_GetError()); }
    }
    t.levels = levels;
    Uint32 size = (Uint32)(w * h * 4);
    SDL_GPUTransferBuffer *xfer = SDL_CreateGPUTransferBuffer(g->dev, &(SDL_GPUTransferBufferCreateInfo){ .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = size });
    void *map = SDL_MapGPUTransferBuffer(g->dev, xfer, false); memcpy(map, rgba, size); SDL_UnmapGPUTransferBuffer(g->dev, xfer);
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g->dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUTexture(cp, &(SDL_GPUTextureTransferInfo){ .transfer_buffer = xfer }, &(SDL_GPUTextureRegion){ .texture = t.tex, .w = (Uint32)w, .h = (Uint32)h, .d = 1 }, false);
    SDL_EndGPUCopyPass(cp);
    if (levels > 1) SDL_GenerateMipmapsForGPUTexture(cmd, t.tex);
    SDL_SubmitGPUCommandBuffer(cmd); SDL_ReleaseGPUTransferBuffer(g->dev, xfer);
    return t;
}

Texture gfx_texture_load(Gfx *g, const char *path, int max_size) {
    // A memory and bandwidth lever for weak hardware, not a look: a 2k albedo on a 640x400 frame
    // is never more than a few texels per pixel of waste.
    if (g->tex_cap > 0 && max_size > g->tex_cap) max_size = g->tex_cap;
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
    Uint32 nvb;  // vertex buffer slots; 0 means 1 (every pipeline but the instanced ones)
} PipeDesc;

// D3D12 refuses things Metal and Vulkan wave through -- a format, a target combination, a
// shader the driver's own compiler will not take -- and the only place that shows is here. Ask
// once, at startup, and put the answer in the log the tester sends back.
static void log_format_support(Gfx *g, const char *what, SDL_GPUTextureFormat fmt, SDL_GPUTextureUsageFlags usage) {
    bool ok = SDL_GPUTextureSupportsFormat(g->dev, fmt, SDL_GPU_TEXTURETYPE_2D, usage);
    if (ok) SDL_Log("format ok: %s (enum %d, usage 0x%x)", what, (int)fmt, (unsigned)usage);
    else SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "format UNSUPPORTED: %s (enum %d, usage 0x%x) on GPU driver '%s'",
                      what, (int)fmt, (unsigned)usage, SDL_GetGPUDeviceDriver(g->dev));
}

static SDL_GPUGraphicsPipeline *make_pipe(Gfx *g, const char *name, const PipeDesc *d) {
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
    if (d->vb) ci.vertex_input_state = (SDL_GPUVertexInputState){ .vertex_buffer_descriptions = d->vb, .num_vertex_buffers = d->nvb ? d->nvb : 1, .vertex_attributes = d->attrs, .num_vertex_attributes = d->nattrs };
    SDL_GPUGraphicsPipeline *p = SDL_CreateGPUGraphicsPipeline(g->dev, &ci);
    if (!p) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "pipeline '%s' failed: %s", name, SDL_GetError());
    return p;
}

static bool g_want_small_hdr = false;
void gfx_request_small_hdr(bool on) { g_want_small_hdr = on; }

bool gfx_init(Gfx *g, Platform *pf, int iw, int ih) {
    memset(g, 0, sizeof *g);
    g->dev = pf->gpu; g->iw = iw; g->ih = ih; g->bw = iw / 4; g->bh = ih / 4;
    g->uiw = iw; g->uih = ih;   // fixed UI coordinate space; gfx_set_render_scale never touches this
    g->render_scale = 1.0f;
    SDL_Log("gfx: %dx%d internal on GPU driver '%s'", iw, ih, SDL_GetGPUDeviceDriver(g->dev));
    log_format_support(g, "HDR colour target (RGBA16F)", HDR_FMT, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
    log_format_support(g, "HDR colour target, small (R11G11B10)", SMALL_HDR_FMT, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
    g->hdr_fmt = HDR_FMT;
    if (g_want_small_hdr && SDL_GPUTextureSupportsFormat(g->dev, SMALL_HDR_FMT, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER))
        g->hdr_fmt = SMALL_HDR_FMT;
    SDL_Log("gfx: HDR target format enum %d (%s)", (int)g->hdr_fmt, g->hdr_fmt == HDR_FMT ? "RGBA16F" : "R11G11B10");
    log_format_support(g, "LDR colour target (RGBA8)", LDR_FMT, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
    log_format_support(g, "depth, sampled for shadows (D32F)", DEPTH_FMT, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
    g->hdr = make_target(g, g->hdr_fmt, iw, ih, false);
    g->depth = make_target(g, DEPTH_FMT, iw, ih, true);
    g->ldr = make_target(g, LDR_FMT, iw, ih, false);
    g->bloom_a = make_target(g, g->hdr_fmt, g->bw, g->bh, false);
    g->bloom_b = make_target(g, g->hdr_fmt, g->bw, g->bh, false);
    if (!g->hdr || !g->depth || !g->ldr || !g->bloom_a || !g->bloom_b) return false;
    g->swap_format = SDL_GetGPUSwapchainTextureFormat(g->dev, pf->window);
    SDL_Log("gfx: swapchain format enum %d", (int)g->swap_format);

    g->samp_nearest = SDL_CreateGPUSampler(g->dev, &(SDL_GPUSamplerCreateInfo){ .min_filter = SDL_GPU_FILTER_NEAREST, .mag_filter = SDL_GPU_FILTER_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT, .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT });
    // The world sampler. mipmap_mode was already LINEAR, but max_lod defaults to 0, which pins
    // every fetch to level 0 -- so trilinear was asked for and never happened, because there was
    // nothing below level 0 to blend with. Both halves are here now, plus anisotropy, which is what
    // keeps a path or a wall seen at a grazing angle from going to mush once mips exist.
    g->samp_linear = SDL_CreateGPUSampler(g->dev, &(SDL_GPUSamplerCreateInfo){ .min_filter = SDL_GPU_FILTER_LINEAR, .mag_filter = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR, .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT, .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .min_lod = 0.0f, .max_lod = 1000.0f, .enable_anisotropy = true, .max_anisotropy = 8 });
    // No mip_lod_bias here: Metal's sampler does not have one and SDL drops it, so a bias set on
    // the sampler would quietly mean two different pictures on two platforms. lit.frag biases the
    // fetch instead, which all three backends spell the same way.
    g->samp_clamp = SDL_CreateGPUSampler(g->dev, &(SDL_GPUSamplerCreateInfo){ .min_filter = SDL_GPU_FILTER_LINEAR, .mag_filter = SDL_GPU_FILTER_LINEAR,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE, .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE });

    SDL_GPUShader *world_vs = load_shader(g, "world.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader *world_inst_vs = load_shader(g, "world_inst.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
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
    SDL_GPUShader *post_fs = load_shader(g, "post.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 4, 1);
    SDL_GPUShader *blit_fs = load_shader(g, "blit.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
    SDL_GPUShader *pixcomp_fs = load_shader(g, "pixcomp.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);
    SDL_GPUShader *ui_vs = load_shader(g, "ui.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader *ui_fs = load_shader(g, "ui.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
    SDL_GPUShader *all[] = { world_vs, world_inst_vs, skin_vs, lit_fs, sky_vs, sky_fs, part_vs, part_fs, fs_vs, bright_fs, blur_fs, post_fs, blit_fs, ui_vs, ui_fs, pixcomp_fs, shadow_fs };
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
    // Instanced world draws: slot 0 is the mesh, stepped per vertex; slot 1 is the per-frame
    // instance stream, stepped per instance. instance_step_rate is left at 0, which SDL3 reserves
    // and every backend reads as "once per instance".
    SDL_GPUVertexBufferDescription inst_vbs[2] = {
        { .slot = 0, .pitch = sizeof(Vertex), .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX },
        { .slot = 1, .pitch = sizeof(GfxInstance), .input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE } };
    SDL_GPUVertexAttribute inst_attrs[] = {
        { .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 0 },
        { .location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 12 },
        { .location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 24 },
        { .location = 3, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 32 },
        { .location = 4, .buffer_slot = 1, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 0 },
        { .location = 5, .buffer_slot = 1, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 16 },
        { .location = 6, .buffer_slot = 1, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 32 },
        { .location = 7, .buffer_slot = 1, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 48 },
        { .location = 8, .buffer_slot = 1, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 64 } };

    g->pipe_world = make_pipe(g, "world", &(PipeDesc){ world_vs, lit_fs, &world_vb, world_attrs, 4, g->hdr_fmt, true, true, SDL_GPU_COMPAREOP_LESS, SDL_GPU_CULLMODE_BACK, 0, 0 });
    g->pipe_world_inst = make_pipe(g, "world_inst", &(PipeDesc){ world_inst_vs, lit_fs, inst_vbs, inst_attrs, 9, g->hdr_fmt, true, true, SDL_GPU_COMPAREOP_LESS, SDL_GPU_CULLMODE_BACK, 0, 2 });
    g->pipe_skin = make_pipe(g, "skin", &(PipeDesc){ skin_vs, lit_fs, &skin_vb, skin_attrs, 5, g->hdr_fmt, true, true, SDL_GPU_COMPAREOP_LESS, SDL_GPU_CULLMODE_BACK, 0, 0 });
    g->pipe_sky = make_pipe(g, "sky", &(PipeDesc){ sky_vs, sky_fs, NULL, NULL, 0, g->hdr_fmt, true, false, SDL_GPU_COMPAREOP_LESS_OR_EQUAL, SDL_GPU_CULLMODE_NONE, 0, 0 });
    g->pipe_particle_add = make_pipe(g, "particle_add", &(PipeDesc){ part_vs, part_fs, &p_vb, p_attrs, 3, g->hdr_fmt, true, false, SDL_GPU_COMPAREOP_LESS, SDL_GPU_CULLMODE_NONE, 2, 0 });
    g->pipe_particle_alpha = make_pipe(g, "particle_alpha", &(PipeDesc){ part_vs, part_fs, &p_vb, p_attrs, 3, g->hdr_fmt, true, false, SDL_GPU_COMPAREOP_LESS, SDL_GPU_CULLMODE_NONE, 1, 0 });
    g->pipe_bright = make_pipe(g, "bright", &(PipeDesc){ fs_vs, bright_fs, NULL, NULL, 0, g->hdr_fmt, false, false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_CULLMODE_NONE, 0, 0 });
    g->pipe_blur = make_pipe(g, "blur", &(PipeDesc){ fs_vs, blur_fs, NULL, NULL, 0, g->hdr_fmt, false, false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_CULLMODE_NONE, 0, 0 });
    g->pipe_post = make_pipe(g, "post", &(PipeDesc){ fs_vs, post_fs, NULL, NULL, 0, LDR_FMT, false, false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_CULLMODE_NONE, 0, 0 });
    g->pipe_ui = make_pipe(g, "ui", &(PipeDesc){ ui_vs, ui_fs, &ui_vb, ui_attrs, 3, LDR_FMT, false, false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_CULLMODE_NONE, 1, 0 });
    g->pipe_ui_swap = make_pipe(g, "ui_swap", &(PipeDesc){ ui_vs, ui_fs, &ui_vb, ui_attrs, 3, g->swap_format, false, false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_CULLMODE_NONE, 1, 0 });
    g->pipe_blit = make_pipe(g, "blit", &(PipeDesc){ fs_vs, blit_fs, NULL, NULL, 0, g->swap_format, false, false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_CULLMODE_NONE, 0, 0 });
    g->pipe_pixcomp = make_pipe(g, "pixcomp", &(PipeDesc){ fs_vs, pixcomp_fs, NULL, NULL, 0, g->hdr_fmt, true, true, SDL_GPU_COMPAREOP_LESS, SDL_GPU_CULLMODE_NONE, 0, 0 });
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
        if (!g->pipe_shadow) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "pipeline 'shadow' failed: %s", SDL_GetError());
        ci.vertex_shader = world_inst_vs; ci.vertex_input_state = (SDL_GPUVertexInputState){ .vertex_buffer_descriptions = inst_vbs, .num_vertex_buffers = 2, .vertex_attributes = inst_attrs, .num_vertex_attributes = 9 };
        g->pipe_shadow_inst = SDL_CreateGPUGraphicsPipeline(g->dev, &ci);
        if (!g->pipe_shadow_inst) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "pipeline 'shadow_inst' failed: %s", SDL_GetError());
        ci.vertex_shader = skin_vs; ci.vertex_input_state = (SDL_GPUVertexInputState){ .vertex_buffer_descriptions = &skin_vb, .num_vertex_buffers = 1, .vertex_attributes = skin_attrs, .num_vertex_attributes = 5 };
        g->pipe_shadow_skin = SDL_CreateGPUGraphicsPipeline(g->dev, &ci);
        if (!g->pipe_shadow_skin) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "pipeline 'shadow_skin' failed: %s", SDL_GetError());
        g->shadow_size = 2048;
        g->shadow_tex = SDL_CreateGPUTexture(g->dev, &(SDL_GPUTextureCreateInfo){ .type = SDL_GPU_TEXTURETYPE_2D, .format = DEPTH_FMT,
            .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, .width = 2048, .height = 2048, .layer_count_or_depth = 1, .num_levels = 1 });
        if (!g->shadow_tex) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "shadow map: %s", SDL_GetError());
        g->sun_vp = m4_identity(); g->shadow_strength = 0.85f; g->shadow_bias = 0.0025f;
    }
    for (size_t i = 0; i < sizeof all / sizeof *all; i++) SDL_ReleaseGPUShader(g->dev, all[i]);

    // Roll call. A pipeline the driver refuses used to be a NULL pointer handed to
    // SDL_BindGPUGraphicsPipeline several frames later, where SDL's own assert takes the process
    // out with nothing in the log to say why. Name every one that failed, here, at once; refuse to
    // start if a pipeline the frame cannot do without is among them; and let the optional ones
    // (shadows) be skipped at draw time rather than crash.
    {
        struct { const char *name; SDL_GPUGraphicsPipeline *p; bool required; } roll[] = {
            { "world", g->pipe_world, true }, { "skin", g->pipe_skin, true }, { "sky", g->pipe_sky, true },
            { "particle_add", g->pipe_particle_add, true }, { "particle_alpha", g->pipe_particle_alpha, true },
            { "bright", g->pipe_bright, true }, { "blur", g->pipe_blur, true }, { "post", g->pipe_post, true },
            { "ui", g->pipe_ui, true }, { "ui_swap", g->pipe_ui_swap, true }, { "blit", g->pipe_blit, true },
            { "pixcomp", g->pipe_pixcomp, true },
            { "shadow", g->pipe_shadow, false }, { "shadow_skin", g->pipe_shadow_skin, false }, { "shadow_inst", g->pipe_shadow_inst, false },
            { "world_inst", g->pipe_world_inst, false },
        };
        const int nroll = (int)(sizeof roll / sizeof *roll);
        char missing[256]; missing[0] = 0; int nmissing = 0; bool fatal = false;
        for (int i = 0; i < nroll; i++) {
            if (roll[i].p) continue;
            nmissing++; if (roll[i].required) fatal = true;
            size_t used = strlen(missing);
            snprintf(missing + used, sizeof missing - used, "%s%s%s", used ? " " : "", roll[i].name, roll[i].required ? "(required)" : "(optional)");
        }
        if (nmissing == 0) SDL_Log("pipelines: all %d created on '%s'", nroll, SDL_GetGPUDeviceDriver(g->dev));
        else SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "pipelines: %d of %d failed on '%s': %s",
                          nmissing, nroll, SDL_GetGPUDeviceDriver(g->dev), missing);
        if (!g->shadow_tex) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "no shadow map: the sun will not cast");
        if (fatal) { SDL_SetError("required pipelines failed: %s", missing); return false; }
    }

    g->ui_vb = SDL_CreateGPUBuffer(g->dev, &(SDL_GPUBufferCreateInfo){ .usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = UI_MAX_VERTS * sizeof(UIVertex) });
    g->ui_xfer = SDL_CreateGPUTransferBuffer(g->dev, &(SDL_GPUTransferBufferCreateInfo){ .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = UI_MAX_VERTS * sizeof(UIVertex) });
    g->ui_verts = malloc(UI_MAX_VERTS * sizeof(UIVertex));
    g->p_vb = SDL_CreateGPUBuffer(g->dev, &(SDL_GPUBufferCreateInfo){ .usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = 2 * P_MAX_VERTS * sizeof(PVertex) });
    g->p_xfer = SDL_CreateGPUTransferBuffer(g->dev, &(SDL_GPUTransferBufferCreateInfo){ .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = 2 * P_MAX_VERTS * sizeof(PVertex) });
    g->p_add = malloc(P_MAX_VERTS * sizeof(PVertex)); g->p_alpha = malloc(P_MAX_VERTS * sizeof(PVertex));
    g->ui2_vb = SDL_CreateGPUBuffer(g->dev, &(SDL_GPUBufferCreateInfo){ .usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = UI_MAX_VERTS * sizeof(UIVertex) });
    g->ui2_xfer = SDL_CreateGPUTransferBuffer(g->dev, &(SDL_GPUTransferBufferCreateInfo){ .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = UI_MAX_VERTS * sizeof(UIVertex) });
    g->ui2_verts = malloc(UI_MAX_VERTS * sizeof(UIVertex));
    g->inst_vb = SDL_CreateGPUBuffer(g->dev, &(SDL_GPUBufferCreateInfo){ .usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = GFX_MAX_INSTANCES * (Uint32)sizeof(GfxInstance) });
    g->inst_xfer = SDL_CreateGPUTransferBuffer(g->dev, &(SDL_GPUTransferBufferCreateInfo){ .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = GFX_MAX_INSTANCES * (Uint32)sizeof(GfxInstance) });
    g->inst_items = malloc(GFX_MAX_INSTANCES * sizeof *g->inst_items);
    if (!g->inst_vb || !g->inst_xfer || !g->inst_items) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "no instance stream: props fall back to one draw each");

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
    // Sketch look's paper grain. Missing file falls back to g->white, which makes the paper layer
    // a no-op (gfx_texture_load_exact already logs the failure).
    { char fp[512]; snprintf(fp, sizeof fp, "%s/textures/paper.png", HOLLOW_ASSET_DIR); g->paper = gfx_texture_load_exact(g, fp); }
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
    if (g->pipe_shadow_inst) SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_shadow_inst);
    if (g->pipe_world_inst) SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_world_inst);
    if (g->inst_xfer) SDL_ReleaseGPUTransferBuffer(g->dev, g->inst_xfer); if (g->inst_vb) SDL_ReleaseGPUBuffer(g->dev, g->inst_vb);
    free(g->inst_items);
    if (g->por_hdr) SDL_ReleaseGPUTexture(g->dev, g->por_hdr); if (g->por_depth) SDL_ReleaseGPUTexture(g->dev, g->por_depth);
    if (g->por_comp) SDL_ReleaseGPUTexture(g->dev, g->por_comp); if (g->por_comp_depth) SDL_ReleaseGPUTexture(g->dev, g->por_comp_depth);
    if (g->portrait.tex) SDL_ReleaseGPUTexture(g->dev, g->portrait.tex);
    gfx_texture_destroy(g, &g->paper);
    SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_pixcomp);
}

// ---------------------------------------------------------------- world pass

static void push_frame_uniforms(Gfx *g, const FrameParams *fp);
static void fill_palette(Gfx *g, Vec4 *pal, Sint32 *npal);
static void fullscreen_pass(Gfx *g, SDL_GPUCommandBuffer *cmd, SDL_GPUGraphicsPipeline *pipe, SDL_GPUTexture *dst, const SDL_GPUTextureSamplerBinding *samplers, Uint32 nsamplers, const void *uniforms, Uint32 usize, const SDL_GPUViewport *vpt);
static void push_material(Gfx *g, Vec4 tint, const Texture *t) {
    const Material *m = &g->material;
    MaterialUniforms u = { .tint = v4(tint.x * m->tint.x, tint.y * m->tint.y, tint.z * m->tint.z, tint.w * m->tint.w),
                           .emissive = v4(m->emissive.x, m->emissive.y, m->emissive.z, m->unlit), .rim = v4(m->rim_color.x, m->rim_color.y, m->rim_color.z, m->rim),
                           .water = v4(m->water, m->water_origin.x, m->water_origin.y, m->water_origin.z),
                           .flat = v4(t ? t->mean[0] : 1.0f, t ? t->mean[1] : 1.0f, t ? t->mean[2] : 1.0f, g->flat) };
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
    push_material(g, v4(1, 1, 1, 1), NULL);
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
        g->por_hdr = make_target(g, g->hdr_fmt, size, size, false);
        g->por_comp = make_target(g, g->hdr_fmt, size, size, false);
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
        // designated initializers keep ink/film zero except ink.x: a portrait keeps a one-pixel
        // ink width if outlines are ever turned on for it.
        PostUniforms u = { .params = v4(0, 0, 0, 1), .res = v4((float)g->por_size, (float)g->por_size, 0, 0), .flash = v4(0, 0, 0, 0),
                           .grade = v4(1, 1, 1, 0), .lift = v4(0, 0, 0, 0), .gain = v4(1, 1, 1, 0), .style = v4(0, 0, 0, 1),
                           .ink = v4(1, 0, 0, 0) };
        fill_palette(g, u.pal, u.npal);
        SDL_GPUTextureSamplerBinding sb[4] = { { .texture = g->por_comp, .sampler = g->samp_nearest }, { .texture = g->por_comp, .sampler = g->samp_nearest },
                                                { .texture = g->por_comp_depth, .sampler = g->samp_nearest }, { .texture = g->paper.tex, .sampler = g->samp_linear } };
        fullscreen_pass(g, g->cmd, g->pipe_post, g->portrait.tex, sb, 4, &u, sizeof u, NULL);
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
            g->pix = make_target(g, g->hdr_fmt, g->pw, g->ph, false);
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
    push_material(g, v4(1, 1, 1, 1), NULL);
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
    prof_count(PROF_C_DRAWS, 1); prof_count(PROF_C_INSTANCES, 1);   // a full-screen triangle, no Mesh to count tris from
    repush_frame(g);   // the composite's uniforms sat in the frame slot
}

void gfx_set_material(Gfx *g, const Material *m) { g->material = m ? *m : material_default(); }
void gfx_set_sprite_lean(Gfx *g, float lean) { g->sprite_lean = lean; }
void gfx_set_texture_cap(Gfx *g, int cap) { g->tex_cap = cap; }

int gfx_set_shadow_size(Gfx *g, int size) {
    if (size < 256) size = 256;
    if (size > 4096) size = 4096;
    if (size == g->shadow_size && g->shadow_tex) return g->shadow_size;
    SDL_WaitForGPUIdle(g->dev);   // the old map may still be bound in a frame in flight
    if (g->shadow_tex) SDL_ReleaseGPUTexture(g->dev, g->shadow_tex);
    g->shadow_tex = SDL_CreateGPUTexture(g->dev, &(SDL_GPUTextureCreateInfo){ .type = SDL_GPU_TEXTURETYPE_2D, .format = DEPTH_FMT,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, .width = (Uint32)size, .height = (Uint32)size, .layer_count_or_depth = 1, .num_levels = 1 });
    if (!g->shadow_tex) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "shadow map %d: %s", size, SDL_GetError()); g->shadow_size = 0; return 0; }
    g->shadow_size = size;
    g->shadow_valid = false;
    SDL_Log("shadow map: %dx%d", size, size);
    return size;
}
void gfx_set_flat(Gfx *g, float amount) { g->flat = clampf(amount, 0, 1); }

// Internal render resolution, independent of the fixed uiw/uih the UI is laid out in. SDL_GPU
// defers texture release until the GPU is done with the old ones, so this is safe to call between
// frames; the pass/shadow/portrait guard below is only against calling it mid-frame.
void gfx_set_render_scale(Gfx *g, float scale, bool nearest) {
    if (g->pass || g->in_shadow || g->in_portrait) return;
    if (scale < 0.25f) scale = 0.25f; if (scale > 1.0f) scale = 1.0f;
    int w = (int)(g->uiw * scale + 0.5f), h = (int)(g->uih * scale + 0.5f);
    w &= ~1; h &= ~1;   // even, so the quarter-res bloom targets stay whole
    if (w == g->iw && h == g->ih) { g->render_scale = scale; g->render_nearest = nearest; return; }
    SDL_ReleaseGPUTexture(g->dev, g->hdr); SDL_ReleaseGPUTexture(g->dev, g->depth); SDL_ReleaseGPUTexture(g->dev, g->ldr);
    SDL_ReleaseGPUTexture(g->dev, g->bloom_a); SDL_ReleaseGPUTexture(g->dev, g->bloom_b);
    g->iw = w; g->ih = h; g->bw = w / 4; g->bh = h / 4;
    g->hdr = make_target(g, g->hdr_fmt, w, h, false);
    g->depth = make_target(g, DEPTH_FMT, w, h, true);
    g->ldr = make_target(g, LDR_FMT, w, h, false);
    g->bloom_a = make_target(g, g->hdr_fmt, g->bw, g->bh, false);
    g->bloom_b = make_target(g, g->hdr_fmt, g->bw, g->bh, false);
    // the pixel-art layer is sized off g->iw/scale inside gfx_set_pixel_look, which early-outs
    // when its scale argument is unchanged: force a rebuild at the new size.
    if (g->pix) { SDL_ReleaseGPUTexture(g->dev, g->pix); g->pix = NULL; }
    if (g->pix_depth) { SDL_ReleaseGPUTexture(g->dev, g->pix_depth); g->pix_depth = NULL; }
    g->pixel_scale = 0;
    g->render_scale = scale; g->render_nearest = nearest;
}

// Returns false for a pipeline the driver refused at startup, and every caller drops the draw.
// Binding NULL and drawing anyway trips SDL's "Graphics pipeline not bound!" assert, which with
// SDL_HINT_ASSERT=abort ends the process -- the tester's crash, several frames after the real fault.
static bool bind_pipe(Gfx *g, SDL_GPUGraphicsPipeline *p) {
    if (!p) return false;
    if (g->bound_pipe != p) { SDL_BindGPUGraphicsPipeline(g->pass, p); g->bound_pipe = p; g->bound_tex = NULL; }
    return true;
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
    if (!bind_pipe(g, g->in_shadow ? g->pipe_shadow : g->pipe_world)) return;
    VSUniforms u = { g->frame.view_proj, model, uv_xform, v4(g->planar_next ? 1.0f : 0.0f, 0, 0, 0) };
    SDL_PushGPUVertexUniformData(g->cmd, 0, &u, sizeof u);
    if (!g->in_shadow) push_material(g, tint, t);
    bind_tex(g, t, g->samp_linear);
    SDL_BindGPUVertexBuffers(g->pass, 0, &(SDL_GPUBufferBinding){ .buffer = m->vb }, 1);
    SDL_BindGPUIndexBuffer(g->pass, &(SDL_GPUBufferBinding){ .buffer = m->ib }, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(g->pass, m->index_count, 1, 0, 0, 0);
    g->draw_calls++;
    prof_count(PROF_C_DRAWS, 1); prof_count(PROF_C_INSTANCES, 1); prof_count(PROF_C_TRIS, m->index_count / 3);
}

void gfx_draw_skinned(Gfx *g, const Mesh *m, const Texture *t, Mat4 model, Vec4 tint, const Mat4 *joints, int njoints) {
    if (!g->pass) return;
    if (!bind_pipe(g, g->in_shadow ? g->pipe_shadow_skin : g->pipe_skin)) return;
    VSUniforms u = { g->frame.view_proj, model, v4(1, 1, 0, 0), v4(0, 0, 0, 0) };
    SDL_PushGPUVertexUniformData(g->cmd, 0, &u, sizeof u);
    static Mat4 tmp[64];
    memset(tmp, 0, sizeof tmp);
    memcpy(tmp, joints, (size_t)(njoints > 64 ? 64 : njoints) * sizeof(Mat4));
    SDL_PushGPUVertexUniformData(g->cmd, 1, tmp, sizeof tmp);
    if (!g->in_shadow) push_material(g, tint, t);
    bind_tex(g, t, g->samp_linear);
    SDL_BindGPUVertexBuffers(g->pass, 0, &(SDL_GPUBufferBinding){ .buffer = m->vb }, 1);
    SDL_BindGPUIndexBuffer(g->pass, &(SDL_GPUBufferBinding){ .buffer = m->ib }, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(g->pass, m->index_count, 1, 0, 0, 0);
    g->draw_calls++;
    prof_count(PROF_C_DRAWS, 1); prof_count(PROF_C_INSTANCES, 1); prof_count(PROF_C_TRIS, m->index_count / 3);
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
    if (!bind_pipe(g, g->pipe_world)) return;
    VSUniforms vu = { g->frame.view_proj, m, xf, v4(0, 0, 0, 0) };
    SDL_PushGPUVertexUniformData(g->cmd, 0, &vu, sizeof vu);
    Material saved = g->material; if (g->material.unlit <= 0) g->material.unlit = 0.8f;
    push_material(g, tint, t); g->material = saved;
    // nearest sampling for crisp pixels; force a rebind since the sampler differs
    SDL_BindGPUFragmentSamplers(g->pass, 0, &(SDL_GPUTextureSamplerBinding){ .texture = t->tex, .sampler = g->samp_nearest }, 1);
    g->bound_tex = NULL;
    SDL_BindGPUVertexBuffers(g->pass, 0, &(SDL_GPUBufferBinding){ .buffer = g->quad.vb }, 1);
    SDL_BindGPUIndexBuffer(g->pass, &(SDL_GPUBufferBinding){ .buffer = g->quad.ib }, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(g->pass, 6, 1, 0, 0, 0);
    g->draw_calls++;
    prof_count(PROF_C_DRAWS, 1); prof_count(PROF_C_INSTANCES, 1); prof_count(PROF_C_TRIS, g->quad.index_count / 3);
}

void gfx_draw_box(Gfx *g, const Texture *t, Vec3 center, Vec3 size, float yaw, Vec4 tint, float uv_tile) {
    Vec4 xf = uv_tile > 0 ? v4(uv_tile, 0, 0, 0) : v4(1, 1, 0, 0);
    g->planar_next = uv_tile > 0;
    gfx_draw(g, &g->cube, t, m4_trs(center, yaw, size), tint, xf);
    g->planar_next = false;
}

void gfx_draw_planar(Gfx *g, const Mesh *m, const Texture *t, Mat4 model, Vec4 tint, float tile) {
    g->planar_next = true;
    gfx_draw(g, m, t, model, tint, v4(tile, 0, 0, 0));
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


// ---------------------------------------------------------------- instancing
//
// The island's 9160 draw calls a frame were not 9160 different things. They were two meshes -- a
// box and a cylinder from assets/models/shapes -- drawn three thousand times because a palm is a
// .part file of thirty pieces and the island stands a hundred and fifteen palms. Every one of
// those pieces differs only in its matrix and its green. That is what an instance is.
//
// The whole frame's instances are collected first, into one stream, and uploaded once. They have
// to be: an upload is a copy pass, a copy pass cannot run inside a render pass, and the frame has
// two render passes (the sun's and the camera's) with different culled sets. So the collection
// carries a `set` per instance and the upload happens between the collection and the first pass.
//
// Instances arrive in whatever order the level lists its props, but a draw call needs its
// instances contiguous in the buffer. Rather than sort, this counts: every arriving instance is
// tagged with its batch and the batch's count goes up; at upload the counts become offsets and
// each item is scattered straight into its place. One pass to collect, one to place, no compare.

void gfx_instances_begin(Gfx *g) {
    g->inst_nitems = 0; g->inst_nbatches = 0; g->inst_total = 0; g->inst_overflow = 0;
    g->inst_ready = false; g->inst_open = g->inst_items != NULL && g->inst_vb != NULL;
    for (int i = 0; i < GFX_INST_HASH; i++) g->inst_hash[i] = -1;
}

// The batch key, packed so it can be hashed and compared as six 64-bit words rather than byte by
// byte: this runs once per instance and the island queues six thousand a frame, which is the one
// place in the collection where a byte loop showed up in the profile.
typedef struct InstKey InstKey;   // the layout lives in gfx.h, inside GfxInstBatch

int gfx_instance_batch(Gfx *g, GfxInstSet set, const Mesh *mesh, const Texture *tex, Vec4 uv_xform, bool planar, Vec3 glow) {
    if (!g->inst_open || !mesh || !mesh->vb || !mesh->index_count) return -1;
    if (!tex) tex = &g->white;
    InstKey k;
    memset(&k, 0, sizeof k);   // no padding garbage: the whole struct is compared
    k.mesh = mesh; k.tex = tex;
    k.uv[0] = uv_xform.x; k.uv[1] = uv_xform.y; k.uv[2] = uv_xform.z; k.uv[3] = uv_xform.w;
    k.glow[0] = glow.x; k.glow[1] = glow.y; k.glow[2] = glow.z;
    k.set_planar = (Uint32)set | (planar ? 0x100u : 0u);

    Uint64 h = 1469598103934665603ull;
    const Uint64 *w = (const Uint64 *)(const void *)&k;
    for (size_t i = 0; i < sizeof k / 8; i++) { h ^= w[i]; h *= 1099511628211ull; }

    // Open addressing, linear probe. The table is at least twice the batch ceiling, so an empty
    // slot always exists and the loop always ends.
    int slot = (int)((h ^ (h >> 32)) & (GFX_INST_HASH - 1));
    for (int probe = 0; probe < GFX_INST_HASH; probe++) {
        int c = g->inst_hash[slot];
        if (c < 0) {   // empty: this is where a new batch's index goes
            if (g->inst_nbatches >= GFX_MAX_INST_BATCHES) { g->inst_overflow++; return -1; }
            int b = g->inst_nbatches++;
            g->inst_hash[slot] = b;
            struct GfxInstBatch *bb = &g->inst_batches[b];
            bb->key = k; bb->mesh = mesh; bb->tex = tex; bb->uv_xform = uv_xform; bb->glow = glow;
            bb->set = (Uint8)set; bb->planar = planar ? 1 : 0; bb->count = 0; bb->first = 0;
            return b;
        }
        if (memcmp(&g->inst_batches[c].key, &k, sizeof k) == 0) return c;
        slot = (slot + 1) & (GFX_INST_HASH - 1);
    }
    g->inst_overflow++;
    return -1;
}

void gfx_instance_add(Gfx *g, int batch, Mat4 model, Vec4 tint) {
    if (batch < 0 || !g->inst_open) return;
    if (g->inst_nitems >= GFX_MAX_INSTANCES) { g->inst_overflow++; return; }
    g->inst_batches[batch].count++;
    struct GfxInstItem *it = &g->inst_items[g->inst_nitems++];
    it->batch = batch;
    memcpy(it->data.model, model.m, sizeof it->data.model);
    it->data.tint[0] = tint.x; it->data.tint[1] = tint.y; it->data.tint[2] = tint.z; it->data.tint[3] = tint.w;
}

void gfx_instance(Gfx *g, GfxInstSet set, const Mesh *mesh, const Texture *tex, Mat4 model, Vec4 tint, Vec4 uv_xform, bool planar, Vec3 glow) {
    gfx_instance_add(g, gfx_instance_batch(g, set, mesh, tex, uv_xform, planar, glow), model, tint);
}

void gfx_instances_upload(Gfx *g, Platform *pf) {
    g->inst_open = false; g->inst_ready = false;
    if (g->inst_overflow)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "instances: %u dropped this frame (stream holds %d in %d batches)", g->inst_overflow, GFX_MAX_INSTANCES, GFX_MAX_INST_BATCHES);
    if (!pf->cmd || !g->inst_nitems || !g->inst_vb || !g->inst_xfer) return;

    Uint32 run = 0;
    for (int i = 0; i < g->inst_nbatches; i++) { g->inst_batches[i].first = run; run += g->inst_batches[i].count; }
    g->inst_total = run;
    static Uint32 cursor[GFX_MAX_INST_BATCHES];   // one frame's write heads; never on the stack
    for (int i = 0; i < g->inst_nbatches; i++) cursor[i] = g->inst_batches[i].first;

    // cycle: hand back a fresh block rather than wait for last frame's copy to be done with this one.
    GfxInstance *map = (GfxInstance *)SDL_MapGPUTransferBuffer(g->dev, g->inst_xfer, true);
    if (!map) { g->inst_total = 0; return; }
    for (Uint32 i = 0; i < g->inst_nitems; i++) {
        const struct GfxInstItem *it = &g->inst_items[i];
        map[cursor[it->batch]++] = it->data;
    }
    SDL_UnmapGPUTransferBuffer(g->dev, g->inst_xfer);

    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(pf->cmd);
    SDL_UploadToGPUBuffer(cp, &(SDL_GPUTransferBufferLocation){ .transfer_buffer = g->inst_xfer },
                          &(SDL_GPUBufferRegion){ .buffer = g->inst_vb, .size = run * (Uint32)sizeof(GfxInstance) }, true);
    SDL_EndGPUCopyPass(cp);
    g->inst_ready = true;
}

void gfx_instances_draw(Gfx *g, GfxInstSet set) {
    if (!g->pass || !g->inst_ready) return;
    SDL_GPUGraphicsPipeline *pipe = g->in_shadow ? g->pipe_shadow_inst : g->pipe_world_inst;
    if (!pipe) return;
    Material saved = g->material;
    for (int i = 0; i < g->inst_nbatches; i++) {
        const struct GfxInstBatch *b = &g->inst_batches[i];
        if (b->set != (Uint8)set || b->count == 0) continue;
        if (!bind_pipe(g, pipe)) break;
        VSUniforms u = { g->frame.view_proj, m4_identity(), b->uv_xform, v4(b->planar ? 1.0f : 0.0f, 0, 0, 0) };
        SDL_PushGPUVertexUniformData(g->cmd, 0, &u, sizeof u);
        if (!g->in_shadow) {
            // The per-instance tint rides in the vertex colour, so the material's own tint stays
            // white; only the glow differs from batch to batch, and it is part of the batch key.
            g->material = material_default(); g->material.emissive = b->glow;
            push_material(g, v4(1, 1, 1, 1), b->tex);
        }
        bind_tex(g, b->tex, g->samp_linear);
        SDL_GPUBufferBinding vbs[2] = { { .buffer = b->mesh->vb },
                                        { .buffer = g->inst_vb, .offset = b->first * (Uint32)sizeof(GfxInstance) } };
        SDL_BindGPUVertexBuffers(g->pass, 0, vbs, 2);
        SDL_BindGPUIndexBuffer(g->pass, &(SDL_GPUBufferBinding){ .buffer = b->mesh->ib }, SDL_GPU_INDEXELEMENTSIZE_16BIT);
        SDL_DrawGPUIndexedPrimitives(g->pass, b->mesh->index_count, b->count, 0, 0, 0);
        g->draw_calls++;
        prof_count(PROF_C_DRAWS, 1); prof_count(PROF_C_BATCHES, 1);
        prof_count(PROF_C_INSTANCES, b->count);
        prof_count(PROF_C_TRIS, b->mesh->index_count / 3 * b->count);
    }
    g->material = saved;
    g->bound_tex = NULL;   // the next plain gfx_draw must rebind: this loop left whatever it liked
}

void gfx_instances_stats(const Gfx *g, GfxInstSet set, unsigned *batches, unsigned *instances) {
    unsigned nb = 0, ni = 0;
    for (int i = 0; i < g->inst_nbatches; i++)
        if (g->inst_batches[i].set == (Uint8)set && g->inst_batches[i].count) { nb++; ni += g->inst_batches[i].count; }
    if (batches) *batches = nb;
    if (instances) *instances = ni;
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
    if (g->nfonts >= GFX_UI_FONTS) return &g->fonts[0];
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
void gfx_ui_text_px(Gfx *g, float x, float y, int px, Vec4 color, const char *text) {
    if (g->ttf) { font_draw(g, x, y, px, color, text); return; }
    gfx_ui_text(g, x, y, (float)px / 17.0f, color, text);
}
float gfx_ui_text_px_width(Gfx *g, int px, const char *text) {
    if (g->ttf) return font_width(g, px, text);
    return stb_easy_font_width((char *)text) * ((float)px / 17.0f);
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
    if (!pass) return;
    // A pipeline the driver refused: the clear above still happened, so the target is defined
    // rather than whatever was in that memory, and nothing is handed to SDL that it will assert on.
    if (!pipe) { SDL_EndGPURenderPass(pass); return; }
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
        if (!bind_pipe(g, g->pipe_sky)) return;
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
        if (g->p_alpha_count && bind_pipe(g, g->pipe_particle_alpha)) {
            bind_tex(g, &g->soft, g->samp_clamp);
            SDL_BindGPUVertexBuffers(g->pass, 0, &(SDL_GPUBufferBinding){ .buffer = g->p_vb, .offset = 0 }, 1);
            SDL_DrawGPUPrimitives(g->pass, g->p_alpha_count, 1, 0, 0);
        }
        if (g->p_add_count && bind_pipe(g, g->pipe_particle_add)) {
            bind_tex(g, &g->soft, g->samp_clamp);
            SDL_BindGPUVertexBuffers(g->pass, 0, &(SDL_GPUBufferBinding){ .buffer = g->p_vb, .offset = (Uint32)(P_MAX_VERTS * sizeof(PVertex)) }, 1);
            SDL_DrawGPUPrimitives(g->pass, g->p_add_count, 1, 0, 0);
        }
    }
    SDL_EndGPURenderPass(g->pass); g->pass = NULL;

    // Bloom: bright extract to quarter res, then two blur ping-pongs. Skipped entirely at bloom 0
    // -- four full-screen passes a level that wants no bloom no longer pays for. post.frag already
    // skips the bloom fetch when grade.w is zero, so the stale bloom_a target is never read.
    if (pp->bloom > 0.0f) {
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
            .style = v4(pp->style_snap, pp->style_outline, pp->style_levels, pp->style_pixel),
            .ink  = v4(pp->ink_width, pp->ink_wobble, pp->ink_luma, pp->paper),
            .film = v4(pp->chroma, pp->dither, pp->hatch, (float)(g->paper.w > 0 ? g->paper.w : 512)) };
        fill_palette(g, u.pal, u.npal);
        SDL_GPUTextureSamplerBinding s[4] = { { .texture = g->hdr, .sampler = g->samp_clamp }, { .texture = g->bloom_a, .sampler = g->samp_clamp },
            { .texture = g->depth, .sampler = g->samp_nearest }, { .texture = g->paper.tex, .sampler = g->samp_linear } };
        fullscreen_pass(g, pf->cmd, g->pipe_post, g->ldr, s, 4, &u, sizeof u, NULL);
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
        // the UI is laid out in the fixed uiw/uih space, not the (possibly scaled-down) render
        // resolution -- see gfx_set_render_scale.
        SDL_PushGPUVertexUniformData(pf->cmd, 0, &(Vec4){ (float)g->uiw, (float)g->uih, 0, 0 }, sizeof(Vec4));
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
        // At half internal resolution the choice between a soft bilinear upscale and hard
        // nearest-neighbour blocks is most of the difference between "cheap camera" and "retro".
        fullscreen_pass(g, pf->cmd, g->pipe_blit, pf->swapchain, &(SDL_GPUTextureSamplerBinding){ .texture = g->ldr, .sampler = g->render_nearest ? g->samp_nearest : g->samp_clamp }, 1, NULL, 0, &vpt);
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
