// Item definitions: assets/items/NAME.txt loading and caching. See items.h.
#include <SDL3/SDL.h>
#include "items.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    def->ok = false;
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
