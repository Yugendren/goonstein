#include "model.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CGLTF_IMPLEMENTATION
#include "vendor/cgltf.h"
#include "vendor/stb_image.h"

// ---------------------------------------------------------------- loading

// Decoded pixels of the last texture made by texture_from_memory / texture_from_file (kept for recolouring).
static unsigned char *last_px; static int last_w, last_h;
static Texture texture_from_memory(Gfx *g, const unsigned char *bytes, size_t size, int max_size) {
    int w, h, n;
    unsigned char *px = stbi_load_from_memory(bytes, (int)size, &w, &h, &n, 4);
    if (!px) return g->white;
    int f = 1;
    while ((w / f) > max_size || (h / f) > max_size) f *= 2;
    int dw = w / f, dh = h / f;
    unsigned char *out = px;
    if (f > 1) {
        out = malloc((size_t)dw * dh * 4);
        for (int y = 0; y < dh; y++) for (int x = 0; x < dw; x++) for (int c = 0; c < 4; c++) {
            int sum = 0;
            for (int yy = 0; yy < f; yy++) for (int xx = 0; xx < f; xx++) sum += px[((y * f + yy) * w + (x * f + xx)) * 4 + c];
            out[(y * dw + x) * 4 + c] = (unsigned char)(sum / (f * f));
        }
    }
    Texture t = gfx_texture_create(g, out, dw, dh);
    free(last_px); last_px = malloc((size_t)dw * dh * 4); memcpy(last_px, out, (size_t)dw * dh * 4); last_w = dw; last_h = dh;
    if (out != px) free(out);
    stbi_image_free(px);
    return t;
}
static Texture texture_from_file(Gfx *g, const char *path, int max_size) {
    size_t size = 0; void *bytes = SDL_LoadFile(path, &size);
    if (!bytes) return gfx_texture_load(g, path, max_size);
    Texture t = texture_from_memory(g, bytes, size, max_size);
    SDL_free(bytes);
    return t;
}
static void take_px(Model *m, int i) { m->tex_px[i] = last_px; m->tex_w[i] = last_w; m->tex_h[i] = last_h; last_px = NULL; }

static int node_index(const cgltf_data *d, const cgltf_node *n) { return n ? (int)(n - d->nodes) : -1; }

bool model_load(Gfx *g, Model *m, const char *path, int max_tex_size) {
    memset(m, 0, sizeof *m);
    cgltf_options opt = {0};
    cgltf_data *d = NULL;
    if (cgltf_parse_file(&opt, path, &d) != cgltf_result_success) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "model parse failed: %s", path); return false; }
    if (cgltf_load_buffers(&opt, d, path) != cgltf_result_success) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "model buffers failed: %s", path); cgltf_free(d); return false; }

    // Nodes
    if (d->nodes_count > MODEL_MAX_NODES) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: too many nodes (%zu)", path, d->nodes_count); cgltf_free(d); return false; }
    m->nnodes = (int)d->nodes_count;
    for (int i = 0; i < m->nnodes; i++) {
        const cgltf_node *n = &d->nodes[i];
        ModelNode *o = &m->nodes[i];
        snprintf(o->name, sizeof o->name, "%s", n->name ? n->name : "");
        o->parent = node_index(d, n->parent);
        o->t = v3(0, 0, 0); o->r = quat_identity(); o->s = v3(1, 1, 1);
        if (n->has_translation) o->t = v3(n->translation[0], n->translation[1], n->translation[2]);
        if (n->has_rotation) o->r = (Quat){n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]};
        if (n->has_scale) o->s = v3(n->scale[0], n->scale[1], n->scale[2]);
        if (n->has_matrix) {
            // Rare for rigs; decompose translation only and warn.
            o->t = v3(n->matrix[12], n->matrix[13], n->matrix[14]);
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s: node %s uses a matrix, rotation ignored", path, o->name);
        }
    }

    // Textures: one per material base colour image
    for (size_t i = 0; i < d->materials_count && m->ntextures < MODEL_MAX_TEX; i++) {
        const cgltf_material *mat = &d->materials[i];
        const cgltf_texture *tex = mat->has_pbr_metallic_roughness ? mat->pbr_metallic_roughness.base_color_texture.texture : NULL;
        if (tex && tex->image && tex->image->buffer_view) {
            const cgltf_buffer_view *bv = tex->image->buffer_view;
            m->textures[m->ntextures] = texture_from_memory(g, (const unsigned char *)bv->buffer->data + bv->offset, bv->size, max_tex_size);
            take_px(m, m->ntextures); m->ntextures++;
        } else if (tex && tex->image && tex->image->uri && strncmp(tex->image->uri, "data:", 5) != 0) {
            // External file next to the model
            char dir[512]; snprintf(dir, sizeof dir, "%s", path);
            char *slash = strrchr(dir, '/'); if (slash) slash[1] = 0; else dir[0] = 0;
            char ipath[1024]; snprintf(ipath, sizeof ipath, "%s%s", dir, tex->image->uri);
            m->textures[m->ntextures] = texture_from_file(g, ipath, max_tex_size);
            take_px(m, m->ntextures); m->ntextures++;
        } else {
            // Untextured material: a flat colour texture from the base colour factor.
            const float *c = mat->pbr_metallic_roughness.base_color_factor;
            unsigned char px[4] = { (unsigned char)(c[0] * 255), (unsigned char)(c[1] * 255), (unsigned char)(c[2] * 255), 255 };
            m->textures[m->ntextures++] = gfx_texture_create(g, px, 1, 1);
        }
    }
    if (m->ntextures == 0) m->textures[m->ntextures++] = g->white;

    // Skin (first one only)
    if (d->skins_count > 0) {
        const cgltf_skin *s = &d->skins[0];
        if (s->joints_count > MODEL_MAX_JOINTS) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: too many joints (%zu)", path, s->joints_count); cgltf_free(d); return false; }
        m->njoints = (int)s->joints_count;
        for (int j = 0; j < m->njoints; j++) {
            m->joints[j] = node_index(d, s->joints[j]);
            if (s->inverse_bind_matrices) cgltf_accessor_read_float(s->inverse_bind_matrices, j, m->inv_bind[j].m, 16);
            else m->inv_bind[j] = m4_identity();
        }
    }

    // Meshes: walk nodes so each mesh knows its node
    m->bmin = v3(1e9f, 1e9f, 1e9f); m->bmax = v3(-1e9f, -1e9f, -1e9f);
    for (int ni = 0; ni < m->nnodes; ni++) {
        const cgltf_node *n = &d->nodes[ni];
        if (!n->mesh) continue;
        for (size_t pi = 0; pi < n->mesh->primitives_count; pi++) {
            if (m->nmeshes >= MODEL_MAX_MESHES) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s: too many meshes, truncating", path); break; }
            const cgltf_primitive *p = &n->mesh->primitives[pi];
            if (p->type != cgltf_primitive_type_triangles || !p->indices) continue;
            const cgltf_accessor *pos = NULL, *nrm = NULL, *uv = NULL, *jnt = NULL, *wgt = NULL;
            for (size_t a = 0; a < p->attributes_count; a++) {
                const cgltf_attribute *at = &p->attributes[a];
                if (at->type == cgltf_attribute_type_position && at->index == 0) pos = at->data;
                else if (at->type == cgltf_attribute_type_normal && at->index == 0) nrm = at->data;
                else if (at->type == cgltf_attribute_type_texcoord && at->index == 0) uv = at->data;
                else if (at->type == cgltf_attribute_type_joints && at->index == 0) jnt = at->data;
                else if (at->type == cgltf_attribute_type_weights && at->index == 0) wgt = at->data;
            }
            if (!pos) continue;
            Uint32 nv = (Uint32)pos->count, ni_ = (Uint32)p->indices->count;
            if (nv > 65535) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s: mesh too large for 16-bit indices", path); continue; }
            Uint16 *idx = malloc(ni_ * sizeof *idx);
            for (Uint32 i = 0; i < ni_; i++) idx[i] = (Uint16)cgltf_accessor_read_index(p->indices, i);
            ModelMesh *mm = &m->meshes[m->nmeshes];
            mm->node = ni;
            mm->tex = p->material ? (int)(p->material - d->materials) : 0;
            if (mm->tex >= m->ntextures) mm->tex = 0;
            bool skinned = jnt && wgt && n->skin && m->njoints > 0;
            mm->skinned = skinned;
            if (skinned) {
                SkinVertex *v = calloc(nv, sizeof *v);
                for (Uint32 i = 0; i < nv; i++) {
                    cgltf_accessor_read_float(pos, i, v[i].pos, 3);
                    if (nrm) cgltf_accessor_read_float(nrm, i, v[i].normal, 3); else v[i].normal[1] = 1;
                    if (uv) cgltf_accessor_read_float(uv, i, v[i].uv, 2);
                    cgltf_uint j4[4] = {0}; cgltf_accessor_read_uint(jnt, i, j4, 4);
                    for (int k = 0; k < 4; k++) v[i].joints[k] = (Uint8)j4[k];
                    cgltf_accessor_read_float(wgt, i, v[i].weights, 4);
                }
                mm->gpu = gfx_skinned_mesh_create(g, v, nv, idx, ni_);
                free(v);
            } else {
                Vertex *v = calloc(nv, sizeof *v);
                for (Uint32 i = 0; i < nv; i++) {
                    cgltf_accessor_read_float(pos, i, v[i].pos, 3);
                    if (nrm) cgltf_accessor_read_float(nrm, i, v[i].normal, 3); else v[i].normal[1] = 1;
                    if (uv) cgltf_accessor_read_float(uv, i, v[i].uv, 2);
                    v[i].color[0] = v[i].color[1] = v[i].color[2] = v[i].color[3] = 1;
                }
                mm->gpu = gfx_mesh_create(g, v, nv, idx, ni_);
                free(v);
            }
            // bounds from accessor min/max (skinned meshes are authored in bind pose, good enough)
            if (pos->has_min && pos->has_max) {
                m->bmin = v3(fminf(m->bmin.x, pos->min[0]), fminf(m->bmin.y, pos->min[1]), fminf(m->bmin.z, pos->min[2]));
                m->bmax = v3(fmaxf(m->bmax.x, pos->max[0]), fmaxf(m->bmax.y, pos->max[1]), fmaxf(m->bmax.z, pos->max[2]));
            }
            free(idx);
            m->nmeshes++;
        }
    }

    // Animations
    m->nclips = (int)d->animations_count;
    m->clips = calloc((size_t)m->nclips, sizeof *m->clips);
    for (int ai = 0; ai < m->nclips; ai++) {
        const cgltf_animation *a = &d->animations[ai];
        AnimClip *c = &m->clips[ai];
        snprintf(c->name, sizeof c->name, "%s", a->name ? a->name : "");
        c->nchannels = 0;
        c->channels = calloc(a->channels_count, sizeof *c->channels);
        for (size_t ci = 0; ci < a->channels_count; ci++) {
            const cgltf_animation_channel *ch = &a->channels[ci];
            int ptype = ch->target_path == cgltf_animation_path_type_translation ? 0 :
                        ch->target_path == cgltf_animation_path_type_rotation ? 1 :
                        ch->target_path == cgltf_animation_path_type_scale ? 2 : -1;
            if (ptype < 0 || !ch->target_node) continue;
            AnimChannel *o = &c->channels[c->nchannels++];
            o->node = node_index(d, ch->target_node); o->path = ptype;
            o->nkeys = (int)ch->sampler->input->count;
            int comps = ptype == 1 ? 4 : 3;
            o->times = malloc((size_t)o->nkeys * sizeof(float));
            o->values = malloc((size_t)o->nkeys * comps * sizeof(float));
            for (int k = 0; k < o->nkeys; k++) {
                cgltf_accessor_read_float(ch->sampler->input, k, &o->times[k], 1);
                if (ch->sampler->interpolation == cgltf_interpolation_type_cubic_spline)
                    cgltf_accessor_read_float(ch->sampler->output, k * 3 + 1, &o->values[k * comps], comps);  // take the value, drop tangents
                else
                    cgltf_accessor_read_float(ch->sampler->output, k, &o->values[k * comps], comps);
            }
            if (o->nkeys > 0 && o->times[o->nkeys - 1] > c->duration) c->duration = o->times[o->nkeys - 1];
        }
    }
    cgltf_free(d);
    SDL_Log("model %s: %d nodes, %d meshes, %d joints, %d clips, bounds y %.2f..%.2f", path, m->nnodes, m->nmeshes, m->njoints, m->nclips, m->bmin.y, m->bmax.y);
    return true;
}

void model_destroy(Gfx *g, Model *m) {
    for (int i = 0; i < m->nmeshes; i++) gfx_mesh_destroy(g, &m->meshes[i].gpu);
    for (int i = 0; i < m->ntextures; i++) { if (m->textures[i].tex != g->white.tex) gfx_texture_destroy(g, &m->textures[i]); free(m->tex_px[i]); }
    for (int i = 0; i < m->nclips; i++) {
        for (int c = 0; c < m->clips[i].nchannels; c++) { free(m->clips[i].channels[c].times); free(m->clips[i].channels[c].values); }
        free(m->clips[i].channels);
    }
    free(m->clips);
    memset(m, 0, sizeof *m);
}

int model_find_clip(const Model *m, const char *name) {
    for (int i = 0; i < m->nclips; i++) if (!strcmp(m->clips[i].name, name)) return i;
    return -1;
}
int model_find_node(const Model *m, const char *name) {
    for (int i = 0; i < m->nnodes; i++) if (!strcmp(m->nodes[i].name, name)) return i;
    return -1;
}
void model_hide_node(Model *m, const char *name, bool hidden) {
    int i = model_find_node(m, name);
    if (i >= 0) m->nodes[i].hidden = hidden;
    else SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "model: no node named %s", name);
}

// ---------------------------------------------------------------- recolouring and parts

static int col_dist(const unsigned char *a, const unsigned char *b) { int dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2]; return dr * dr + dg * dg + db * db; }

int model_palette(const Model *m, ModelColor *out, int max) {
    // Gather distinct colours, merging near-identical ones (anti-aliased edges and downsampling
    // blends make thousands of near-duplicates around a couple of dozen real paint colours).
    enum { CAP = 512 };
    ModelColor tmp[CAP]; int n = 0;
    for (int t = 0; t < m->ntextures; t++) {
        const unsigned char *px = m->tex_px[t]; if (!px) continue;
        int total = m->tex_w[t] * m->tex_h[t];
        for (int i = 0; i < total; i++) {
            const unsigned char *p = px + i * 4;
            if (p[3] < 128) continue;
            int best = -1, bd = 1 << 30;
            for (int k = 0; k < n; k++) { int d = col_dist(tmp[k].rgb, p); if (d < bd) { bd = d; best = k; } }
            if (best >= 0 && bd <= 12 * 12) { tmp[best].count++; continue; }
            if (n >= CAP) continue;
            tmp[n].rgb[0] = p[0]; tmp[n].rgb[1] = p[1]; tmp[n].rgb[2] = p[2]; tmp[n].count = 1; n++;
        }
    }
    for (int i = 1; i < n; i++) { ModelColor c = tmp[i]; int j = i - 1; while (j >= 0 && tmp[j].count < c.count) { tmp[j + 1] = tmp[j]; j--; } tmp[j + 1] = c; }
    int keep = n < max ? n : max;
    memcpy(out, tmp, (size_t)keep * sizeof *out);
    return keep;
}

void model_recolor(Gfx *g, Model *m, const unsigned char (*from)[3], const unsigned char (*to)[3], int n) {
    // Every pixel belongs to its nearest `from` colour (within a tolerance); it moves by the same
    // offset as that colour, so shading and anti-aliased edges follow the recolour.
    for (int t = 0; t < m->ntextures; t++) {
        const unsigned char *px = m->tex_px[t]; if (!px) continue;
        int total = m->tex_w[t] * m->tex_h[t];
        unsigned char *out = malloc((size_t)total * 4); if (!out) continue;
        memcpy(out, px, (size_t)total * 4);
        for (int i = 0; i < total; i++) {
            unsigned char *p = out + i * 4;
            int best = -1, bd = 1 << 30;
            for (int k = 0; k < n; k++) { int d = col_dist(from[k], p); if (d < bd) { bd = d; best = k; } }
            if (best < 0 || bd > 40 * 40) continue;
            for (int c = 0; c < 3; c++) { int v = p[c] + (int)to[best][c] - (int)from[best][c]; p[c] = (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v); }
        }
        if (m->textures[t].tex != g->white.tex) gfx_texture_destroy(g, &m->textures[t]);
        m->textures[t] = gfx_texture_create(g, out, m->tex_w[t], m->tex_h[t]);
        free(out);
    }
}

int model_part_names(const Model *m, const char **out, int max) {
    int n = 0;
    for (int i = 0; i < m->nmeshes && n < max; i++) {
        const char *nm = m->nodes[m->meshes[i].node].name;
        bool dup = false; for (int k = 0; k < n; k++) if (!strcmp(out[k], nm)) dup = true;
        if (!dup) out[n++] = nm;
    }
    return n;
}

// ---------------------------------------------------------------- playback

static void begin_fade(AnimPlayer *p, float fade_dur) {
    if (p->clip >= 0 && fade_dur > 0) {
        p->prev = p->clip; p->prev_time = p->time; p->prev_rate = p->time < p->split ? p->rate1 : p->rate2;
        p->fade = 0; p->fade_dur = fade_dur;
    } else { p->prev = -1; p->fade = 1; p->fade_dur = 0; }
}

void anim_play(AnimPlayer *p, const Model *m, int clip, float rate, bool loop, bool hold, float fade_dur) {
    (void)m;
    if (clip < 0) return;
    begin_fade(p, fade_dur);
    p->clip = clip; p->time = 0; p->rate1 = p->rate2 = rate; p->split = 1e9f; p->loop = loop; p->hold = hold;
}

void anim_play_fitted(AnimPlayer *p, const Model *m, int clip, float contact, float lead, float tail, float fade_dur) {
    if (clip < 0) return;
    begin_fade(p, fade_dur);
    float len = m->clips[clip].duration;
    contact = clampf(contact, 0.0f, len);
    p->clip = clip; p->time = 0; p->loop = false; p->hold = true;
    p->split = contact;
    p->rate1 = lead > 0.001f ? contact / lead : 1e6f;
    p->rate2 = tail > 0.001f ? (len - contact) / tail : 1.0f;
}

void anim_update(AnimPlayer *p, const Model *m, float dt) {
    if (p->clip < 0 || p->clip >= m->nclips) return;
    float len = m->clips[p->clip].duration;
    float rate = p->time < p->split ? p->rate1 : p->rate2;
    p->time += dt * rate;
    if (p->time >= p->split && rate == p->rate1 && p->rate1 > 1e5f) p->time = p->split;  // snapped straight to contact
    if (len > 0) {
        if (p->loop) p->time = fmodf(p->time, len);
        else if (p->time > len) p->time = len;
    }
    if (p->prev >= 0) {
        p->prev_time += dt * p->prev_rate;
        if (p->prev_time > m->clips[p->prev].duration) p->prev_time = m->clips[p->prev].duration;
        p->fade += dt / p->fade_dur;
        if (p->fade >= 1) { p->fade = 1; p->prev = -1; }
    }
}

bool anim_finished(const AnimPlayer *p, const Model *m) {
    if (p->clip < 0 || p->loop) return false;
    return p->time >= m->clips[p->clip].duration - 1e-4f;
}

// ---------------------------------------------------------------- posing

typedef struct LocalPose { Vec3 t[MODEL_MAX_NODES]; Quat r[MODEL_MAX_NODES]; Vec3 s[MODEL_MAX_NODES]; } LocalPose;

static void sample_clip(const Model *m, int clip, float time, LocalPose *lp) {
    for (int i = 0; i < m->nnodes; i++) { lp->t[i] = m->nodes[i].t; lp->r[i] = m->nodes[i].r; lp->s[i] = m->nodes[i].s; }
    if (clip < 0 || clip >= m->nclips) return;
    const AnimClip *c = &m->clips[clip];
    for (int ci = 0; ci < c->nchannels; ci++) {
        const AnimChannel *ch = &c->channels[ci];
        if (ch->nkeys == 0) continue;
        int comps = ch->path == 1 ? 4 : 3;
        int k = 0;
        if (time <= ch->times[0]) k = 0;
        else if (time >= ch->times[ch->nkeys - 1]) k = ch->nkeys - 1;
        else { int lo = 0, hi = ch->nkeys - 1; while (hi - lo > 1) { int mid = (lo + hi) / 2; if (ch->times[mid] <= time) lo = mid; else hi = mid; } k = lo; }
        int k2 = k + 1 < ch->nkeys ? k + 1 : k;
        float t = 0;
        if (k2 != k) { float span = ch->times[k2] - ch->times[k]; t = span > 1e-6f ? clampf((time - ch->times[k]) / span, 0, 1) : 0; }
        const float *a = &ch->values[k * comps], *b = &ch->values[k2 * comps];
        if (ch->path == 0) lp->t[ch->node] = v3(lerpf(a[0], b[0], t), lerpf(a[1], b[1], t), lerpf(a[2], b[2], t));
        else if (ch->path == 2) lp->s[ch->node] = v3(lerpf(a[0], b[0], t), lerpf(a[1], b[1], t), lerpf(a[2], b[2], t));
        else lp->r[ch->node] = quat_slerp((Quat){a[0], a[1], a[2], a[3]}, (Quat){b[0], b[1], b[2], b[3]}, t);
    }
}

void model_pose(const Model *m, const AnimPlayer *p, ModelPose *out) {
    static LocalPose cur, prev;   // large; keep off the stack
    sample_clip(m, p->clip, p->time, &cur);
    if (p->prev >= 0 && p->fade < 1) {
        sample_clip(m, p->prev, p->prev_time, &prev);
        float k = smoothstep(p->fade);
        for (int i = 0; i < m->nnodes; i++) {
            cur.t[i] = v3_lerp(prev.t[i], cur.t[i], k);
            cur.s[i] = v3_lerp(prev.s[i], cur.s[i], k);
            cur.r[i] = quat_slerp(prev.r[i], cur.r[i], k);
        }
    }
    // Parents come before children in glTF exports from Blender, but do not rely on it: resolve lazily.
    bool done[MODEL_MAX_NODES] = {0};
    for (int i = 0; i < m->nnodes; i++) {
        // walk up until a resolved ancestor, then resolve down the chain
        int chain[MODEL_MAX_NODES]; int cn = 0; int j = i;
        while (j >= 0 && !done[j]) { chain[cn++] = j; j = m->nodes[j].parent; }
        for (int c = cn - 1; c >= 0; c--) {
            int k = chain[c];
            Mat4 local = m4_from_trs(cur.t[k], cur.r[k], cur.s[k]);
            int par = m->nodes[k].parent;
            out->global[k] = par >= 0 ? m4_mul(out->global[par], local) : local;
            done[k] = true;
        }
    }
    for (int j = 0; j < m->njoints; j++) out->joints[j] = m4_mul(out->global[m->joints[j]], m->inv_bind[j]);
}

void model_draw(Gfx *g, const Model *m, const ModelPose *pose, Mat4 world, Vec4 tint) {
    for (int i = 0; i < m->nmeshes; i++) {
        const ModelMesh *mm = &m->meshes[i];
        // A hidden flag on the mesh node or any ancestor hides the mesh.
        bool hidden = false;
        for (int n = mm->node; n >= 0; n = m->nodes[n].parent) if (m->nodes[n].hidden) { hidden = true; break; }
        if (hidden) continue;
        const Texture *t = &m->textures[mm->tex];
        if (mm->skinned) gfx_draw_skinned(g, &mm->gpu, t, world, tint, pose->joints, m->njoints);
        else gfx_draw(g, &mm->gpu, t, m4_mul(world, pose->global[mm->node]), tint, v4(1, 1, 0, 0));
    }
}
