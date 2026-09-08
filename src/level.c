// Level file parser, hot reload and collision. See level.h and assets/levels/README.md.
#include "level.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <stdlib.h>

static const char *k_tex_names[TEX_COUNT] = { "stone", "tile", "wood", "metal", "flesh", "plaster" };

int level_tex_from_name(const char *name) {
    for (int i = 0; i < TEX_COUNT; i++)
        if (strcmp(name, k_tex_names[i]) == 0) return i;
    return -1;
}

// Split a mutable line into whitespace-separated tokens; returns token count.
static int tokenize(char *line, char *tok[], int max_tok) {
    int n = 0;
    char *save = NULL;
    for (char *t = SDL_strtok_r(line, " \t", &save); t && n < max_tok; t = SDL_strtok_r(NULL, " \t", &save))
        tok[n++] = t;
    return n;
}

static bool parse_floats(char **tok, int start, int count, float *out) {
    for (int i = 0; i < count; i++) {
        char *end = NULL;
        out[i] = SDL_strtod(tok[start + i], &end);
        if (end == tok[start + i]) return false;
    }
    return true;
}

// Parse the whole file into `out`. Returns false and logs on any fatal (I/O) error;
// bad individual lines are warned about and skipped, not fatal.
static bool parse_level(Level *out, const char *path) {
    memset(out, 0, sizeof *out);
    out->fog_color = v3(0, 0, 0);
    out->light_color = v3(1, 1, 1);
    out->ambient = 0.2f;

    size_t size = 0;
    void *data = SDL_LoadFile(path, &size);
    if (!data) {
        SDL_Log("level_load: could not read '%s': %s", path, SDL_GetError());
        return false;
    }

    // Work on a NUL-terminated mutable copy so we can tokenize in place.
    char *buf = (char *)malloc(size + 1);
    if (!buf) { SDL_free(data); SDL_Log("level_load: out of memory"); return false; }
    memcpy(buf, data, size);
    buf[size] = '\0';
    SDL_free(data);

    int line_no = 0;
    char *save_line = NULL;
    for (char *line = SDL_strtok_r(buf, "\n", &save_line); line; line = SDL_strtok_r(NULL, "\n", &save_line)) {
        line_no++;
        // Strip trailing '\r' (CRLF files) and comments.
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t')) line[--len] = '\0';

        char *tok[32];
        int n = tokenize(line, tok, 32);
        if (n == 0) continue; // blank / comment-only line

        const char *cmd = tok[0];

        if (strcmp(cmd, "fog") == 0) {
            float f[5];
            if (n != 6 || !parse_floats(tok, 1, 5, f)) { SDL_Log("level_load:%d: bad fog line", line_no); continue; }
            out->fog_color = v3(f[0], f[1], f[2]);
            out->fog_near = f[3];
            out->fog_far = f[4];

        } else if (strcmp(cmd, "light") == 0) {
            float f[7];
            if (n != 8 || !parse_floats(tok, 1, 7, f)) { SDL_Log("level_load:%d: bad light line", line_no); continue; }
            out->light_dir = v3(f[0], f[1], f[2]);
            out->ambient = f[3];
            out->light_color = v3(f[4], f[5], f[6]);

        } else if (strcmp(cmd, "spawn") == 0) {
            float f[4];
            if (n != 5 || !parse_floats(tok, 1, 4, f)) { SDL_Log("level_load:%d: bad spawn line", line_no); continue; }
            out->spawn = v3(f[0], f[1], f[2]);
            out->spawn_yaw = f[3] * DEG2RAD;

        } else if (strcmp(cmd, "boss") == 0) {
            float f[4];
            if (n != 5 || !parse_floats(tok, 1, 4, f)) { SDL_Log("level_load:%d: bad boss line", line_no); continue; }
            out->boss_spawn = v3(f[0], f[1], f[2]);
            out->boss_yaw = f[3] * DEG2RAD;

        } else if (strcmp(cmd, "arena") == 0) {
            float f[6];
            if (n != 7 || !parse_floats(tok, 1, 6, f)) { SDL_Log("level_load:%d: bad arena line", line_no); continue; }
            out->arena_min = v3(f[0], f[1], f[2]);
            out->arena_max = v3(f[3], f[4], f[5]);

        } else if (strcmp(cmd, "block") == 0) {
            // block x y z sx sy sz tex r g b tile [solid|pass]
            if (n != 13 && n != 14) { SDL_Log("level_load:%d: bad block line", line_no); continue; }
            float pos[3], size3[3], tint[3], tile;
            if (!parse_floats(tok, 1, 3, pos) || !parse_floats(tok, 4, 3, size3)) {
                SDL_Log("level_load:%d: bad block line", line_no); continue;
            }
            int tex = level_tex_from_name(tok[7]);
            if (tex < 0) { SDL_Log("level_load:%d: unknown texture '%s'", line_no, tok[7]); continue; }
            if (!parse_floats(tok, 8, 3, tint) || !parse_floats(tok, 11, 1, &tile)) {
                SDL_Log("level_load:%d: bad block line", line_no); continue;
            }
            bool solid = true;
            if (n == 14) {
                if (strcmp(tok[13], "pass") == 0) solid = false;
                else if (strcmp(tok[13], "solid") == 0) solid = true;
                else { SDL_Log("level_load:%d: bad block solidity token '%s'", line_no, tok[13]); continue; }
            }
            if (out->nblocks >= LEVEL_MAX_BLOCKS) { SDL_Log("level_load:%d: too many blocks", line_no); continue; }
            Block *b = &out->blocks[out->nblocks++];
            b->center = v3(pos[0], pos[1], pos[2]);
            b->size = v3(size3[0], size3[1], size3[2]);
            b->tex = tex;
            b->tint = v4(tint[0], tint[1], tint[2], 1.0f);
            b->uv_tile = tile;
            b->solid = solid;

        } else if (strcmp(cmd, "cam") == 0) {
            // cam name minx miny minz maxx maxy maxz eyex eyey eyez tx ty tz fov
            if (n != 15) { SDL_Log("level_load:%d: bad cam line", line_no); continue; }
            float f[13];
            if (!parse_floats(tok, 2, 13, f)) { SDL_Log("level_load:%d: bad cam line", line_no); continue; }
            if (out->ncams >= LEVEL_MAX_CAMS) { SDL_Log("level_load:%d: too many cams", line_no); continue; }
            CamVolume *c = &out->cams[out->ncams++];
            memset(c, 0, sizeof *c);
            SDL_strlcpy(c->name, tok[1], sizeof c->name);
            c->vmin = v3(f[0], f[1], f[2]);
            c->vmax = v3(f[3], f[4], f[5]);
            c->eye = v3(f[6], f[7], f[8]);
            c->target = v3(f[9], f[10], f[11]);
            c->fov = f[12];

        } else if (strcmp(cmd, "trigger") == 0) {
            // trigger name minx miny minz maxx maxy maxz [once]
            if (n != 8 && n != 9) { SDL_Log("level_load:%d: bad trigger line", line_no); continue; }
            float f[6];
            if (!parse_floats(tok, 2, 6, f)) { SDL_Log("level_load:%d: bad trigger line", line_no); continue; }
            bool once = false;
            if (n == 9) {
                if (strcmp(tok[8], "once") != 0) { SDL_Log("level_load:%d: bad trigger token '%s'", line_no, tok[8]); continue; }
                once = true;
            }
            if (out->ntriggers >= LEVEL_MAX_TRIGGERS) { SDL_Log("level_load:%d: too many triggers", line_no); continue; }
            Trigger *t = &out->triggers[out->ntriggers++];
            memset(t, 0, sizeof *t);
            SDL_strlcpy(t->name, tok[1], sizeof t->name);
            t->vmin = v3(f[0], f[1], f[2]);
            t->vmax = v3(f[3], f[4], f[5]);
            t->once = once;
            t->fired = false;

        } else {
            SDL_Log("level_load:%d: unknown command '%s'", line_no, cmd);
        }
    }

    free(buf);
    return true;
}

bool level_load(Level *lv, const char *path) {
    Level tmp;
    if (!parse_level(&tmp, path)) return false;

    SDL_PathInfo info;
    long long mtime = 0;
    if (SDL_GetPathInfo(path, &info)) mtime = (long long)info.modify_time;

    SDL_strlcpy(tmp.path, path, sizeof tmp.path);
    tmp.mtime = mtime;

    *lv = tmp;
    return true;
}

bool level_reload_if_changed(Level *lv) {
    if (lv->path[0] == '\0') return false;
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(lv->path, &info)) return false;
    long long mtime = (long long)info.modify_time;
    if (mtime <= lv->mtime) return false;
    char path[512];
    SDL_strlcpy(path, lv->path, sizeof path);
    return level_load(lv, path);
}

// True if the Y range [y0, y1] overlaps the block's Y extent.
static bool y_overlaps(const Block *b, float y0, float y1) {
    float bmin = b->center.y - b->size.y * 0.5f;
    float bmax = b->center.y + b->size.y * 0.5f;
    return y0 < bmax && y1 > bmin;
}

// Largest single sub-step taken while sliding along an axis. Kept well below any
// reasonable wall thickness so a big `delta` (e.g. a teleport, or a test) can't tunnel
// through solid geometry between checks.
#define LEVEL_MOVE_MAX_STEP 0.05f

// Move `primary` (the x or z coordinate, per `axis`: 0 = x, 1 = z) by `d`, substepping
// and testing the moving circle against every solid block after each sub-step so we
// stop flush against the first one hit rather than tunnelling through it. `secondary`
// is the other horizontal coordinate, held fixed for this axis pass.
static float move_axis(const Level *lv, float primary, float secondary, float y0, float y1,
                        float radius, float d, int axis) {
    if (d == 0.0f) return primary;

    int steps = (int)ceilf(fabsf(d) / LEVEL_MOVE_MAX_STEP);
    if (steps < 1) steps = 1;
    float step = d / (float)steps;

    for (int s = 0; s < steps; s++) {
        float next = primary + step;
        bool hit = false;

        for (int i = 0; i < lv->nblocks; i++) {
            const Block *b = &lv->blocks[i];
            if (!b->solid || !y_overlaps(b, y0, y1)) continue;

            float pmin, pmax, smin, smax;
            if (axis == 0) {
                pmin = b->center.x - b->size.x * 0.5f; pmax = b->center.x + b->size.x * 0.5f;
                smin = b->center.z - b->size.z * 0.5f; smax = b->center.z + b->size.z * 0.5f;
            } else {
                pmin = b->center.z - b->size.z * 0.5f; pmax = b->center.z + b->size.z * 0.5f;
                smin = b->center.x - b->size.x * 0.5f; smax = b->center.x + b->size.x * 0.5f;
            }

            // How far off the block's secondary-axis span the circle's centre sits;
            // beyond `radius` the circle can't touch this block at all this pass.
            float cs = clampf(secondary, smin, smax);
            float ds = secondary - cs;
            if (fabsf(ds) >= radius) continue;

            // Chord half-length of the circle at this secondary offset: how far into
            // (or short of) the block's primary span still counts as touching it.
            float reach = sqrtf(fmaxf(radius * radius - ds * ds, 0.0f));
            float touch_lo = pmin - reach;
            float touch_hi = pmax + reach;
            if (next > touch_lo && next < touch_hi) {
                next = (step > 0.0f) ? fminf(next, touch_lo) : fmaxf(next, touch_hi);
                hit = true;
            }
        }

        primary = next;
        if (hit) break; // stopped by a wall; discard the rest of this axis's motion
    }
    return primary;
}

Vec3 level_move(const Level *lv, Vec3 pos, float radius, float height, Vec3 delta) {
    float x = pos.x, z = pos.z;
    float y0 = pos.y, y1 = pos.y + height;

    x = move_axis(lv, x, z, y0, y1, radius, delta.x, 0);
    z = move_axis(lv, z, x, y0, y1, radius, delta.z, 1);

    return v3(x, pos.y + delta.y, z);
}

static bool point_in_box(Vec3 p, Vec3 vmin, Vec3 vmax) {
    return p.x >= vmin.x && p.x <= vmax.x &&
           p.y >= vmin.y && p.y <= vmax.y &&
           p.z >= vmin.z && p.z <= vmax.z;
}

const CamVolume *level_camera_at(const Level *lv, Vec3 p) {
    for (int i = 0; i < lv->ncams; i++)
        if (point_in_box(p, lv->cams[i].vmin, lv->cams[i].vmax)) return &lv->cams[i];
    return NULL;
}

Trigger *level_trigger_at(Level *lv, Vec3 p) {
    for (int i = 0; i < lv->ntriggers; i++) {
        Trigger *t = &lv->triggers[i];
        if (t->fired) continue;
        if (point_in_box(p, t->vmin, t->vmax)) {
            if (t->once) t->fired = true;
            return t;
        }
    }
    return NULL;
}

void level_reset_triggers(Level *lv) {
    for (int i = 0; i < lv->ntriggers; i++) lv->triggers[i].fired = false;
}

float level_ray_solid(const Level *lv, Vec3 a, Vec3 b, float margin) {
    float best = 1.0f;
    Vec3 d = v3_sub(b, a);
    for (int i = 0; i < lv->nblocks; i++) {
        const Block *bl = &lv->blocks[i];
        if (!bl->solid) continue;
        Vec3 h = v3_scale(bl->size, 0.5f);
        Vec3 mn = v3(bl->center.x - h.x - margin, bl->center.y - h.y - margin, bl->center.z - h.z - margin);
        Vec3 mx = v3(bl->center.x + h.x + margin, bl->center.y + h.y + margin, bl->center.z + h.z + margin);
        float t0 = 0.0f, t1 = 1.0f;
        const float *av = &a.x, *dv = &d.x, *mnv = &mn.x, *mxv = &mx.x;
        bool miss = false;
        for (int k = 0; k < 3; k++) {
            if (fabsf(dv[k]) < 1e-6f) { if (av[k] < mnv[k] || av[k] > mxv[k]) { miss = true; break; } continue; }
            float ta = (mnv[k] - av[k]) / dv[k], tb = (mxv[k] - av[k]) / dv[k];
            if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; }
            if (ta > t0) t0 = ta;
            if (tb < t1) t1 = tb;
            if (t0 > t1) { miss = true; break; }
        }
        if (!miss && t0 < best) best = t0 > 0 ? t0 : 0;
    }
    return best;
}
