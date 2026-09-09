// Level file parser, hot reload and collision. See level.h and assets/levels/README.md.
#include "level.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *k_tex_names[TEX_COUNT] = { "stone", "tile", "wood", "metal", "flesh", "plaster", "flat" };

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
        .pixel_scale = 3, .pixel_levels = 8, .pixel_outline = 1, .pixel_palette = 1, .pixel_inner = 0.6f, .shadow = 1.0f, .style_snap = 0, .style_outline = 0, .style_levels = 0, .style_pixel = 1, .cam_pitch = 36, .cam_dist = 14, .cam_fov = 32, .cam_yaw = -35,
        .daytime = -1.0f };

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
        } else if (strcmp(cmd, "sun") == 0) {
            float f[7];
            if (n != 8 || !parse_floats(tok, 1, 7, f)) { SDL_Log("level_load:%d: bad sun line", line_no); continue; }
            out->look.sun_dir = v3(f[0], f[1], f[2]); out->look.sun_intensity = f[3]; out->look.sun_color = v3(f[4], f[5], f[6]);
        } else if (strcmp(cmd, "ambient") == 0) {
            float f[6];
            if (n != 7 || !parse_floats(tok, 1, 6, f)) { SDL_Log("level_load:%d: bad ambient line", line_no); continue; }
            out->look.sky_ambient = v3(f[0], f[1], f[2]); out->look.ground_ambient = v3(f[3], f[4], f[5]);
        } else if (strcmp(cmd, "fogv") == 0) {
            float f[8];
            if (n != 9 || !parse_floats(tok, 1, 8, f)) { SDL_Log("level_load:%d: bad fogv line", line_no); continue; }
            out->look.fog_color = v3(f[0], f[1], f[2]); out->look.fog_density = f[3]; out->look.fog_base = f[4];
            out->look.fog_falloff = f[5]; out->look.fog_scatter = f[6]; out->look.fog_start = f[7];
            out->fog_color = out->look.fog_color;
        } else if (strcmp(cmd, "sky") == 0) {
            float f[12];
            if (n != 13 || !parse_floats(tok, 1, 12, f)) { SDL_Log("level_load:%d: bad sky line", line_no); continue; }
            out->look.sky_zenith = v3(f[0], f[1], f[2]); out->look.sky_horizon = v3(f[3], f[4], f[5]); out->look.sky_ground = v3(f[6], f[7], f[8]);
            out->look.sun_glow = f[9]; out->look.stars = f[10]; out->look.sky_fog_blend = f[11];
        } else if (strcmp(cmd, "toon") == 0) {
            float f[3];
            if (n != 4 || !parse_floats(tok, 1, 3, f)) { SDL_Log("level_load:%d: bad toon line", line_no); continue; }
            out->look.toon_softness = f[0]; out->look.shadow_floor = f[1]; out->look.rim_power = f[2];
        } else if (strcmp(cmd, "grade") == 0) {
            float f[5];
            if (n != 6 || !parse_floats(tok, 1, 5, f)) { SDL_Log("level_load:%d: bad grade line", line_no); continue; }
            out->look.exposure = f[0]; out->look.saturation = f[1]; out->look.contrast = f[2]; out->look.bloom = f[3]; out->look.bloom_threshold = f[4];
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
        } else if (strcmp(cmd, "style") == 0) {
            // style SNAP OUTLINE LEVELS PIXEL
            float f[4] = { 0, 0, 0, 1 };
            if (n < 5 || !parse_floats(tok, 1, 4, f)) { SDL_Log("level_load:%d: bad style line", line_no); continue; }
            out->look.style_snap = f[0]; out->look.style_outline = f[1]; out->look.style_levels = f[2]; out->look.style_pixel = f[3];
        } else if (strcmp(cmd, "shadow") == 0) {
            float f[1]; if (n < 2 || !parse_floats(tok, 1, 1, f)) { SDL_Log("level_load:%d: bad shadow line", line_no); continue; }
            out->look.shadow = f[0];
        } else if (strcmp(cmd, "pixel") == 0) {
            // pixel SCALE LEVELS OUTLINE PALETTE [INNER]
            float f[5] = { 3, 8, 1, 0, 0.6f };
            if (n < 5 || !parse_floats(tok, 1, n - 1 > 5 ? 5 : n - 1, f)) { SDL_Log("level_load:%d: bad pixel line", line_no); continue; }
            out->look.pixel_scale = f[0]; out->look.pixel_levels = f[1]; out->look.pixel_outline = f[2]; out->look.pixel_palette = f[3]; out->look.pixel_inner = f[4];
        } else if (strcmp(cmd, "daytime") == 0) {
            // daytime HOUR   (0..24; the sun, sky and fog colour follow the clock)
            float f[1];
            if (n != 2 || !parse_floats(tok, 1, 1, f)) { SDL_Log("level_load:%d: bad daytime line", line_no); continue; }
            out->look.daytime = f[0];
        } else if (strcmp(cmd, "lift") == 0 || strcmp(cmd, "gain") == 0) {
            float f[3];
            if (n != 4 || !parse_floats(tok, 1, 3, f)) { SDL_Log("level_load:%d: bad %s line", line_no, cmd); continue; }
            if (cmd[0] == 'l') out->look.lift = v3(f[0], f[1], f[2]); else out->look.gain = v3(f[0], f[1], f[2]);
        } else if (strcmp(cmd, "prop") == 0) {
            // prop FILE x y z yaw scale [tint r g b] [glow r g b] [collide R]
            float f[5];
            if (n < 7 || !parse_floats(tok, 2, 5, f)) { SDL_Log("level_load:%d: bad prop line", line_no); continue; }
            if (out->nprops >= LEVEL_MAX_PROPS) { SDL_Log("level_load:%d: too many props", line_no); continue; }
            Prop *pr = &out->props[out->nprops++];
            memset(pr, 0, sizeof *pr);
            SDL_strlcpy(pr->file, tok[1], sizeof pr->file);
            pr->pos = v3(f[0], f[1], f[2]); pr->yaw = f[3] * DEG2RAD; pr->scale = f[4]; pr->tint = v4(1, 1, 1, 1); pr->stretch = v3(1, 1, 1);
            int i = 7;
            while (i < n) {
                float g3[3];
                if (strcmp(tok[i], "tint") == 0 && i + 3 < n && parse_floats(tok, i + 1, 3, g3)) { pr->tint = v4(g3[0], g3[1], g3[2], 1); i += 4; }
                else if (strcmp(tok[i], "glow") == 0 && i + 3 < n && parse_floats(tok, i + 1, 3, g3)) { pr->glow = v3(g3[0], g3[1], g3[2]); i += 4; }
                else if (strcmp(tok[i], "collide") == 0 && i + 1 < n && parse_floats(tok, i + 1, 1, g3)) { pr->collide = g3[0]; i += 2; }
                else if (strcmp(tok[i], "stretch") == 0 && i + 3 < n && parse_floats(tok, i + 1, 3, g3)) { pr->stretch = v3(g3[0], g3[1], g3[2]); i += 4; }
                else { SDL_Log("level_load:%d: bad prop option '%s'", line_no, tok[i]); break; }
            }
            if (pr->collide > 0 && out->nblocks < LEVEL_MAX_BLOCKS) {
                // Invisible solid box for the trunk / body of the prop
                Block *b = &out->blocks[out->nblocks++];
                memset(b, 0, sizeof *b);
                b->center = v3(pr->pos.x, pr->pos.y + 2.5f, pr->pos.z); b->size = v3(pr->collide * 2, 5.0f, pr->collide * 2);   // tall enough that the camera cannot peek over a wall
                b->tex = -1; b->solid = true; b->tint = v4(1, 1, 1, 0);
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

// True if `b` is an invisible collider block auto-generated from a prop's `collide R` option
// (see the "prop" case in parse_level). Such blocks aren't written out on their own; they're
// re-created from the prop line when the file is reloaded.
static bool is_auto_prop_collider(const Level *lv, const Block *b) {
    for (int i = 0; i < lv->nprops; i++) {
        const Prop *pr = &lv->props[i];
        if (pr->collide <= 0.0f) continue;
        if (fabsf(b->center.x - pr->pos.x) < 1e-3f && fabsf(b->center.z - pr->pos.z) < 1e-3f &&
            fabsf(b->size.x - pr->collide * 2.0f) < 1e-3f)
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
                k_tex_names[b->tex],
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
        if (pr->collide > 0.0f)
            fprintf(f, " collide %.3f", pr->collide);
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
            // Thin floors and low steps are walked over, not collided with.
            if (b->center.y + b->size.y * 0.5f <= y0 + 0.35f) continue;

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
