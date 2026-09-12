// Level file parser, hot reload and collision. See level.h and assets/levels/README.md.
#include "level.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "part.h"

static const char *k_tex_names[TEX_COUNT] = { "stone", "tile", "wood", "metal", "flesh", "plaster", "flat" };

// ---------------------------------------------------------------- assets/textures/NAME.png|jpg
// One texture per image file, no level line: dropping a photoscan in the folder is the whole
// workflow. Scanned lazily (the first name lookup) because the level loader runs before the
// renderer, and only once, so ids stay put while a level is edited and saved.
static char k_user_name[LEVEL_MAX_USER_TEX][32];
static char k_user_file[LEVEL_MAX_USER_TEX][128];
static int  k_nuser = -1;   // -1 = not scanned yet

static int cmp_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void scan_user_textures(void) {
    if (k_nuser >= 0) return;
    k_nuser = 0;
    char dir[640]; snprintf(dir, sizeof dir, "%s/textures", HOLLOW_ASSET_DIR);
    int count = 0;
    char **files = SDL_GlobDirectory(dir, "*", 0, &count);
    if (!files) return;   // no textures folder: nothing to scan, every name stays procedural
    SDL_qsort(files, (size_t)count, sizeof *files, cmp_name);   // ids must not depend on the filesystem's order
    for (int i = 0; i < count; i++) {
        const char *dot = strrchr(files[i], '.');
        if (!dot || (SDL_strcasecmp(dot, ".png") != 0 && SDL_strcasecmp(dot, ".jpg") != 0 && SDL_strcasecmp(dot, ".jpeg") != 0)) continue;
        if (k_nuser >= LEVEL_MAX_USER_TEX) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "assets/textures: over %d images, ignoring %s", LEVEL_MAX_USER_TEX, files[i]); continue; }
        snprintf(k_user_name[k_nuser], sizeof k_user_name[0], "%.*s", (int)(dot - files[i]), files[i]);
        snprintf(k_user_file[k_nuser], sizeof k_user_file[0], "textures/%s", files[i]);
        k_nuser++;
    }
    SDL_free(files);
}

int         level_user_tex_count(void)     { scan_user_textures(); return k_nuser; }
const char *level_user_tex_name(int i)     { scan_user_textures(); return i >= 0 && i < k_nuser ? k_user_name[i] : ""; }
const char *level_user_tex_file(int i)     { scan_user_textures(); return i >= 0 && i < k_nuser ? k_user_file[i] : ""; }

int level_tex_from_name(const char *name) {
    for (int i = 0; i < TEX_COUNT; i++)
        if (strcmp(name, k_tex_names[i]) == 0) return i;
    scan_user_textures();
    for (int i = 0; i < k_nuser; i++)
        if (strcmp(name, k_user_name[i]) == 0) return TEX_COUNT + i;
    return -1;
}

const char *level_tex_name(int id) {
    if (id >= 0 && id < TEX_COUNT) return k_tex_names[id];
    scan_user_textures();
    if (id >= TEX_COUNT && id - TEX_COUNT < k_nuser) return k_user_name[id - TEX_COUNT];
    return k_tex_names[TEX_FLAT];
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

// A .part can carry its own collider (see part.h), which every prop placed from it inherits. The
// file is read once per load and remembered here: a level with a hundred palms in it would
// otherwise open the same file a hundred times.
#define PART_COLLIDE_CACHE 32
typedef struct PartCollide { char file[128]; float r, h; bool deck; } PartCollide;
static bool part_collider(PartCollide *cache, int *n, const char *file, float *r, float *h, bool *deck) {
    size_t len = strlen(file);
    if (len < 5 || strcmp(file + len - 5, ".part") != 0) return false;
    for (int i = 0; i < *n; i++)
        if (!strcmp(cache[i].file, file)) { *r = cache[i].r; *h = cache[i].h; *deck = cache[i].deck; return *r > 0; }
    PartDoc d;
    char path[1024]; snprintf(path, sizeof path, "%s/%s", HOLLOW_ASSET_DIR, file);
    if (!part_load(&d, path)) return false;
    if (*n < PART_COLLIDE_CACHE) {
        PartCollide *c = &cache[(*n)++];
        SDL_strlcpy(c->file, file, sizeof c->file);
        c->r = d.collide; c->h = d.collide_h; c->deck = d.collide_deck;
    }
    *r = d.collide; *h = d.collide_h; *deck = d.collide_deck;
    return *r > 0;
}

// Deepest an "look include" is allowed to nest, so a file that includes itself (directly or
// through a cycle) can't recurse forever.
#define LOOK_INCLUDE_MAX_DEPTH 4

// Parse one look line into `lk`. Returns false if `cmd` is not a look command at all (the caller
// then tries the placement commands), and logs and returns true for a look line it could not read.
// `where` names the file `tok`/`line_no` came from, for messages raised while walking a `look
// include`d file; `depth` is how many `look include`s deep we already are.
static bool parse_look_line(Look *lk, char **tok, int n, int line_no, const char *where, int depth) {
    (void)where;   // only the `look include` branch below needs it, to name the file it recurses into
    const char *cmd = tok[0];
    float f[12];

    if (strcmp(cmd, "sun") == 0) {
        if (n != 8 || !parse_floats(tok, 1, 7, f)) { SDL_Log("level_load:%d: bad sun line", line_no); return true; }
        lk->sun_dir = v3(f[0], f[1], f[2]); lk->sun_intensity = f[3]; lk->sun_color = v3(f[4], f[5], f[6]);
    } else if (strcmp(cmd, "ambient") == 0) {
        if (n != 7 || !parse_floats(tok, 1, 6, f)) { SDL_Log("level_load:%d: bad ambient line", line_no); return true; }
        lk->sky_ambient = v3(f[0], f[1], f[2]); lk->ground_ambient = v3(f[3], f[4], f[5]);
    } else if (strcmp(cmd, "fogv") == 0) {
        if (n != 9 || !parse_floats(tok, 1, 8, f)) { SDL_Log("level_load:%d: bad fogv line", line_no); return true; }
        lk->fog_color = v3(f[0], f[1], f[2]); lk->fog_density = f[3]; lk->fog_base = f[4];
        lk->fog_falloff = f[5]; lk->fog_scatter = f[6]; lk->fog_start = f[7];
    } else if (strcmp(cmd, "sky") == 0) {
        if (n != 13 || !parse_floats(tok, 1, 12, f)) { SDL_Log("level_load:%d: bad sky line", line_no); return true; }
        lk->sky_zenith = v3(f[0], f[1], f[2]); lk->sky_horizon = v3(f[3], f[4], f[5]); lk->sky_ground = v3(f[6], f[7], f[8]);
        lk->sun_glow = f[9]; lk->stars = f[10]; lk->sky_fog_blend = f[11];
    } else if (strcmp(cmd, "toon") == 0) {
        if (n != 4 || !parse_floats(tok, 1, 3, f)) { SDL_Log("level_load:%d: bad toon line", line_no); return true; }
        lk->toon_softness = f[0]; lk->shadow_floor = f[1]; lk->rim_power = f[2];
    } else if (strcmp(cmd, "grade") == 0) {
        if (n != 6 || !parse_floats(tok, 1, 5, f)) { SDL_Log("level_load:%d: bad grade line", line_no); return true; }
        lk->exposure = f[0]; lk->saturation = f[1]; lk->contrast = f[2]; lk->bloom = f[3]; lk->bloom_threshold = f[4];
    } else if (strcmp(cmd, "lift") == 0 || strcmp(cmd, "gain") == 0) {
        if (n != 4 || !parse_floats(tok, 1, 3, f)) { SDL_Log("level_load:%d: bad %s line", line_no, cmd); return true; }
        if (cmd[0] == 'l') lk->lift = v3(f[0], f[1], f[2]); else lk->gain = v3(f[0], f[1], f[2]);
    } else if (strcmp(cmd, "shadow") == 0) {
        if (n < 2 || !parse_floats(tok, 1, 1, f)) { SDL_Log("level_load:%d: bad shadow line", line_no); return true; }
        lk->shadow = f[0];
    } else if (strcmp(cmd, "style") == 0) {
        // style SNAP OUTLINE LEVELS PIXEL
        float s[4] = { 0, 0, 0, 1 };
        if (n < 5 || !parse_floats(tok, 1, 4, s)) { SDL_Log("level_load:%d: bad style line", line_no); return true; }
        lk->style_snap = s[0]; lk->style_outline = s[1]; lk->style_levels = s[2]; lk->style_pixel = s[3];
    } else if (strcmp(cmd, "pixel") == 0) {
        // pixel SCALE [LEVELS] [OUTLINE] [PALETTE] [INNER]   (trailing fields keep their default; "pixel 0" turns the pass off)
        float p[5] = { 3, 8, 1, 0, 0.6f };
        int given = n - 1 > 5 ? 5 : n - 1;
        if (given < 1 || !parse_floats(tok, 1, given, p)) { SDL_Log("level_load:%d: bad pixel line", line_no); return true; }
        lk->pixel_scale = p[0]; lk->pixel_levels = p[1]; lk->pixel_outline = p[2]; lk->pixel_palette = p[3]; lk->pixel_inner = p[4];
    } else if (strcmp(cmd, "daytime") == 0) {
        // daytime HOUR   (0..24; the sun, sky and fog colour follow the clock)
        if (n != 2 || !parse_floats(tok, 1, 1, f)) { SDL_Log("level_load:%d: bad daytime line", line_no); return true; }
        lk->daytime = f[0];
    } else if (strcmp(cmd, "look") == 0) {
        if (n < 2) { SDL_Log("level_load:%d: bad look line", line_no); return true; }
        const char *sub = tok[1];
        if (strcmp(sub, "far") == 0) {
            // look far METRES   (camera far plane in metres; default 80. A big open level needs ~600)
            if (n != 3 || !parse_floats(tok, 2, 1, f) || f[0] < 1) { SDL_Log("level_load:%d: bad look line (want 'look far METRES')", line_no); return true; }
            lk->view_far = f[0];
        } else if (strcmp(sub, "res") == 0) {
            // look res SCALE [nearest|linear]   (SCALE 0.25..1; the word defaults to linear)
            if (n < 3 || !parse_floats(tok, 2, 1, f) || f[0] <= 0) { SDL_Log("level_load:%d: bad look res line", line_no); return true; }
            lk->render_scale = f[0];
            lk->render_nearest = 0;
            if (n >= 4) {
                if (strcmp(tok[3], "nearest") == 0) lk->render_nearest = 1;
                else if (strcmp(tok[3], "linear") == 0) lk->render_nearest = 0;
                else { SDL_Log("level_load:%d: bad look res filter '%s'", line_no, tok[3]); return true; }
            }
        } else if (strcmp(sub, "texcap") == 0) {
            if (n != 3 || !parse_floats(tok, 2, 1, f)) { SDL_Log("level_load:%d: bad look texcap line", line_no); return true; }
            lk->tex_cap = f[0];
        } else if (strcmp(sub, "flat") == 0) {
            if (n != 3 || !parse_floats(tok, 2, 1, f)) { SDL_Log("level_load:%d: bad look flat line", line_no); return true; }
            lk->flat = f[0];
        } else if (strcmp(sub, "grain") == 0) {
            if (n != 3 || !parse_floats(tok, 2, 1, f)) { SDL_Log("level_load:%d: bad look grain line", line_no); return true; }
            lk->grain = f[0];
        } else if (strcmp(sub, "vignette") == 0) {
            if (n != 3 || !parse_floats(tok, 2, 1, f)) { SDL_Log("level_load:%d: bad look vignette line", line_no); return true; }
            lk->vignette = f[0];
        } else if (strcmp(sub, "chroma") == 0) {
            if (n != 3 || !parse_floats(tok, 2, 1, f)) { SDL_Log("level_load:%d: bad look chroma line", line_no); return true; }
            lk->chroma = f[0];
        } else if (strcmp(sub, "dither") == 0) {
            if (n != 3 || !parse_floats(tok, 2, 1, f)) { SDL_Log("level_load:%d: bad look dither line", line_no); return true; }
            lk->dither = f[0];
        } else if (strcmp(sub, "ink") == 0) {
            // look ink WIDTH [WOBBLE] [LUMA]   (trailing values keep their defaults 1 0 0)
            float v[3] = { 1, 0, 0 };
            int given = n - 2 > 3 ? 3 : n - 2;
            if (given < 1 || !parse_floats(tok, 2, given, v)) { SDL_Log("level_load:%d: bad look ink line", line_no); return true; }
            lk->ink_width = v[0]; lk->ink_wobble = v[1]; lk->ink_luma = v[2];
        } else if (strcmp(sub, "hatch") == 0) {
            if (n != 3 || !parse_floats(tok, 2, 1, f)) { SDL_Log("level_load:%d: bad look hatch line", line_no); return true; }
            lk->hatch = f[0];
        } else if (strcmp(sub, "paper") == 0) {
            if (n != 3 || !parse_floats(tok, 2, 1, f)) { SDL_Log("level_load:%d: bad look paper line", line_no); return true; }
            lk->paper = f[0];
        } else if (strcmp(sub, "include") == 0) {
            // look include FILE   (or FILE.txt): pulls in assets/looks/FILE.txt as if its lines were
            // written here. Guards its own depth so a cyclic include can't recurse forever.
            if (n != 3) { SDL_Log("level_load:%d: bad look include line", line_no); return true; }
            if (depth >= LOOK_INCLUDE_MAX_DEPTH) { SDL_Log("level_load:%d: look include '%s' nested too deep, ignoring", line_no, tok[2]); return true; }

            char name[96];
            snprintf(name, sizeof name, "%s%s", tok[2], strchr(tok[2], '.') ? "" : ".txt");
            char inc_path[768]; snprintf(inc_path, sizeof inc_path, "%s/looks/%s", HOLLOW_ASSET_DIR, name);
            size_t inc_size = 0;
            void *inc_data = SDL_LoadFile(inc_path, &inc_size);
            if (!inc_data) { SDL_Log("level_load:%d: look include: could not read '%s': %s", line_no, inc_path, SDL_GetError()); return true; }
            char *inc_buf = (char *)malloc(inc_size + 1);
            if (!inc_buf) { SDL_free(inc_data); SDL_Log("level_load:%d: look include: out of memory", line_no); return true; }
            memcpy(inc_buf, inc_data, inc_size);
            inc_buf[inc_size] = '\0';
            SDL_free(inc_data);

            int inc_line_no = 0;
            char *inc_save = NULL;
            for (char *inc_line = SDL_strtok_r(inc_buf, "\n", &inc_save); inc_line; inc_line = SDL_strtok_r(NULL, "\n", &inc_save)) {
                inc_line_no++;
                char *hash = strchr(inc_line, '#');
                if (hash) *hash = '\0';
                size_t len = strlen(inc_line);
                while (len > 0 && (inc_line[len - 1] == '\r' || inc_line[len - 1] == ' ' || inc_line[len - 1] == '\t')) inc_line[--len] = '\0';
                char *inc_tok[32];
                int inc_n = tokenize(inc_line, inc_tok, 32);
                if (inc_n == 0) continue;
                if (!parse_look_line(lk, inc_tok, inc_n, inc_line_no, name, depth + 1))
                    SDL_Log("level_load: %s:%d: '%s' is not a look line (an included look file may only set the look)", name, inc_line_no, inc_tok[0]);
            }
            free(inc_buf);
        } else {
            SDL_Log("level_load:%d: unknown look sub-command '%s'", line_no, tok[1]);
        }
    } else {
        return false;
    }
    return true;
}

// Apply assets/looks/NAME.txt on top of a level that has already loaded. The only caller is the
// HOLLOW_LOOK env hook, whose whole job is shooting one view under several art styles without
// editing the level; a level that wants a look for keeps writes `look include NAME` in its own text.
void level_look_include(Level *lv, const char *name) {
    char cmd[] = "look", sub[] = "include", file[96];
    SDL_strlcpy(file, name, sizeof file);
    char *tok[3] = { cmd, sub, file };
    parse_look_line(&lv->look, tok, 3, 0, lv->path, 0);
    lv->fog_color = lv->look.fog_color;
}

// Parse the whole file into `out`. Returns false and logs on any fatal (I/O) error;
// bad individual lines are warned about and skipped, not fatal.
static bool parse_level(Level *out, const char *path) {
    memset(out, 0, sizeof *out);
    out->fog_color = v3(0, 0, 0);
    out->light_color = v3(1, 1, 1);
    out->ambient = 0.2f;
    out->view = VIEW_TOP;
    out->view_bob = 1.0f;
    SDL_strlcpy(out->scene_intro, "intro.txt", sizeof out->scene_intro);
    SDL_strlcpy(out->scene_boss, "boss_intro.txt", sizeof out->scene_boss);
    SDL_strlcpy(out->scene_victory, "victory.txt", sizeof out->scene_victory);
    out->look = (Look){
        .sun_dir = v3(0.3f, -0.8f, 0.5f), .sun_intensity = 1.0f, .sun_color = v3(1, 0.95f, 0.85f),
        .sky_ambient = v3(0.25f, 0.3f, 0.45f), .ground_ambient = v3(0.08f, 0.07f, 0.06f),
        .fog_color = v3(0.05f, 0.06f, 0.1f), .fog_density = 0.03f, .fog_base = 0.0f, .fog_falloff = 0.15f, .fog_scatter = 0.5f, .fog_start = 4.0f,
        .sky_zenith = v3(0.02f, 0.03f, 0.08f), .sky_horizon = v3(0.15f, 0.12f, 0.2f), .sky_ground = v3(0.02f, 0.02f, 0.03f), .sun_glow = 0.6f, .stars = 1.0f, .sky_fog_blend = 0.6f,
        .toon_softness = 0.08f, .shadow_floor = 0.15f, .rim_power = 3.0f,
        .exposure = 1.0f, .saturation = 1.1f, .contrast = 1.05f, .bloom = 0.35f, .bloom_threshold = 1.0f,
        .lift = v3(0.01f, 0.01f, 0.03f), .gain = v3(1, 1, 1),
        .pixel_scale = 3, .pixel_levels = 8, .pixel_outline = 1, .pixel_palette = 1, .pixel_inner = 0.6f, .shadow = 1.0f, .style_snap = 0, .style_outline = 0, .style_levels = 0, .style_pixel = 1, .cam_pitch = 36, .cam_dist = 14, .cam_fov = 32, .cam_yaw = -35, .view_far = 80,
        .daytime = -1.0f,
        .render_scale = 1, .render_nearest = 0, .tex_cap = 0, .flat = 0, .grain = 0, .vignette = 0.45f, .chroma = 0, .dither = 0, .ink_width = 1, .ink_wobble = 0, .ink_luma = 0, .hatch = 0, .paper = 0 };

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

    PartCollide part_cache[PART_COLLIDE_CACHE]; int npart_cache = 0;
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

        // Every line that only sets how the level looks (sun/ambient/fogv/sky/toon/grade/lift/gain/
        // shadow/style/pixel/daytime/look) is handled here, including a `look include` that pulls
        // more of the same from assets/looks/. `fogv` inside it also lands in out->look.fog_color, so
        // refresh the legacy out->fog_color mirror every time -- the old `fog` line below still wins
        // if it comes later, same as before this line existed.
        if (parse_look_line(&out->look, tok, n, line_no, path, 0)) { out->fog_color = out->look.fog_color; continue; }

        if (strcmp(cmd, "fog") == 0) {
            float f[5];
            if (n != 6 || !parse_floats(tok, 1, 5, f)) { SDL_Log("level_load:%d: bad fog line", line_no); continue; }
            out->fog_color = v3(f[0], f[1], f[2]);
            out->fog_near = f[3];
            out->fog_far = f[4];

        } else if (strcmp(cmd, "light") == 0 && n == 8) {
            float f[7];
            if (!parse_floats(tok, 1, 7, f)) { SDL_Log("level_load:%d: bad light line", line_no); continue; }
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
            // block x y z sx sy sz tex r g b tile [solid|pass]   (12 tokens without the word default to solid)
            if (n != 12 && n != 13 && n != 14) { SDL_Log("level_load:%d: bad block line", line_no); continue; }
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
            if (n > 12) {
                if (strcmp(tok[n - 1], "pass") == 0) solid = false;
                else if (strcmp(tok[n - 1], "solid") == 0) solid = true;
                else { SDL_Log("level_load:%d: bad block solidity token '%s'", line_no, tok[n - 1]); continue; }
            }
            if (out->nblocks >= LEVEL_MAX_BLOCKS) { SDL_Log("level_load:%d: too many blocks", line_no); continue; }
            Block *b = &out->blocks[out->nblocks++];
            b->center = v3(pos[0], pos[1], pos[2]);
            b->size = v3(size3[0], size3[1], size3[2]);
            b->tex = tex;
            b->tint = v4(tint[0], tint[1], tint[2], 1.0f);
            b->uv_tile = tile;
            b->solid = solid;

        } else if (strcmp(cmd, "scene") == 0) {
            // scene intro|boss|victory FILE   (FILE under assets/scenes/, e.g. glade_intro.txt)
            if (n != 3) { SDL_Log("level_load:%d: bad scene line", line_no); continue; }
            if (strcmp(tok[1], "intro") == 0) SDL_strlcpy(out->scene_intro, tok[2], sizeof out->scene_intro);
            else if (strcmp(tok[1], "boss") == 0) SDL_strlcpy(out->scene_boss, tok[2], sizeof out->scene_boss);
            else if (strcmp(tok[1], "victory") == 0) SDL_strlcpy(out->scene_victory, tok[2], sizeof out->scene_victory);
            else if (out->nscenes < 16) { SDL_strlcpy(out->scenes[out->nscenes].name, tok[1], 32); SDL_strlcpy(out->scenes[out->nscenes].file, tok[2], 128); out->nscenes++; }
            else SDL_Log("level_load:%d: unknown scene slot '%s'", line_no, tok[1]);
        } else if (strcmp(cmd, "terrain") == 0) {
            // terrain FILE   (base name relative to assets/, e.g. levels/glade_terrain)
            if (n != 2) { SDL_Log("level_load:%d: bad terrain line", line_no); continue; }
            SDL_strlcpy(out->terrain_file, tok[1], sizeof out->terrain_file);
        } else if (strcmp(cmd, "combat") == 0) {
            out->combat_realtime = n >= 2 && strcmp(tok[1], "realtime") == 0;
        } else if (strcmp(cmd, "view") == 0) {
            // view top | third | first [BOB]   (BOB scales the first-person head bob: 1 subtle, 0 off)
            out->view = VIEW_TOP;
            if (n >= 2 && strcmp(tok[1], "third") == 0) out->view = VIEW_THIRD;
            else if (n >= 2 && strcmp(tok[1], "first") == 0) out->view = VIEW_FIRST;
            out->view_bob = 1.0f;
            if (n >= 3) { float f[1]; if (parse_floats(tok, 2, 1, f)) out->view_bob = f[0]; }
        } else if (strcmp(cmd, "camera") == 0) {
            // camera PITCH DIST FOV [YAW]   (overworld camera: 36 14 32 -35 is the isometric default; 25 9 50 = closer over-the-shoulder)
            float f[4] = { 36, 14, 32, -35 };
            if (n < 4 || !parse_floats(tok, 1, n - 1 > 4 ? 4 : n - 1, f)) { SDL_Log("level_load:%d: bad camera line", line_no); continue; }
            out->look.cam_pitch = f[0]; out->look.cam_dist = f[1]; out->look.cam_fov = f[2]; out->look.cam_yaw = f[3];
        } else if (strcmp(cmd, "prop") == 0) {
            // prop FILE x y z yaw scale [tint r g b] [glow r g b] [collide R]
            float f[5];
            if (n < 7 || !parse_floats(tok, 2, 5, f)) { SDL_Log("level_load:%d: bad prop line", line_no); continue; }
            if (out->nprops >= LEVEL_MAX_PROPS) { SDL_Log("level_load:%d: too many props", line_no); continue; }
            Prop *pr = &out->props[out->nprops++];
            memset(pr, 0, sizeof *pr);
            SDL_strlcpy(pr->file, tok[1], sizeof pr->file);
            pr->pos = v3(f[0], f[1], f[2]); pr->yaw = f[3] * DEG2RAD; pr->scale = f[4]; pr->tint = v4(1, 1, 1, 1); pr->stretch = v3(1, 1, 1);
            pr->tex = -1; pr->tex_tile = 1;   // the memset above zeroed these; 0 would mean TEX_STONE
            if (pr->scale <= 0) {
                // prop has no pitch field, so a line written as if it did puts its pitch into scale;
                // a .part is how something actually gets tilted.
                SDL_Log("level_load:%d: prop '%s' has scale %.3f -- prop is 'FILE x y z yaw scale' with no pitch field, tilt with a .part instead; using scale 1", line_no, pr->file, pr->scale);
                pr->scale = 1;
            }
            pr->collide_block = -1;
            int i = 7;
            while (i < n) {
                float g3[3];
                if (strcmp(tok[i], "tint") == 0 && i + 3 < n && parse_floats(tok, i + 1, 3, g3)) { pr->tint = v4(g3[0], g3[1], g3[2], 1); i += 4; }
                else if (strcmp(tok[i], "glow") == 0 && i + 3 < n && parse_floats(tok, i + 1, 3, g3)) { pr->glow = v3(g3[0], g3[1], g3[2]); i += 4; }
                else if (strcmp(tok[i], "collide") == 0 && i + 1 < n && parse_floats(tok, i + 1, 1, g3)) {
                    // collide R [H] [deck]: R is the half-width of an invisible box at the prop's
                    // base, H its height (default 5), `deck` makes its top face a floor instead of
                    // a wall (piers, boat decks, anything you are meant to walk on).
                    pr->collide = g3[0]; i += 2;
                    if (i < n && parse_floats(tok, i, 1, g3)) { pr->collide_h = g3[0]; i += 1; }
                    if (i < n && strcmp(tok[i], "deck") == 0) { pr->collide_deck = true; i += 1; }
                }
                else if (strcmp(tok[i], "stretch") == 0 && i + 3 < n && parse_floats(tok, i + 1, 3, g3)) { pr->stretch = v3(g3[0], g3[1], g3[2]); i += 4; }
                else if (strcmp(tok[i], "name") == 0 && i + 1 < n) { SDL_strlcpy(pr->name, tok[i + 1], sizeof pr->name); i += 2; }   // a cutscene actor: `actor prop:NAME move ...`
                else if (strcmp(tok[i], "tex") == 0 && i + 1 < n) {
                    // tex NAME [TILE]: one texture over every piece of the prop, projected in
                    // world space at TILE repeats per metre so scale and stretch never smear it.
                    int t = level_tex_from_name(tok[i + 1]);
                    if (t < 0) SDL_Log("level_load:%d: unknown texture '%s'", line_no, tok[i + 1]);
                    else pr->tex = t;
                    i += 2;
                    if (i < n && parse_floats(tok, i, 1, g3)) { pr->tex_tile = g3[0]; i += 1; }
                }
                else { SDL_Log("level_load:%d: bad prop option '%s', ignoring the rest of the line", line_no, tok[i]); break; }
            }
            if (pr->collide <= 0) {   // no collider on the line: take the part's own, scaled like the prop
                float r, h; bool deck;
                if (part_collider(part_cache, &npart_cache, pr->file, &r, &h, &deck)) {
                    pr->collide = r * pr->scale; pr->collide_h = h * pr->scale; pr->collide_deck = deck;
                    pr->collide_default = true;
                }
            }
            if (pr->collide > 0 && out->nblocks < LEVEL_MAX_BLOCKS) {
                // Invisible box for the trunk / body / deck of the prop
                float h = pr->collide_h > 0 ? pr->collide_h : 5.0f;   // default: tall enough that the camera cannot peek over a wall
                Block *b = &out->blocks[out->nblocks++];
                memset(b, 0, sizeof *b);
                b->center = v3(pr->pos.x, pr->pos.y + h * 0.5f, pr->pos.z); b->size = v3(pr->collide * 2, h, pr->collide * 2);
                b->tex = -1; b->solid = !pr->collide_deck; b->platform = pr->collide_deck; b->tint = v4(1, 1, 1, 0);
                pr->collide_block = out->nblocks - 1;
            }
        } else if (strcmp(cmd, "light") == 0 && n >= 9 && n != 8) {
            // light x y z r g b radius intensity [flicker F]
            float f[8];
            if (!parse_floats(tok, 1, 8, f)) { SDL_Log("level_load:%d: bad light line", line_no); continue; }
            if (out->nlights >= LEVEL_MAX_LIGHTS) { SDL_Log("level_load:%d: too many lights", line_no); continue; }
            LevelLight *l = &out->lights[out->nlights++];
            memset(l, 0, sizeof *l);
            l->pos = v3(f[0], f[1], f[2]); l->color = v3(f[3], f[4], f[5]); l->radius = f[6]; l->intensity = f[7];
            if (n >= 11 && strcmp(tok[9], "flicker") == 0) l->flicker = (float)SDL_strtod(tok[10], NULL);
        } else if (strcmp(cmd, "collider") == 0) {
            float f[6];
            if (n != 7 || !parse_floats(tok, 1, 6, f)) { SDL_Log("level_load:%d: bad collider line", line_no); continue; }
            if (out->nblocks >= LEVEL_MAX_BLOCKS) { SDL_Log("level_load:%d: too many blocks", line_no); continue; }
            Block *b = &out->blocks[out->nblocks++];
            memset(b, 0, sizeof *b);
            b->center = v3(f[0], f[1], f[2]); b->size = v3(f[3], f[4], f[5]); b->tex = -1; b->solid = true; b->tint = v4(1, 1, 1, 0);
        } else if (strcmp(cmd, "emitter") == 0) {
            // emitter TYPE x y z ex ey ez rate r g b size life
            float f[12];
            if (n != 14 || !parse_floats(tok, 2, 12, f)) { SDL_Log("level_load:%d: bad emitter line", line_no); continue; }
            if (out->nemitters >= LEVEL_MAX_EMITTERS) { SDL_Log("level_load:%d: too many emitters", line_no); continue; }
            LevelEmitter *e = &out->emitters[out->nemitters++];
            memset(e, 0, sizeof *e);
            SDL_strlcpy(e->type, tok[1], sizeof e->type);
            e->pos = v3(f[0], f[1], f[2]); e->extent = v3(f[3], f[4], f[5]); e->rate = f[6]; e->color = v3(f[7], f[8], f[9]); e->size = f[10]; e->life = f[11];
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

        } else if (strcmp(cmd, "npc") == 0) {
            // npc NAME CHARACTER x y z yaw SCENE [radius]
            float f[4];
            if (n < 8 || !parse_floats(tok, 3, 4, f)) { SDL_Log("level_load:%d: bad npc line", line_no); continue; }
            if (out->nnpcs >= LEVEL_MAX_NPCS) { SDL_Log("level_load:%d: too many npcs", line_no); continue; }
            Npc *np = &out->npcs[out->nnpcs++]; memset(np, 0, sizeof *np);
            SDL_strlcpy(np->name, tok[1], sizeof np->name); SDL_strlcpy(np->file, tok[2], sizeof np->file); SDL_strlcpy(np->scene, tok[7], sizeof np->scene);
            np->pos = v3(f[0], f[1], f[2]); np->yaw = f[3] * DEG2RAD; np->radius = n >= 9 ? (float)atof(tok[8]) : 2.5f;
        } else if (strcmp(cmd, "item") == 0) {
            // item NAME x y z [yaw]
            if (n != 5 && n != 6) { SDL_Log("level_load:%d: bad item line", line_no); continue; }
            float f[4];
            if (!parse_floats(tok, 2, n - 2, f)) { SDL_Log("level_load:%d: bad item line", line_no); continue; }
            if (out->nitems >= LEVEL_MAX_ITEMS) { SDL_Log("level_load:%d: too many items", line_no); continue; }
            LevelItem *it = &out->items[out->nitems++]; memset(it, 0, sizeof *it);
            SDL_strlcpy(it->name, tok[1], sizeof it->name);
            it->pos = v3(f[0], f[1], f[2]); it->yaw = n == 6 ? f[3] * DEG2RAD : 0.0f;

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
    // Parse into a scratch copy so a broken file leaves the loaded level alone -- but a Level is
    // 620 kB, and on Windows the main thread's whole stack is 1 MB, so the scratch copy is a
    // heap allocation rather than a local.
    Level *tmp = malloc(sizeof *tmp);
    if (!tmp) return false;
    if (!parse_level(tmp, path)) { free(tmp); return false; }

    SDL_PathInfo info;
    long long mtime = 0;
    if (SDL_GetPathInfo(path, &info)) mtime = (long long)info.modify_time;

    SDL_strlcpy(tmp->path, path, sizeof tmp->path);
    tmp->mtime = mtime;

    *lv = *tmp;
    free(tmp);
    return true;
}

// Asked once a tick, which is sixty stats of the same file a second on a disk that is not going to
// answer differently -- and on Windows a stat is a syscall with a path walk behind it. A level file
// is saved by a human or by the editor; once a second is as fast as anyone can notice and it is
// what every other hot reload in this tree already does (props_hot_reload, hero_hot_reload,
// gfx_palette_update).
bool level_reload_if_changed(Level *lv) {
    if (lv->path[0] == '\0') return false;
    static Uint64 last = 0; Uint64 now = SDL_GetTicks();
    if (now - last < 1000) return false;
    last = now;
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(lv->path, &info)) return false;
    long long mtime = (long long)info.modify_time;
    if (mtime <= lv->mtime) return false;
    char path[512];
    SDL_strlcpy(path, lv->path, sizeof path);
    return level_load(lv, path);
}

// True if `b` is an invisible collider block auto-generated from a prop's `collide R` option
// (see the "prop" case in parse_level). Such blocks aren't written out on their own; they're
// re-created from the prop line when the file is reloaded.
static bool is_auto_prop_collider(const Level *lv, const Block *b) {
    for (int i = 0; i < lv->nprops; i++) {
        const Prop *pr = &lv->props[i];
        if (pr->collide <= 0.0f) continue;
        float h = pr->collide_h > 0 ? pr->collide_h : 5.0f;
        if (fabsf(b->center.x - pr->pos.x) < 1e-3f && fabsf(b->center.z - pr->pos.z) < 1e-3f &&
            fabsf(b->size.x - pr->collide * 2.0f) < 1e-3f && fabsf(b->size.y - h) < 1e-3f)
            return true;
    }
    return false;
}

bool level_save(const Level *lv, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { SDL_Log("level_save: could not write '%s'", path); return false; }

    fprintf(f, "# written by the level editor\n");

    const Look *lk = &lv->look;
    fprintf(f, "sun      %.3f %.3f %.3f   %.3f   %.3f %.3f %.3f\n",
            lk->sun_dir.x, lk->sun_dir.y, lk->sun_dir.z, lk->sun_intensity,
            lk->sun_color.x, lk->sun_color.y, lk->sun_color.z);
    fprintf(f, "ambient  %.3f %.3f %.3f   %.3f %.3f %.3f\n",
            lk->sky_ambient.x, lk->sky_ambient.y, lk->sky_ambient.z,
            lk->ground_ambient.x, lk->ground_ambient.y, lk->ground_ambient.z);
    fprintf(f, "fogv     %.3f %.3f %.3f   %.3f   %.3f  %.3f  %.3f  %.3f\n",
            lk->fog_color.x, lk->fog_color.y, lk->fog_color.z, lk->fog_density,
            lk->fog_base, lk->fog_falloff, lk->fog_scatter, lk->fog_start);
    fprintf(f, "sky      %.3f %.3f %.3f   %.3f %.3f %.3f   %.3f %.3f %.3f   %.3f  %.3f  %.3f\n",
            lk->sky_zenith.x, lk->sky_zenith.y, lk->sky_zenith.z,
            lk->sky_horizon.x, lk->sky_horizon.y, lk->sky_horizon.z,
            lk->sky_ground.x, lk->sky_ground.y, lk->sky_ground.z,
            lk->sun_glow, lk->stars, lk->sky_fog_blend);
    fprintf(f, "toon     %.3f %.3f %.3f\n", lk->toon_softness, lk->shadow_floor, lk->rim_power);
    fprintf(f, "grade    %.3f %.3f %.3f %.3f %.3f\n",
            lk->exposure, lk->saturation, lk->contrast, lk->bloom, lk->bloom_threshold);
    fprintf(f, "lift     %.3f %.3f %.3f\n", lk->lift.x, lk->lift.y, lk->lift.z);
    fprintf(f, "gain     %.3f %.3f %.3f\n", lk->gain.x, lk->gain.y, lk->gain.z);
    fprintf(f, "pixel    %.0f %.0f %.2f %.0f %.2f\n", lk->pixel_scale, lk->pixel_levels, lk->pixel_outline, lk->pixel_palette, lk->pixel_inner);
    fprintf(f, "shadow   %.2f\n", lk->shadow);
    if (lk->view_far != 80.0f) fprintf(f, "look far %.1f\n", lk->view_far);
    if (lk->render_scale != 1.0f || lk->render_nearest != 0.0f)
        fprintf(f, "look res %.3f %s\n", lk->render_scale, lk->render_nearest != 0.0f ? "nearest" : "linear");
    if (lk->tex_cap != 0.0f)  fprintf(f, "look texcap %.0f\n", lk->tex_cap);
    if (lk->flat != 0.0f)     fprintf(f, "look flat %.3f\n", lk->flat);
    if (lk->grain != 0.0f)    fprintf(f, "look grain %.3f\n", lk->grain);
    if (lk->vignette != 0.45f) fprintf(f, "look vignette %.3f\n", lk->vignette);
    if (lk->chroma != 0.0f)   fprintf(f, "look chroma %.3f\n", lk->chroma);
    if (lk->dither != 0.0f)   fprintf(f, "look dither %.3f\n", lk->dither);
    if (lk->ink_width != 1.0f || lk->ink_wobble != 0.0f || lk->ink_luma != 0.0f)
        fprintf(f, "look ink %.3f %.3f %.3f\n", lk->ink_width, lk->ink_wobble, lk->ink_luma);
    if (lk->hatch != 0.0f)    fprintf(f, "look hatch %.3f\n", lk->hatch);
    if (lk->paper != 0.0f)    fprintf(f, "look paper %.3f\n", lk->paper);
    fprintf(f, "style    %.2f %.2f %.0f %.0f\n", lk->style_snap, lk->style_outline, lk->style_levels, lk->style_pixel);
    fprintf(f, "camera   %.0f %.1f %.0f %.0f\n", lk->cam_pitch, lk->cam_dist, lk->cam_fov, lk->cam_yaw);
    if (lk->daytime >= 0) fprintf(f, "daytime  %.2f\n", lk->daytime);

    if (lv->scene_intro[0])   fprintf(f, "scene intro %s\n", lv->scene_intro);
    if (lv->scene_boss[0])    fprintf(f, "scene boss %s\n", lv->scene_boss);
    if (lv->scene_victory[0]) fprintf(f, "scene victory %s\n", lv->scene_victory);
    if (lv->combat_realtime) fprintf(f, "combat realtime\n");
    if (lv->view == VIEW_THIRD) fprintf(f, "view third\n");
    else if (lv->view == VIEW_FIRST) { if (lv->view_bob == 1.0f) fprintf(f, "view first\n"); else fprintf(f, "view first %.2f\n", lv->view_bob); }
    for (int i = 0; i < lv->nscenes; i++) fprintf(f, "scene %s %s\n", lv->scenes[i].name, lv->scenes[i].file);
    for (int i = 0; i < lv->nnpcs; i++) fprintf(f, "npc %s %s %.3f %.3f %.3f %.1f %s %.2f\n", lv->npcs[i].name, lv->npcs[i].file, lv->npcs[i].pos.x, lv->npcs[i].pos.y, lv->npcs[i].pos.z, lv->npcs[i].yaw / DEG2RAD, lv->npcs[i].scene, lv->npcs[i].radius);
    for (int i = 0; i < lv->nitems; i++)
        fprintf(f, "item %s %.3f %.3f %.3f %.1f\n", lv->items[i].name, lv->items[i].pos.x, lv->items[i].pos.y, lv->items[i].pos.z, lv->items[i].yaw / DEG2RAD);
    if (lv->terrain_file[0])  fprintf(f, "terrain %s\n", lv->terrain_file);

    fprintf(f, "spawn %.3f %.3f %.3f %.3f\n",
            lv->spawn.x, lv->spawn.y, lv->spawn.z, lv->spawn_yaw * (180.0f / PI));
    fprintf(f, "boss  %.3f %.3f %.3f %.3f\n",
            lv->boss_spawn.x, lv->boss_spawn.y, lv->boss_spawn.z, lv->boss_yaw * (180.0f / PI));
    fprintf(f, "arena %.3f %.3f %.3f %.3f %.3f %.3f\n",
            lv->arena_min.x, lv->arena_min.y, lv->arena_min.z,
            lv->arena_max.x, lv->arena_max.y, lv->arena_max.z);

    for (int i = 0; i < lv->nblocks; i++) {
        const Block *b = &lv->blocks[i];
        if (b->tex < 0) {
            if (is_auto_prop_collider(lv, b)) continue; // re-created from its prop's "collide" option
            fprintf(f, "collider %.3f %.3f %.3f %.3f %.3f %.3f\n",
                    b->center.x, b->center.y, b->center.z, b->size.x, b->size.y, b->size.z);
            continue;
        }
        fprintf(f, "block %.3f %.3f %.3f  %.3f %.3f %.3f  %s  %.3f %.3f %.3f  %.3f  %s\n",
                b->center.x, b->center.y, b->center.z,
                b->size.x, b->size.y, b->size.z,
                level_tex_name(b->tex),
                b->tint.x, b->tint.y, b->tint.z,
                b->uv_tile,
                b->solid ? "solid" : "pass");
    }

    for (int i = 0; i < lv->nprops; i++) {
        const Prop *pr = &lv->props[i];
        fprintf(f, "prop %s %.3f %.3f %.3f %.3f %.3f",
                pr->file, pr->pos.x, pr->pos.y, pr->pos.z, pr->yaw * (180.0f / PI), pr->scale);
        if (pr->tint.x != 1.0f || pr->tint.y != 1.0f || pr->tint.z != 1.0f)
            fprintf(f, " tint %.3f %.3f %.3f", pr->tint.x, pr->tint.y, pr->tint.z);
        if (pr->glow.x != 0.0f || pr->glow.y != 0.0f || pr->glow.z != 0.0f)
            fprintf(f, " glow %.3f %.3f %.3f", pr->glow.x, pr->glow.y, pr->glow.z);
        if (pr->stretch.x != 1.0f || pr->stretch.y != 1.0f || pr->stretch.z != 1.0f)
            fprintf(f, " stretch %.3f %.3f %.3f", pr->stretch.x, pr->stretch.y, pr->stretch.z);
        if (pr->name[0]) fprintf(f, " name %s", pr->name);
        if (pr->tex >= 0) fprintf(f, " tex %s %.3f", level_tex_name(pr->tex), pr->tex_tile > 0 ? pr->tex_tile : 1.0f);
        if (pr->collide > 0.0f && !pr->collide_default) {
            fprintf(f, " collide %.3f", pr->collide);
            if (pr->collide_h > 0.0f || pr->collide_deck) fprintf(f, " %.3f", pr->collide_h > 0 ? pr->collide_h : 5.0f);
            if (pr->collide_deck) fprintf(f, " deck");
        }
        fprintf(f, "\n");
    }

    for (int i = 0; i < lv->nlights; i++) {
        const LevelLight *l = &lv->lights[i];
        fprintf(f, "light %.3f %.3f %.3f  %.3f %.3f %.3f  %.3f %.3f",
                l->pos.x, l->pos.y, l->pos.z, l->color.x, l->color.y, l->color.z,
                l->radius, l->intensity);
        if (l->flicker > 0.0f) fprintf(f, " flicker %.3f", l->flicker);
        fprintf(f, "\n");
    }

    for (int i = 0; i < lv->nemitters; i++) {
        const LevelEmitter *e = &lv->emitters[i];
        fprintf(f, "emitter %s %.3f %.3f %.3f  %.3f %.3f %.3f  %.3f  %.3f %.3f %.3f  %.3f  %.3f\n",
                e->type, e->pos.x, e->pos.y, e->pos.z, e->extent.x, e->extent.y, e->extent.z,
                e->rate, e->color.x, e->color.y, e->color.z, e->size, e->life);
    }

    for (int i = 0; i < lv->ncams; i++) {
        const CamVolume *c = &lv->cams[i];
        fprintf(f, "cam %s %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f\n",
                c->name, c->vmin.x, c->vmin.y, c->vmin.z, c->vmax.x, c->vmax.y, c->vmax.z,
                c->eye.x, c->eye.y, c->eye.z, c->target.x, c->target.y, c->target.z, c->fov);
    }

    for (int i = 0; i < lv->ntriggers; i++) {
        const Trigger *t = &lv->triggers[i];
        fprintf(f, "trigger %s %.3f %.3f %.3f %.3f %.3f %.3f%s\n",
                t->name, t->vmin.x, t->vmin.y, t->vmin.z, t->vmax.x, t->vmax.y, t->vmax.z,
                t->once ? " once" : "");
    }

    bool ok = (fclose(f) == 0);
    if (!ok) SDL_Log("level_save: error writing '%s'", path);
    return ok;
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
            // Thin floors and low steps are walked over, not collided with: anything whose top is
            // within a step of the feet is climbed by level_ground instead of stopping the move.
            if (b->center.y + b->size.y * 0.5f <= y0 + LEVEL_STEP_UP) continue;

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
            // Already overlapping before the move (spawned inside, or pushed in by
            // something else): never eject, just let the motion continue.
            if (primary > touch_lo && primary < touch_hi) continue;
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

float level_ground(const Level *lv, Vec3 pos, float base, int *block) {
    float best = base; int bi = -1;
    float reach = pos.y + LEVEL_STEP_UP;   // a surface higher than this is something to walk into, not onto
    for (int i = 0; i < lv->nblocks; i++) {
        const Block *b = &lv->blocks[i];
        if (!b->solid && !b->platform) continue;
        float top = b->center.y + b->size.y * 0.5f;
        if (top > reach || top <= best) continue;
        if (fabsf(pos.x - b->center.x) > b->size.x * 0.5f) continue;
        if (fabsf(pos.z - b->center.z) > b->size.z * 0.5f) continue;
        best = top; bi = i;
    }
    if (block) *block = bi;
    return best;
}

Vec3 level_move(const Level *lv, Vec3 pos, float radius, float height, Vec3 delta) {
    float x = pos.x, z = pos.z;
    float y0 = pos.y, y1 = pos.y + height;

    x = move_axis(lv, x, z, y0, y1, radius, delta.x, 0);
    z = move_axis(lv, z, x, y0, y1, radius, delta.z, 1);

    return v3(x, pos.y + delta.y, z);
}

// --- traversal --- see level.h. level_ground stops at a step because that is what walking is;
// these three are what climbing needs, and they deliberately ask about one column at a time so the
// mantle probe can sample the ledge, the ground beyond it and the face in between with the same call.
float level_top_at(const Level *lv, float x, float z, float ceiling, int *block) {
    float best = -1e9f; int bi = -1;
    for (int i = 0; i < lv->nblocks; i++) {
        const Block *b = &lv->blocks[i];
        if (!b->solid && !b->platform) continue;
        float top = b->center.y + b->size.y * 0.5f;
        if (top > ceiling || top <= best) continue;
        if (fabsf(x - b->center.x) > b->size.x * 0.5f) continue;
        if (fabsf(z - b->center.z) > b->size.z * 0.5f) continue;
        best = top; bi = i;
    }
    if (block) *block = bi;
    return best;
}

bool level_clear(const Level *lv, float x, float z, float radius, float y0, float y1) {
    if (y1 <= y0) return true;
    for (int i = 0; i < lv->nblocks; i++) {
        const Block *b = &lv->blocks[i];
        if (!b->solid) continue;
        if (b->center.y - b->size.y * 0.5f >= y1 || b->center.y + b->size.y * 0.5f <= y0) continue;
        float dx = fabsf(x - b->center.x) - b->size.x * 0.5f;
        float dz = fabsf(z - b->center.z) - b->size.z * 0.5f;
        if (dx < 0) dx = 0;
        if (dz < 0) dz = 0;
        if (dx * dx + dz * dz < radius * radius) return false;
    }
    return true;
}

bool level_wall_near(const Level *lv, float x, float z, float radius, float y0, float y1, Vec3 *normal, float *gap) {
    float best = radius; bool found = false;
    for (int i = 0; i < lv->nblocks; i++) {
        const Block *b = &lv->blocks[i];
        if (!b->solid) continue;
        if (b->center.y - b->size.y * 0.5f >= y1 || b->center.y + b->size.y * 0.5f <= y0) continue;
        float hx = b->size.x * 0.5f, hz = b->size.z * 0.5f;
        float dx = x - b->center.x, dz = z - b->center.z;
        float ox = fabsf(dx) - hx, oz = fabsf(dz) - hz;
        // Inside the footprint there is no face to run on, only a block you are standing in.
        if (ox < 0 && oz < 0) continue;
        float cx = ox > 0 ? ox : 0, cz = oz > 0 ? oz : 0;
        float d = sqrtf(cx * cx + cz * cz);
        if (d >= best) continue;
        best = d; found = true;
        // The nearer of the two overhangs is the face you are alongside: a corner gives the axis
        // you are further outside of, which is the one whose face you would actually touch first.
        Vec3 n = (ox > oz) ? v3(dx > 0 ? 1.0f : -1.0f, 0, 0) : v3(0, 0, dz > 0 ? 1.0f : -1.0f);
        if (normal) *normal = n;
        if (gap) *gap = d;
    }
    return found;
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
        // "hold" is a query volume (the boat's cargo hold), not a scene cue; the explore loop
        // logs every trigger it gets here, so it must never fire on its own.
        if (strcmp(t->name, "hold") == 0) continue;
        if (point_in_box(p, t->vmin, t->vmax)) {
            if (t->once) t->fired = true;
            return t;
        }
    }
    return NULL;
}

const Trigger *level_trigger_named(const Level *lv, const char *name) {
    for (int i = 0; i < lv->ntriggers; i++)
        if (strcmp(lv->triggers[i].name, name) == 0) return &lv->triggers[i];
    return NULL;
}

void level_reset_triggers(Level *lv) {
    for (int i = 0; i < lv->ntriggers; i++) lv->triggers[i].fired = false;
}

// A collider narrower than this is something the camera is allowed to see through: a palm trunk,
// a bollard, a cactus. Without it the orbit camera bounces off every tree on the island.
#define CAMERA_IGNORE_RADIUS 0.6f

float level_ray_solid(const Level *lv, Vec3 a, Vec3 b, float margin) {
    float best = 1.0f;
    Vec3 d = v3_sub(b, a);
    for (int i = 0; i < lv->nblocks; i++) {
        const Block *bl = &lv->blocks[i];
        if (!bl->solid) continue;
        if (bl->tex < 0 && bl->size.x < CAMERA_IGNORE_RADIUS * 2 && bl->size.z < CAMERA_IGNORE_RADIUS * 2) continue;
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
