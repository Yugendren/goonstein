// Item definitions: assets/items/NAME.txt loading and caching. See items.h.
#include <SDL3/SDL.h>
#include "items.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Split a mutable string into whitespace-separated tokens; returns token count (capped at max_tok,
// so a line with more fields than expected comes back short and the caller's count check rejects it).
static int tokenize(char *s, char *tok[], int max_tok) {
    int n = 0;
    char *save = NULL;
    for (char *t = SDL_strtok_r(s, " \t", &save); t && n < max_tok; t = SDL_strtok_r(NULL, " \t", &save))
        tok[n++] = t;
    return n;
}

static bool parse_floats(char **tok, int count, float *out) {
    for (int i = 0; i < count; i++) {
        char *end = NULL;
        out[i] = (float)SDL_strtod(tok[i], &end);
        if (end == tok[i]) return false;
    }
    return true;
}

// Everything an ItemDef has before its file (if any) is read. `ok` starts false so a def that
// never finishes parsing is inert rather than half-configured.
static void itemdef_defaults(ItemDef *def, const char *name) {
    memset(def, 0, sizeof *def);
    SDL_strlcpy(def->name, name, sizeof def->name);
    SDL_strlcpy(def->display, name, sizeof def->display);
    def->mass = 5.0f;
    def->half = v3(0.25f, 0.25f, 0.25f);
    def->radius = 0.0f;
    def->fragile = 0.0f;
    def->value = 0;
    def->two_handed = false;
    def->scale = 1.0f;
    def->tint = v4(1, 1, 1, 1);
    def->sound = -1;
    def->weapon = 0; def->damage = 0; def->rate = 1.0f; def->wrange = 0;
    def->ammo = 0; def->knock = 0; def->pellets = 1; def->fire_sound = -1;
    def->grip = v3(0, 0, 0); def->grip_yaw = def->grip_pitch = def->grip_roll = 0;
    // --- projectiles / ammo --- inert until a `projectile`, `reserve` or `ammo TYPE N` line says
    // otherwise; proj_kind is derived from the name so it is the same number in every process.
    def->proj = false; def->proj_kind = itemdef_kind_hash(name);
    def->proj_speed = 0; def->proj_gravity = 0; def->proj_bounce = 0; def->proj_life = 0;
    def->proj_damage = 0; def->proj_radius = 0; def->proj_sound = -1; def->proj_model[0] = '\0';
    def->proj_scale = 1.0f; def->proj_spread = 0; def->proj_drag = 0;
    def->reserve = 0; def->ammo_type[0] = '\0'; def->pickup_type[0] = '\0'; def->pickup_n = 0;
    def->muzzle = v3(0, 0, 0); def->eject = v3(0, 0, 0);
    def->ok = false;
}

// A projectile's kind travels as one byte, and both ends have to agree what that byte means
// without the name being on the wire. Taking it from the item file's name rather than from the
// def's index is what makes it independent of the order the files happened to be loaded in: a
// client that has loaded three item files and a host that has loaded nine still call a nail a nail.
// FNV-1a, folded to 8 bits, with 0 reserved for "no projectile".
uint8_t itemdef_kind_hash(const char *name) {
    uint32_t h = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) { h ^= *p; h *= 16777619u; }
    uint8_t k = (uint8_t)((h ^ (h >> 8) ^ (h >> 16) ^ (h >> 24)) & 0xFFu);
    return k ? k : 1;
}

int itemdef_by_kind(const Items *its, uint8_t kind) {
    if (!kind) return -1;
    for (int i = 0; i < its->ndefs; i++)
        if (its->defs[i].ok && its->defs[i].proj && its->defs[i].proj_kind == kind) return i;
    return -1;
}

// Read and apply `path` onto `def` (defaults already set). Returns true if the file existed,
// parsed, and gave a `model`; false otherwise. Bad individual lines are warned about and skipped,
// not fatal to the rest of the file.
static bool itemdef_parse(ItemDef *def, const char *path) {
    size_t size = 0;
    void *data = SDL_LoadFile(path, &size);
    if (!data) {
        SDL_Log("itemdef: could not read '%s': %s", path, SDL_GetError());
        return false;
    }

    // Work on a NUL-terminated mutable copy so we can tokenize in place.
    char *buf = (char *)malloc(size + 1);
    if (!buf) { SDL_free(data); SDL_Log("itemdef: out of memory reading '%s'", path); return false; }
    memcpy(buf, data, size);
    buf[size] = '\0';
    SDL_free(data);

    bool have_model = false;
    int line_no = 0;
    char *save_line = NULL;
    for (char *line = SDL_strtok_r(buf, "\n", &save_line); line; line = SDL_strtok_r(NULL, "\n", &save_line)) {
        line_no++;
        // Strip trailing '\r' (CRLF files) and comments.
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t')) line[--len] = '\0';

        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') continue; // blank / comment-only line

        char *key = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
        while (*p == ' ' || *p == '\t') p++;
        char *value = p; // rest of the line; trailing whitespace already trimmed above

        if (strcmp(key, "model") == 0) {
            if (*value == '\0') { SDL_Log("itemdef:%d: '%s' empty model path", line_no, path); continue; }
            SDL_strlcpy(def->model, value, sizeof def->model);
            have_model = true;

        } else if (strcmp(key, "display") == 0) {
            if (*value == '\0') { SDL_Log("itemdef:%d: '%s' empty display name", line_no, path); continue; }
            SDL_strlcpy(def->display, value, sizeof def->display);

        } else if (strcmp(key, "mass") == 0) {
            char *end = NULL;
            float f = (float)SDL_strtod(value, &end);
            if (end == value) { SDL_Log("itemdef:%d: '%s' bad mass", line_no, path); continue; }
            if (f < 0.1f) { SDL_Log("itemdef:%d: '%s' mass %.3f clamped to 0.1", line_no, path, f); f = 0.1f; }
            def->mass = f;

        } else if (strcmp(key, "half") == 0) {
            char *tok[8];
            int n = tokenize(value, tok, 8);
            float f[3];
            if (n != 3 || !parse_floats(tok, 3, f)) { SDL_Log("itemdef:%d: '%s' bad half (want 3 numbers)", line_no, path); continue; }
            def->half = v3(f[0], f[1], f[2]);

        } else if (strcmp(key, "radius") == 0) {
            char *end = NULL;
            float f = (float)SDL_strtod(value, &end);
            if (end == value) { SDL_Log("itemdef:%d: '%s' bad radius", line_no, path); continue; }
            def->radius = f;

        } else if (strcmp(key, "fragile") == 0) {
            char *end = NULL;
            float f = (float)SDL_strtod(value, &end);
            if (end == value) { SDL_Log("itemdef:%d: '%s' bad fragile", line_no, path); continue; }
            def->fragile = f;

        } else if (strcmp(key, "value") == 0) {
            char *end = NULL;
            float f = (float)SDL_strtod(value, &end);
            if (end == value) { SDL_Log("itemdef:%d: '%s' bad value", line_no, path); continue; }
            def->value = (int)f;

        } else if (strcmp(key, "two_handed") == 0) {
            if (strcmp(value, "yes") == 0 || strcmp(value, "1") == 0 || strcmp(value, "true") == 0) def->two_handed = true;
            else if (strcmp(value, "no") == 0 || strcmp(value, "0") == 0 || strcmp(value, "false") == 0) def->two_handed = false;
            else { SDL_Log("itemdef:%d: '%s' bad two_handed value '%s'", line_no, path, value); continue; }

        } else if (strcmp(key, "scale") == 0) {
            char *end = NULL;
            float f = (float)SDL_strtod(value, &end);
            if (end == value) { SDL_Log("itemdef:%d: '%s' bad scale", line_no, path); continue; }
            if (f < 0.01f) { SDL_Log("itemdef:%d: '%s' scale %.4f clamped to 0.01", line_no, path, f); f = 0.01f; }
            def->scale = f;

        } else if (strcmp(key, "tint") == 0) {
            char *tok[8];
            int n = tokenize(value, tok, 8);
            float f[3];
            if (n != 3 || !parse_floats(tok, 3, f)) { SDL_Log("itemdef:%d: '%s' bad tint (want 3 numbers)", line_no, path); continue; }
            def->tint = v4(f[0], f[1], f[2], 1.0f);

        } else if (strcmp(key, "sound") == 0) {
            if (*value == '\0') { SDL_Log("itemdef:%d: '%s' empty sound name", line_no, path); continue; }
            int s = audio_sound_from_name(value);
            if (s < 0) SDL_Log("itemdef:%d: '%s' unknown sound '%s'", line_no, path, value);
            def->sound = s;

        // --- weapons --- a weapon is an item with `weapon melee|gun` and a few numbers. Anything
        // without that line is loot and never fires. See weapons.h for what the numbers do.
        } else if (strcmp(key, "weapon") == 0) {
            if (strcmp(value, "melee") == 0) def->weapon = 1;
            else if (strcmp(value, "gun") == 0) def->weapon = 2;
            else if (strcmp(value, "none") == 0) def->weapon = 0;
            else { SDL_Log("itemdef:%d: '%s' bad weapon '%s' (want melee, gun or none)", line_no, path, value); continue; }

        } else if (strcmp(key, "damage") == 0 || strcmp(key, "rate") == 0 || strcmp(key, "range") == 0 || strcmp(key, "knock") == 0) {
            char *end = NULL;
            float f = (float)SDL_strtod(value, &end);
            if (end == value) { SDL_Log("itemdef:%d: '%s' bad %s", line_no, path, key); continue; }
            if (key[0] == 'd') def->damage = fmaxf(f, 0);
            else if (key[0] == 'r' && key[1] == 'a') { if (f < 0.05f) { SDL_Log("itemdef:%d: '%s' rate %.3f clamped to 0.05", line_no, path, f); f = 0.05f; } def->rate = f; }
            else if (key[0] == 'r') def->wrange = fmaxf(f, 0);
            else def->knock = fmaxf(f, 0);

        } else if (strcmp(key, "ammo") == 0) {
            // Two lines share one word, told apart by shape. `ammo 12` is a magazine size on a
            // gun; `ammo pistol 24` is a box of rounds lying on the ground, which is a different
            // kind of item entirely. The brief asks for both spellings, so both are read here.
            char *tok[4];
            char copy[64]; SDL_strlcpy(copy, value, sizeof copy);
            int n = tokenize(copy, tok, 4);
            if (n == 2) {
                SDL_strlcpy(def->pickup_type, tok[0], sizeof def->pickup_type);
                def->pickup_n = SDL_atoi(tok[1]);
                if (def->pickup_n < 1) { SDL_Log("itemdef:%d: '%s' ammo pickup of %d rounds", line_no, path, def->pickup_n); def->pickup_n = 1; }
            } else if (n == 1) {
                char *end = NULL;
                float f = (float)SDL_strtod(tok[0], &end);
                if (end == tok[0]) { SDL_Log("itemdef:%d: '%s' bad ammo", line_no, path); continue; }
                int c = (int)f; if (c < 0) c = 0; if (c > 255) c = 255;
                def->ammo = c;
            } else { SDL_Log("itemdef:%d: '%s' bad ammo (want N, or TYPE N)", line_no, path); continue; }

        } else if (strcmp(key, "pellets") == 0) {
            char *end = NULL;
            float f = (float)SDL_strtod(value, &end);
            if (end == value) { SDL_Log("itemdef:%d: '%s' bad pellets", line_no, path); continue; }
            int n = (int)f;
            def->pellets = n < 1 ? 1 : (n > 24 ? 24 : n);

        } else if (strcmp(key, "reserve") == 0) {
            char *end = NULL;
            float f = (float)SDL_strtod(value, &end);
            if (end == value) { SDL_Log("itemdef:%d: '%s' bad reserve", line_no, path); continue; }
            int n = (int)f; if (n < 0) n = 0; if (n > 999) n = 999;
            def->reserve = n;

        } else if (strcmp(key, "ammo_type") == 0) {
            if (*value == '\0') { SDL_Log("itemdef:%d: '%s' empty ammo_type", line_no, path); continue; }
            SDL_strlcpy(def->ammo_type, value, sizeof def->ammo_type);

        // --- projectiles --- One line for the whole flying thing, in the order the brief names
        // them: projectile SPEED GRAVITY BOUNCE LIFE DAMAGE RADIUS SOUND MODEL. SOUND is a sound
        // name or "-"; MODEL is a path under assets/ or "-" for a lit streak with no mesh.
        } else if (strcmp(key, "projectile") == 0) {
            char *tok[12];
            int n = tokenize(value, tok, 12);
            float f[6];
            if (n < 8 || !parse_floats(tok, 6, f)) {
                SDL_Log("itemdef:%d: '%s' bad projectile (want SPEED GRAVITY BOUNCE LIFE DAMAGE RADIUS SOUND MODEL)", line_no, path);
                continue;
            }
            def->proj = true;
            def->proj_speed = fmaxf(f[0], 0.1f);
            def->proj_gravity = f[1];
            def->proj_bounce = clampf(f[2], 0.0f, 0.95f);
            def->proj_life = fmaxf(f[3], 0.05f);
            def->proj_damage = fmaxf(f[4], 0.0f);
            def->proj_radius = fmaxf(f[5], 0.0f);
            if (strcmp(tok[6], "-") != 0) {
                int s = audio_sound_from_name(tok[6]);
                if (s < 0) SDL_Log("itemdef:%d: '%s' unknown projectile sound '%s'", line_no, path, tok[6]);
                def->proj_sound = s;
            }
            if (strcmp(tok[7], "-") != 0) SDL_strlcpy(def->proj_model, tok[7], sizeof def->proj_model);

        } else if (strcmp(key, "proj_spread") == 0 || strcmp(key, "proj_drag") == 0 || strcmp(key, "proj_scale") == 0) {
            char *end = NULL;
            float f = (float)SDL_strtod(value, &end);
            if (end == value) { SDL_Log("itemdef:%d: '%s' bad %s", line_no, path, key); continue; }
            if (key[5] == 's' && key[6] == 'p') def->proj_spread = fmaxf(f, 0.0f);
            else if (key[5] == 'd') def->proj_drag = clampf(f, 0.0f, 10.0f);
            else def->proj_scale = fmaxf(f, 0.001f);

        } else if (strcmp(key, "muzzle") == 0 || strcmp(key, "eject") == 0) {
            char *tok[8];
            int n = tokenize(value, tok, 8);
            float f[3];
            if (n != 3 || !parse_floats(tok, 3, f)) { SDL_Log("itemdef:%d: '%s' bad %s (want 3 numbers)", line_no, path, key); continue; }
            if (key[0] == 'm') def->muzzle = v3(f[0], f[1], f[2]); else def->eject = v3(f[0], f[1], f[2]);

        } else if (strcmp(key, "grip") == 0) {
            char *tok[8];
            int n = tokenize(value, tok, 8);
            float f[6];
            if (n != 6 || !parse_floats(tok, 6, f)) { SDL_Log("itemdef:%d: '%s' bad grip (want x y z yaw pitch roll)", line_no, path); continue; }
            def->grip = v3(f[0], f[1], f[2]);
            def->grip_yaw = f[3]; def->grip_pitch = f[4]; def->grip_roll = f[5];

        } else if (strcmp(key, "fire_sound") == 0) {
            if (*value == '\0') { SDL_Log("itemdef:%d: '%s' empty fire_sound name", line_no, path); continue; }
            int fs = audio_sound_from_name(value);
            if (fs < 0) SDL_Log("itemdef:%d: '%s' unknown fire_sound '%s'", line_no, path, value);
            def->fire_sound = fs;

        } else {
            SDL_Log("itemdef:%d: '%s' unknown key '%s'", line_no, path, key);
        }
    }

    free(buf);

    if (!have_model) {
        SDL_Log("itemdef: '%s' has no model, ignoring", path);
        return false;
    }
    return true;
}

int itemdef_get(Items *its, const char *name) {
    // Already loaded (or already tried and failed) this run: reuse the cached slot.
    for (int i = 0; i < its->ndefs; i++)
        if (strcmp(its->defs[i].name, name) == 0)
            return its->defs[i].ok ? i : -1;

    if (its->ndefs >= ITEMDEF_MAX) {
        SDL_Log("itemdef_get: too many item defs loaded (max %d), cannot add '%s'", ITEMDEF_MAX, name);
        return -1;
    }

    // Take the slot up front, even if loading fails below, so a level full of a missing item
    // does not re-open the file every time it is asked for.
    int idx = its->ndefs++;
    ItemDef *def = &its->defs[idx];
    itemdef_defaults(def, name);

    char path[512];
    snprintf(path, sizeof path, "%s/items/%s.txt", HOLLOW_ASSET_DIR, name);
    def->ok = itemdef_parse(def, path);

    return def->ok ? idx : -1;
}
