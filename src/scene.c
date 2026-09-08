// Cutscene playback: parses assets/scenes/*.txt (see README.md) into a sorted list of
// timestamped commands and fires them against a SceneHost as time advances.
#include "scene.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <stdlib.h>

// ---- tokenizing -----------------------------------------------------------
// Splits a line into whitespace-separated tokens, treating a "..." run as one token
// (with the quotes stripped) so `say` text can contain spaces.

#define TOK_MAX 16
#define TOK_LEN 200

static int tokenize(char *line, char toks[TOK_MAX][TOK_LEN]) {
    int n = 0;
    char *p = line;
    while (*p && n < TOK_MAX) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        if (*p == '#') break; // trailing comment
        char *out = toks[n];
        int len = 0;
        if (*p == '"') {
            p++;
            while (*p && *p != '"' && len < TOK_LEN - 1) { out[len++] = *p++; }
            if (*p == '"') p++;
        } else {
            while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && len < TOK_LEN - 1) {
                out[len++] = *p++;
            }
        }
        out[len] = 0;
        n++;
    }
    return n;
}

static void set_actor(SceneCmd *c, const char *s) {
    SDL_strlcpy(c->actor, s, sizeof c->actor);
}
static void set_text(SceneCmd *c, const char *s) {
    SDL_strlcpy(c->text, s, sizeof c->text);
}

// ---- load -------------------------------------------------------------------

bool scene_load(Scene *sc, const char *path) {
    memset(sc, 0, sizeof *sc);

    size_t size = 0;
    void *data = SDL_LoadFile(path, &size);
    if (!data) {
        SDL_Log("scene_load: could not read '%s': %s", path, SDL_GetError());
        return false;
    }

    // Work on a mutable, NUL-terminated copy so we can split it into lines in place.
    char *text = (char *)SDL_malloc(size + 1);
    if (!text) {
        SDL_free(data);
        SDL_Log("scene_load: out of memory reading '%s'", path);
        return false;
    }
    memcpy(text, data, size);
    text[size] = 0;
    SDL_free(data);

    int line_no = 0;
    char *save = NULL;
    char *line = SDL_strtok_r(text, "\n", &save);
    while (line) {
        line_no++;
        char toks[TOK_MAX][TOK_LEN];
        int nt = tokenize(line, toks);
        line = SDL_strtok_r(NULL, "\n", &save);
        if (nt == 0) continue; // blank or comment-only

        if (sc->n >= SCENE_MAX_CMDS) {
            SDL_Log("scene_load: '%s' line %d: too many commands (max %d), skipping rest",
                    path, line_no, SCENE_MAX_CMDS);
            break;
        }

        float t = (float)SDL_atof(toks[0]);
        if (nt < 2) {
            SDL_Log("scene_load: '%s' line %d: missing command after time", path, line_no);
            continue;
        }
        const char *cmd = toks[1];
        SceneCmd c;
        memset(&c, 0, sizeof c);
        c.t = t;
        c.fov = 60.0f;

        if (strcmp(cmd, "cam") == 0) {
            // T cam ex ey ez  tx ty tz  fov  [cut]
            if (nt < 9) { SDL_Log("scene_load: '%s' line %d: cam needs 8 fields", path, line_no); continue; }
            c.type = SC_CAM;
            c.pos = v3((float)SDL_atof(toks[2]), (float)SDL_atof(toks[3]), (float)SDL_atof(toks[4]));
            c.target = v3((float)SDL_atof(toks[5]), (float)SDL_atof(toks[6]), (float)SDL_atof(toks[7]));
            c.fov = (float)SDL_atof(toks[8]);
            c.a = (nt >= 10 && strcmp(toks[9], "cut") == 0) ? 1.0f : 0.0f;
        } else if (strcmp(cmd, "say") == 0) {
            // T say "text" dur [speaker]
            if (nt < 4) { SDL_Log("scene_load: '%s' line %d: say needs text and dur", path, line_no); continue; }
            c.type = SC_SAY;
            set_text(&c, toks[2]);
            c.dur = (float)SDL_atof(toks[3]);
            if (nt >= 5) set_actor(&c, toks[4]);
        } else if (strcmp(cmd, "actor") == 0) {
            // T actor NAME move|face|anim|teleport ...
            if (nt < 4) { SDL_Log("scene_load: '%s' line %d: actor needs a name and sub-command", path, line_no); continue; }
            const char *name = toks[2];
            const char *sub = toks[3];
            if (strcmp(sub, "move") == 0) {
                if (nt < 8) { SDL_Log("scene_load: '%s' line %d: actor move needs x y z dur", path, line_no); continue; }
                c.type = SC_ACTOR_MOVE;
                set_actor(&c, name);
                c.pos = v3((float)SDL_atof(toks[4]), (float)SDL_atof(toks[5]), (float)SDL_atof(toks[6]));
                c.dur = (float)SDL_atof(toks[7]);
            } else if (strcmp(sub, "face") == 0) {
                if (nt < 7) { SDL_Log("scene_load: '%s' line %d: actor face needs x y z", path, line_no); continue; }
                c.type = SC_ACTOR_FACE;
                set_actor(&c, name);
                c.target = v3((float)SDL_atof(toks[4]), (float)SDL_atof(toks[5]), (float)SDL_atof(toks[6]));
            } else if (strcmp(sub, "anim") == 0) {
                if (nt < 5) { SDL_Log("scene_load: '%s' line %d: actor anim needs a name", path, line_no); continue; }
                c.type = SC_ACTOR_ANIM;
                set_actor(&c, name);
                set_text(&c, toks[4]);
            } else if (strcmp(sub, "teleport") == 0) {
                if (nt < 8) { SDL_Log("scene_load: '%s' line %d: actor teleport needs x y z yaw", path, line_no); continue; }
                c.type = SC_ACTOR_TELEPORT;
                set_actor(&c, name);
                c.pos = v3((float)SDL_atof(toks[4]), (float)SDL_atof(toks[5]), (float)SDL_atof(toks[6]));
                c.a = (float)SDL_atof(toks[7]);
            } else {
                SDL_Log("scene_load: '%s' line %d: unknown actor sub-command '%s', skipping", path, line_no, sub);
                continue;
            }
        } else if (strcmp(cmd, "fade") == 0) {
            if (nt < 5) { SDL_Log("scene_load: '%s' line %d: fade needs from to dur", path, line_no); continue; }
            c.type = SC_FADE;
            c.a = (float)SDL_atof(toks[2]);
            c.b = (float)SDL_atof(toks[3]);
            c.dur = (float)SDL_atof(toks[4]);
        } else if (strcmp(cmd, "letterbox") == 0) {
            if (nt < 3) { SDL_Log("scene_load: '%s' line %d: letterbox needs on|off", path, line_no); continue; }
            c.type = SC_LETTERBOX;
            c.a = (strcmp(toks[2], "on") == 0) ? 1.0f : 0.0f;
        } else if (strcmp(cmd, "shake") == 0) {
            if (nt < 4) { SDL_Log("scene_load: '%s' line %d: shake needs amount dur", path, line_no); continue; }
            c.type = SC_SHAKE;
            c.a = (float)SDL_atof(toks[2]);
            c.dur = (float)SDL_atof(toks[3]);
        } else if (strcmp(cmd, "sound") == 0) {
            if (nt < 3) { SDL_Log("scene_load: '%s' line %d: sound needs a name", path, line_no); continue; }
            c.type = SC_SOUND;
            set_text(&c, toks[2]);
        } else if (strcmp(cmd, "end") == 0) {
            c.type = SC_END;
        } else {
            SDL_Log("scene_load: '%s' line %d: unknown command '%s', skipping", path, line_no, cmd);
            continue;
        }

        sc->cmds[sc->n++] = c;
    }

    SDL_free(text);

    // Stable sort by t (insertion sort; small n, keeps original order for ties).
    for (int i = 1; i < sc->n; i++) {
        SceneCmd key = sc->cmds[i];
        int j = i - 1;
        while (j >= 0 && sc->cmds[j].t > key.t) {
            sc->cmds[j + 1] = sc->cmds[j];
            j--;
        }
        sc->cmds[j + 1] = key;
    }

    SDL_strlcpy(sc->path, path, sizeof sc->path);
    return true;
}

// ---- playback ------------------------------------------------------------

void scene_start(Scene *sc) {
    sc->time = 0;
    sc->next = 0;
    sc->playing = true;
    sc->done = false;
    sc->cam_valid = false;
    sc->cam_eye = v3(0, 0, 0);
    sc->cam_target = v3(0, 0, 0);
    sc->cam_fov = 60.0f;
    sc->fade = 1.0f;
    sc->letterbox = 1.0f;
    sc->shake = 0.0f;
    sc->subtitle[0] = 0;
    sc->speaker[0] = 0;
    sc->subtitle_until = 0;
}

// Fade animation state is derived on the fly from the most recent SC_FADE command that
// has started; find it rather than keeping separate playback fields (Scene has none).
static const SceneCmd *last_fade_at_or_before(const Scene *sc, float time) {
    const SceneCmd *found = NULL;
    for (int i = 0; i < sc->n; i++) {
        const SceneCmd *c = &sc->cmds[i];
        if (c->type == SC_FADE && c->t <= time) found = c;
    }
    return found;
}

static const SceneCmd *last_shake_at_or_before(const Scene *sc, float time) {
    const SceneCmd *found = NULL;
    for (int i = 0; i < sc->n; i++) {
        const SceneCmd *c = &sc->cmds[i];
        if (c->type == SC_SHAKE && c->t <= time) found = c;
    }
    return found;
}

// Updates sc->fade toward the active fade command's animated value.
static void update_fade(Scene *sc) {
    const SceneCmd *f = last_fade_at_or_before(sc, sc->time);
    if (!f) return;
    float t = (f->dur > 1e-6f) ? clampf((sc->time - f->t) / f->dur, 0, 1) : 1.0f;
    sc->fade = lerpf(f->a, f->b, t);
}

static void update_shake(Scene *sc) {
    const SceneCmd *s = last_shake_at_or_before(sc, sc->time);
    if (!s) { return; }
    float t = (s->dur > 1e-6f) ? clampf((sc->time - s->t) / s->dur, 0, 1) : 1.0f;
    sc->shake = lerpf(s->a, 0.0f, t);
}

// Finds the camera keys bracketing `time` and writes the interpolated camera into sc.
static void update_camera(Scene *sc) {
    const SceneCmd *a = NULL, *b = NULL;
    for (int i = 0; i < sc->n; i++) {
        SceneCmd *c = &sc->cmds[i];
        if (c->type != SC_CAM) continue;
        if (c->t <= sc->time) {
            a = c;
        } else if (!b) {
            b = c;
            break;
        }
    }
    if (!a) {
        sc->cam_valid = false;
        return;
    }
    sc->cam_valid = true;
    if (!b || b->a != 0.0f) {
        // No next key, or the next key is a hard cut: hold A until B's time arrives
        // (at which point A becomes B on a later update).
        sc->cam_eye = a->pos;
        sc->cam_target = a->target;
        sc->cam_fov = a->fov;
        return;
    }
    float span = b->t - a->t;
    float t = (span > 1e-6f) ? clampf((sc->time - a->t) / span, 0, 1) : 1.0f;
    t = ease_in_out(t);
    sc->cam_eye = v3_lerp(a->pos, b->pos, t);
    sc->cam_target = v3_lerp(a->target, b->target, t);
    sc->cam_fov = lerpf(a->fov, b->fov, t);
}

static void fire(Scene *sc, const SceneCmd *c, const SceneHost *host) {
    switch (c->type) {
    case SC_CAM:
        // Handled continuously by update_camera; nothing to do on fire.
        break;
    case SC_SAY:
        SDL_strlcpy(sc->subtitle, c->text, sizeof sc->subtitle);
        SDL_strlcpy(sc->speaker, c->actor, sizeof sc->speaker);
        sc->subtitle_until = c->t + c->dur;
        break;
    case SC_ACTOR_MOVE:
        if (host && host->actor_move) host->actor_move(host->ud, c->actor, c->pos, c->dur);
        break;
    case SC_ACTOR_FACE:
        if (host && host->actor_face) host->actor_face(host->ud, c->actor, c->target);
        break;
    case SC_ACTOR_ANIM:
        if (host && host->actor_anim) host->actor_anim(host->ud, c->actor, c->text);
        break;
    case SC_ACTOR_TELEPORT:
        if (host && host->actor_teleport) host->actor_teleport(host->ud, c->actor, c->pos, c->a);
        break;
    case SC_FADE:
        // Animated value tracked continuously by update_fade (called right after fire()).
        break;
    case SC_LETTERBOX:
        sc->letterbox = c->a;
        break;
    case SC_SHAKE:
        // Animated value tracked continuously by update_shake (called right after fire()).
        break;
    case SC_SOUND:
        if (host && host->sound) host->sound(host->ud, c->text);
        break;
    case SC_END:
        sc->playing = false;
        sc->done = true;
        break;
    }
}

void scene_update(Scene *sc, float dt, const SceneHost *host) {
    if (!sc->playing) return;
    sc->time += dt;

    while (sc->next < sc->n && sc->cmds[sc->next].t <= sc->time) {
        fire(sc, &sc->cmds[sc->next], host);
        sc->next++;
        if (sc->done) break;
    }

    update_camera(sc);
    update_fade(sc);
    update_shake(sc);

    if (sc->subtitle[0] && sc->time > sc->subtitle_until) {
        sc->subtitle[0] = 0;
        sc->speaker[0] = 0;
    }
}

void scene_skip(Scene *sc, const SceneHost *host) {
    while (sc->next < sc->n) {
        const SceneCmd *c = &sc->cmds[sc->next];
        switch (c->type) {
        case SC_ACTOR_TELEPORT:
            if (host && host->actor_teleport) host->actor_teleport(host->ud, c->actor, c->pos, c->a);
            break;
        case SC_ACTOR_ANIM:
            if (host && host->actor_anim) host->actor_anim(host->ud, c->actor, c->text);
            break;
        case SC_ACTOR_MOVE:
            if (host && host->actor_move) host->actor_move(host->ud, c->actor, c->pos, 0.0f);
            break;
        default:
            break; // sounds, says, camera, fade, letterbox, shake: skipped
        }
        sc->next++;
    }
    sc->time = (sc->n > 0) ? sc->cmds[sc->n - 1].t : sc->time;
    sc->fade = 1.0f;
    sc->letterbox = 0.0f;
    sc->playing = false;
    sc->done = true;
    sc->cam_valid = false;
    sc->subtitle[0] = 0;
    sc->speaker[0] = 0;
}
