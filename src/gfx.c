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

// ---------------------------------------------------------------- shaders

typedef struct WorldUniforms {
    Mat4 view_proj, model;
    Vec4 tint, fog_params, light_dir, light_color, uv_xform;
} WorldUniforms;

static SDL_GPUShader *load_shader(Gfx *g, const char *name, SDL_GPUShaderStage stage,
                                  Uint32 samplers, Uint32 uniforms) {
    SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(g->dev);
    const char *ext, *entry;
    SDL_GPUShaderFormat fmt;
    if (formats & SDL_GPU_SHADERFORMAT_MSL)        { ext = "msl";  entry = "main0"; fmt = SDL_GPU_SHADERFORMAT_MSL; }
    else if (formats & SDL_GPU_SHADERFORMAT_SPIRV) { ext = "spv";  entry = "main";  fmt = SDL_GPU_SHADERFORMAT_SPIRV; }
    else if (formats & SDL_GPU_SHADERFORMAT_DXIL)  { ext = "dxil"; entry = "main";  fmt = SDL_GPU_SHADERFORMAT_DXIL; }
    else { SDL_SetError("no supported shader format"); return NULL; }

    char path[512];
    snprintf(path, sizeof path, "%s/shaders/%s.%s", HOLLOW_ASSET_DIR, name, ext);
    size_t size = 0;
    void *code = SDL_LoadFile(path, &size);
    if (!code) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "shader missing: %s", path); return NULL; }

    SDL_GPUShaderCreateInfo ci = {
        .code = code, .code_size = size, .entrypoint = entry, .format = fmt, .stage = stage,
        .num_samplers = samplers, .num_uniform_buffers = uniforms,
    };
    SDL_GPUShader *sh = SDL_CreateGPUShader(g->dev, &ci);
    SDL_free(code);
    if (!sh) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "shader %s: %s", name, SDL_GetError());
    return sh;
}

// ---------------------------------------------------------------- buffers

static bool upload(Gfx *g, SDL_GPUBuffer *dst, const void *data, Uint32 size) {
    SDL_GPUTransferBufferCreateInfo tci = { .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = size };
    SDL_GPUTransferBuffer *xfer = SDL_CreateGPUTransferBuffer(g->dev, &tci);
    if (!xfer) return false;
    void *map = SDL_MapGPUTransferBuffer(g->dev, xfer, false);
    memcpy(map, data, size);
    SDL_UnmapGPUTransferBuffer(g->dev, xfer);
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g->dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUBuffer(cp, &(SDL_GPUTransferBufferLocation){ .transfer_buffer = xfer },
                          &(SDL_GPUBufferRegion){ .buffer = dst, .size = size }, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(g->dev, xfer);
    return true;
}

Mesh gfx_mesh_create(Gfx *g, const Vertex *v, Uint32 nv, const Uint16 *idx, Uint32 ni) {
    Mesh m = {0};
    m.vb = SDL_CreateGPUBuffer(g->dev, &(SDL_GPUBufferCreateInfo){ .usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = nv * sizeof *v });
    m.ib = SDL_CreateGPUBuffer(g->dev, &(SDL_GPUBufferCreateInfo){ .usage = SDL_GPU_BUFFERUSAGE_INDEX, .size = ni * sizeof *idx });
    upload(g, m.vb, v, nv * sizeof *v);
    upload(g, m.ib, idx, ni * sizeof *idx);
    m.index_count = ni;
    return m;
}

void gfx_mesh_destroy(Gfx *g, Mesh *m) {
    if (m->vb) SDL_ReleaseGPUBuffer(g->dev, m->vb);
    if (m->ib) SDL_ReleaseGPUBuffer(g->dev, m->ib);
    memset(m, 0, sizeof *m);
}

Texture gfx_texture_create(Gfx *g, const unsigned char *rgba, int w, int h) {
    Texture t = { .w = w, .h = h };
    t.tex = SDL_CreateGPUTexture(g->dev, &(SDL_GPUTextureCreateInfo){
        .type = SDL_GPU_TEXTURETYPE_2D, .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER, .width = w, .height = h,
        .layer_count_or_depth = 1, .num_levels = 1 });
    Uint32 size = (Uint32)(w * h * 4);
    SDL_GPUTransferBuffer *xfer = SDL_CreateGPUTransferBuffer(g->dev,
        &(SDL_GPUTransferBufferCreateInfo){ .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = size });
    void *map = SDL_MapGPUTransferBuffer(g->dev, xfer, false);
    memcpy(map, rgba, size);
    SDL_UnmapGPUTransferBuffer(g->dev, xfer);
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g->dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_UploadToGPUTexture(cp, &(SDL_GPUTextureTransferInfo){ .transfer_buffer = xfer },
                           &(SDL_GPUTextureRegion){ .texture = t.tex, .w = w, .h = h, .d = 1 }, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(g->dev, xfer);
    return t;
}

// Box-filter downsample by integer factor so imported art lands at PS2 texture sizes.
Texture gfx_texture_load(Gfx *g, const char *path, int max_size) {
    int w, h, n;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    if (!px) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "texture load failed: %s", path); return g->white; }
    int f = 1;
    while ((w / f) > max_size || (h / f) > max_size) f *= 2;
    int dw = w / f, dh = h / f;
    unsigned char *out = px;
    if (f > 1) {
        out = malloc((size_t)dw * dh * 4);
        for (int y = 0; y < dh; y++) for (int x = 0; x < dw; x++) for (int c = 0; c < 4; c++) {
            int sum = 0;
            for (int yy = 0; yy < f; yy++) for (int xx = 0; xx < f; xx++)
                sum += px[((y * f + yy) * w + (x * f + xx)) * 4 + c];
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

// ---------------------------------------------------------------- init

static Mesh make_cube(Gfx *g) {
    // 6 faces * 4 verts, unit cube [-0.5, 0.5]
    static const float N[6][3] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    static const float U[6][3] = {{1,0,0},{-1,0,0},{0,0,-1},{0,0,1},{1,0,0},{1,0,0}};
    static const float V[6][3] = {{0,1,0},{0,1,0},{0,1,0},{0,1,0},{0,0,-1},{0,0,1}};
    Vertex v[24]; Uint16 idx[36];
    for (int f = 0; f < 6; f++) {
        for (int i = 0; i < 4; i++) {
            float su = (i == 1 || i == 2) ? 0.5f : -0.5f;
            float sv = (i >= 2) ? 0.5f : -0.5f;
            Vertex *p = &v[f * 4 + i];
            for (int k = 0; k < 3; k++) {
                p->pos[k] = N[f][k] * 0.5f + U[f][k] * su + V[f][k] * sv;
                p->normal[k] = N[f][k];
            }
            p->uv[0] = su + 0.5f; p->uv[1] = 0.5f - sv;
            p->color[0] = p->color[1] = p->color[2] = p->color[3] = 1.0f;
        }
        Uint16 b = (Uint16)(f * 4);
        Uint16 *o = &idx[f * 6];
        o[0] = b; o[1] = b + 1; o[2] = b + 2; o[3] = b; o[4] = b + 2; o[5] = b + 3;
    }
    return gfx_mesh_create(g, v, 24, idx, 36);
}

static bool create_targets(Gfx *g) {
    g->color = SDL_CreateGPUTexture(g->dev, &(SDL_GPUTextureCreateInfo){
        .type = SDL_GPU_TEXTURETYPE_2D, .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = g->iw, .height = g->ih, .layer_count_or_depth = 1, .num_levels = 1 });
    g->depth = SDL_CreateGPUTexture(g->dev, &(SDL_GPUTextureCreateInfo){
        .type = SDL_GPU_TEXTURETYPE_2D, .format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        .width = g->iw, .height = g->ih, .layer_count_or_depth = 1, .num_levels = 1 });
    return g->color && g->depth;
}

bool gfx_init(Gfx *g, Platform *pf, int iw, int ih) {
    memset(g, 0, sizeof *g);
    g->dev = pf->gpu; g->iw = iw; g->ih = ih;
    if (!create_targets(g)) return false;

    g->samp_nearest = SDL_CreateGPUSampler(g->dev, &(SDL_GPUSamplerCreateInfo){
        .min_filter = SDL_GPU_FILTER_NEAREST, .mag_filter = SDL_GPU_FILTER_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT, .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT });
    g->samp_linear = SDL_CreateGPUSampler(g->dev, &(SDL_GPUSamplerCreateInfo){
        .min_filter = SDL_GPU_FILTER_LINEAR, .mag_filter = SDL_GPU_FILTER_LINEAR,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT, .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT });

    SDL_GPUShader *wv = load_shader(g, "world.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader *wf = load_shader(g, "world.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    SDL_GPUShader *uv = load_shader(g, "ui.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
    SDL_GPUShader *uf = load_shader(g, "ui.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
    SDL_GPUShader *pv = load_shader(g, "post.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
    SDL_GPUShader *pfr = load_shader(g, "post.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
    if (!wv || !wf || !uv || !uf || !pv || !pfr) return false;

    SDL_GPUColorTargetDescription offscreen_ct = { .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM };
    SDL_GPUColorTargetDescription blend_ct = { .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .blend_state = { .enable_blend = true,
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA, .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE, .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD } };
    SDL_GPUColorTargetDescription swap_ct = { .format = SDL_GetGPUSwapchainTextureFormat(g->dev, pf->window) };

    SDL_GPUVertexAttribute world_attrs[] = {
        { .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 0 },
        { .location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 12 },
        { .location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 24 },
        { .location = 3, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 32 },
    };
    SDL_GPUVertexBufferDescription world_vb = { .slot = 0, .pitch = sizeof(Vertex), .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX };
    g->pipe_world = SDL_CreateGPUGraphicsPipeline(g->dev, &(SDL_GPUGraphicsPipelineCreateInfo){
        .vertex_shader = wv, .fragment_shader = wf,
        .vertex_input_state = { .vertex_buffer_descriptions = &world_vb, .num_vertex_buffers = 1,
                                .vertex_attributes = world_attrs, .num_vertex_attributes = 4 },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = { .fill_mode = SDL_GPU_FILLMODE_FILL, .cull_mode = SDL_GPU_CULLMODE_BACK,
                              .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE },
        .depth_stencil_state = { .enable_depth_test = true, .enable_depth_write = true,
                                 .compare_op = SDL_GPU_COMPAREOP_LESS },
        .target_info = { .color_target_descriptions = &offscreen_ct, .num_color_targets = 1,
                         .has_depth_stencil_target = true, .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM } });

    SDL_GPUVertexAttribute ui_attrs[] = {
        { .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 0 },
        { .location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = 8 },
        { .location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = 16 },
    };
    SDL_GPUVertexBufferDescription ui_vb = { .slot = 0, .pitch = sizeof(UIVertex), .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX };
    g->pipe_ui = SDL_CreateGPUGraphicsPipeline(g->dev, &(SDL_GPUGraphicsPipelineCreateInfo){
        .vertex_shader = uv, .fragment_shader = uf,
        .vertex_input_state = { .vertex_buffer_descriptions = &ui_vb, .num_vertex_buffers = 1,
                                .vertex_attributes = ui_attrs, .num_vertex_attributes = 3 },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = { .fill_mode = SDL_GPU_FILLMODE_FILL, .cull_mode = SDL_GPU_CULLMODE_NONE },
        .target_info = { .color_target_descriptions = &blend_ct, .num_color_targets = 1 } });

    g->pipe_post = SDL_CreateGPUGraphicsPipeline(g->dev, &(SDL_GPUGraphicsPipelineCreateInfo){
        .vertex_shader = pv, .fragment_shader = pfr,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state = { .fill_mode = SDL_GPU_FILLMODE_FILL, .cull_mode = SDL_GPU_CULLMODE_NONE },
        .target_info = { .color_target_descriptions = &swap_ct, .num_color_targets = 1 } });

    SDL_ReleaseGPUShader(g->dev, wv); SDL_ReleaseGPUShader(g->dev, wf);
    SDL_ReleaseGPUShader(g->dev, uv); SDL_ReleaseGPUShader(g->dev, uf);
    SDL_ReleaseGPUShader(g->dev, pv); SDL_ReleaseGPUShader(g->dev, pfr);
    if (!g->pipe_world || !g->pipe_ui || !g->pipe_post) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "pipeline: %s", SDL_GetError());
        return false;
    }

    g->ui_vb = SDL_CreateGPUBuffer(g->dev, &(SDL_GPUBufferCreateInfo){ .usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = UI_MAX_VERTS * sizeof(UIVertex) });
    g->ui_xfer = SDL_CreateGPUTransferBuffer(g->dev, &(SDL_GPUTransferBufferCreateInfo){ .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = UI_MAX_VERTS * sizeof(UIVertex) });
    g->ui_verts = malloc(UI_MAX_VERTS * sizeof(UIVertex));

    unsigned char white[4] = {255, 255, 255, 255};
    g->white = gfx_texture_create(g, white, 1, 1);
    g->cube = make_cube(g);
    return true;
}

void gfx_shutdown(Gfx *g) {
    gfx_mesh_destroy(g, &g->cube);
    gfx_texture_destroy(g, &g->white);
    free(g->ui_verts);
    SDL_ReleaseGPUTransferBuffer(g->dev, g->ui_xfer);
    SDL_ReleaseGPUBuffer(g->dev, g->ui_vb);
    SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_world);
    SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_ui);
    SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipe_post);
    SDL_ReleaseGPUSampler(g->dev, g->samp_nearest);
    SDL_ReleaseGPUSampler(g->dev, g->samp_linear);
    SDL_ReleaseGPUTexture(g->dev, g->color);
    SDL_ReleaseGPUTexture(g->dev, g->depth);
}

// ---------------------------------------------------------------- world pass

void gfx_begin(Gfx *g, Platform *pf, const FrameParams *fp) {
    g->frame = *fp;
    g->ui_count = 0;
    g->draw_calls = 0;
    g->bound_tex = NULL;
    g->pass = NULL;
    g->cmd = pf->cmd;
    if (!pf->cmd) return;
    SDL_GPUColorTargetInfo ct = { .texture = g->color, .load_op = SDL_GPU_LOADOP_CLEAR, .store_op = SDL_GPU_STOREOP_STORE,
        .clear_color = { fp->fog_color.x, fp->fog_color.y, fp->fog_color.z, 1 } };
    SDL_GPUDepthStencilTargetInfo dt = { .texture = g->depth, .load_op = SDL_GPU_LOADOP_CLEAR, .store_op = SDL_GPU_STOREOP_DONT_CARE,
        .stencil_load_op = SDL_GPU_LOADOP_DONT_CARE, .stencil_store_op = SDL_GPU_STOREOP_DONT_CARE,
        .clear_depth = 1.0f, .cycle = true };
    g->pass = SDL_BeginGPURenderPass(pf->cmd, &ct, 1, &dt);
    SDL_BindGPUGraphicsPipeline(g->pass, g->pipe_world);
    SDL_PushGPUFragmentUniformData(pf->cmd, 0, &(Vec4){ fp->fog_color.x, fp->fog_color.y, fp->fog_color.z, 1 }, sizeof(Vec4));
}

void gfx_draw(Gfx *g, const Mesh *m, const Texture *t, Mat4 model, Vec4 tint, Vec4 uv_xform) {
    if (!g->pass) return;
    WorldUniforms u = {
        .view_proj = g->frame.view_proj, .model = model, .tint = tint,
        .fog_params = v4(g->frame.fog_near, g->frame.fog_far, 0, 0),
        .light_dir = v4(g->frame.light_dir.x, g->frame.light_dir.y, g->frame.light_dir.z, g->frame.ambient),
        .light_color = v4(g->frame.light_color.x, g->frame.light_color.y, g->frame.light_color.z, 1),
        .uv_xform = uv_xform,
    };
    SDL_PushGPUVertexUniformData(g->cmd, 0, &u, sizeof u);
    if (t != g->bound_tex) {
        SDL_BindGPUFragmentSamplers(g->pass, 0, &(SDL_GPUTextureSamplerBinding){ .texture = t->tex, .sampler = g->samp_nearest }, 1);
        g->bound_tex = t;
    }
    SDL_BindGPUVertexBuffers(g->pass, 0, &(SDL_GPUBufferBinding){ .buffer = m->vb }, 1);
    SDL_BindGPUIndexBuffer(g->pass, &(SDL_GPUBufferBinding){ .buffer = m->ib }, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(g->pass, m->index_count, 1, 0, 0, 0);
    g->draw_calls++;
}

void gfx_draw_box(Gfx *g, const Texture *t, Vec3 center, Vec3 size, float yaw, Vec4 tint, float uv_tile) {
    // uv_tile > 0: world-space planar mapping at that many repeats per metre (see world.vert).
    // uv_tile <= 0: the texture is stretched once across each face.
    Vec4 xf = uv_tile > 0 ? v4(-uv_tile, 0, 0, 0) : v4(1, 1, 0, 0);
    gfx_draw(g, &g->cube, t, m4_trs(center, yaw, size), tint, xf);
}

void gfx_set_ambient(Gfx *g, float ambient) { g->frame.ambient = ambient; }

void gfx_draw_box_wire(Gfx *g, Vec3 c, Vec3 s, Vec4 color) {
    const float th = 0.02f;
    float hx = s.x * 0.5f, hy = s.y * 0.5f, hz = s.z * 0.5f;
    for (int i = 0; i < 4; i++) {
        float a = (i & 1) ? 1 : -1, b = (i & 2) ? 1 : -1;
        gfx_draw(g, &g->cube, &g->white, m4_trs(v3(c.x, c.y + a * hy, c.z + b * hz), 0, v3(s.x, th, th)), color, v4(1, 1, 0, 0));
        gfx_draw(g, &g->cube, &g->white, m4_trs(v3(c.x + a * hx, c.y, c.z + b * hz), 0, v3(th, s.y, th)), color, v4(1, 1, 0, 0));
        gfx_draw(g, &g->cube, &g->white, m4_trs(v3(c.x + a * hx, c.y + b * hy, c.z), 0, v3(th, th, s.z)), color, v4(1, 1, 0, 0));
    }
}

// ---------------------------------------------------------------- ui

static void ui_push(Gfx *g, float x, float y, float u, float v, Vec4 c) {
    if (g->ui_count >= UI_MAX_VERTS) return;
    UIVertex *o = &g->ui_verts[g->ui_count++];
    o->pos[0] = x; o->pos[1] = y; o->uv[0] = u; o->uv[1] = v;
    o->color[0] = c.x; o->color[1] = c.y; o->color[2] = c.z; o->color[3] = c.w;
}

void gfx_ui_rect(Gfx *g, float x, float y, float w, float h, Vec4 c) {
    ui_push(g, x, y, 0, 0, c);         ui_push(g, x + w, y, 1, 0, c);     ui_push(g, x + w, y + h, 1, 1, c);
    ui_push(g, x, y, 0, 0, c);         ui_push(g, x + w, y + h, 1, 1, c); ui_push(g, x, y + h, 0, 1, c);
}

void gfx_ui_text(Gfx *g, float x, float y, float scale, Vec4 c, const char *text) {
    static char buf[64 * 1024];
    int quads = stb_easy_font_print(0, 0, (char *)text, NULL, buf, sizeof buf);
    const float *q = (const float *)buf;  // 4 verts per quad, 16 bytes each: x y z + rgba bytes
    for (int i = 0; i < quads; i++) {
        float px[4], py[4];
        for (int k = 0; k < 4; k++) { px[k] = x + q[(i * 4 + k) * 4 + 0] * scale; py[k] = y + q[(i * 4 + k) * 4 + 1] * scale; }
        ui_push(g, px[0], py[0], 0, 0, c); ui_push(g, px[1], py[1], 0, 0, c); ui_push(g, px[2], py[2], 0, 0, c);
        ui_push(g, px[0], py[0], 0, 0, c); ui_push(g, px[2], py[2], 0, 0, c); ui_push(g, px[3], py[3], 0, 0, c);
    }
}

float gfx_ui_text_width(float scale, const char *text) { return stb_easy_font_width((char *)text) * scale; }

// ---------------------------------------------------------------- end of frame

void gfx_end(Gfx *g, Platform *pf, const PostParams *pp, double time) {
    if (!pf->cmd) return;
    if (g->pass) { SDL_EndGPURenderPass(g->pass); g->pass = NULL; }

    // UI: upload the quad list, draw it over the offscreen image.
    if (g->ui_count > 0) {
        void *map = SDL_MapGPUTransferBuffer(g->dev, g->ui_xfer, true);
        memcpy(map, g->ui_verts, g->ui_count * sizeof(UIVertex));
        SDL_UnmapGPUTransferBuffer(g->dev, g->ui_xfer);
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(pf->cmd);
        SDL_UploadToGPUBuffer(cp, &(SDL_GPUTransferBufferLocation){ .transfer_buffer = g->ui_xfer },
                              &(SDL_GPUBufferRegion){ .buffer = g->ui_vb, .size = g->ui_count * sizeof(UIVertex) }, true);
        SDL_EndGPUCopyPass(cp);

        SDL_GPUColorTargetInfo ct = { .texture = g->color, .load_op = SDL_GPU_LOADOP_LOAD, .store_op = SDL_GPU_STOREOP_STORE };
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(pf->cmd, &ct, 1, NULL);
        SDL_BindGPUGraphicsPipeline(pass, g->pipe_ui);
        SDL_PushGPUVertexUniformData(pf->cmd, 0, &(Vec4){ (float)g->iw, (float)g->ih, 0, 0 }, sizeof(Vec4));
        SDL_BindGPUFragmentSamplers(pass, 0, &(SDL_GPUTextureSamplerBinding){ .texture = g->white.tex, .sampler = g->samp_nearest }, 1);
        SDL_BindGPUVertexBuffers(pass, 0, &(SDL_GPUBufferBinding){ .buffer = g->ui_vb }, 1);
        SDL_DrawGPUPrimitives(pass, g->ui_count, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
    }

    // Post: letterboxed upscale to the swapchain.
    if (pf->swapchain) {
        SDL_GPUColorTargetInfo ct = { .texture = pf->swapchain, .load_op = SDL_GPU_LOADOP_CLEAR, .store_op = SDL_GPU_STOREOP_STORE,
                                      .clear_color = { 0, 0, 0, 1 } };
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(pf->cmd, &ct, 1, NULL);
        float target_aspect = (float)g->iw / (float)g->ih;
        float sw = (float)pf->swap_w, sh = (float)pf->swap_h;
        float vw = sw, vh = sw / target_aspect;
        if (vh > sh) { vh = sh; vw = sh * target_aspect; }
        SDL_SetGPUViewport(pass, &(SDL_GPUViewport){ .x = (sw - vw) * 0.5f, .y = (sh - vh) * 0.5f, .w = vw, .h = vh, .min_depth = 0, .max_depth = 1 });
        SDL_BindGPUGraphicsPipeline(pass, g->pipe_post);
        struct { Vec4 params, res; } u = { v4((float)time, pp->grain, pp->vignette, pp->fade), v4((float)g->iw, (float)g->ih, 0, 0) };
        SDL_PushGPUFragmentUniformData(pf->cmd, 0, &u, sizeof u);
        SDL_BindGPUFragmentSamplers(pass, 0, &(SDL_GPUTextureSamplerBinding){ .texture = g->color, .sampler = g->samp_nearest }, 1);
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
    }
}

bool gfx_screenshot(Gfx *g, const char *path) {
    Uint32 size = (Uint32)(g->iw * g->ih * 4);
    SDL_GPUTransferBuffer *xfer = SDL_CreateGPUTransferBuffer(g->dev,
        &(SDL_GPUTransferBufferCreateInfo){ .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, .size = size });
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g->dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_DownloadFromGPUTexture(cp, &(SDL_GPUTextureRegion){ .texture = g->color, .w = g->iw, .h = g->ih, .d = 1 },
                               &(SDL_GPUTextureTransferInfo){ .transfer_buffer = xfer });
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(g->dev, true, &fence, 1);
    SDL_ReleaseGPUFence(g->dev, fence);
    void *map = SDL_MapGPUTransferBuffer(g->dev, xfer, false);
    int ok = stbi_write_png(path, g->iw, g->ih, 4, map, g->iw * 4);
    SDL_UnmapGPUTransferBuffer(g->dev, xfer);
    SDL_ReleaseGPUTransferBuffer(g->dev, xfer);
    return ok != 0;
}
