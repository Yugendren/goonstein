#include "charmodel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool charmodel_load(Gfx *g, CharModel *cm, const char *config_path) {
    memset(cm, 0, sizeof *cm);
    cm->scale = 1.0f; cm->player.clip = -1; cm->player.prev = -1; cm->last_anim = ANIM_COUNT; cm->last_move = -1;
    for (int i = 0; i < ANIM_COUNT; i++) cm->bind[i].clip = -1;
    size_t n; char *text = SDL_LoadFile(config_path, &n);
    if (!text) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "character config missing: %s", config_path); return false; }
    int tex_size = 256;
    char *cur = text; int ln = 0;
    // First pass: model line must load before anims can resolve, so collect lines.
    char *lines[256]; int nlines = 0;
    while (*cur && nlines < 256) {
        char *line = cur; char *nl = strchr(cur, '\n');
        if (nl) { *nl = 0; cur = nl + 1; } else cur += strlen(cur);
        char *hash = strchr(line, '#'); if (hash) *hash = 0;
        lines[nlines++] = line;
    }
    for (int i = 0; i < nlines; i++) {
        char buf[512]; snprintf(buf, sizeof buf, "%s", lines[i]);
        char *key = strtok(buf, " \t"); if (!key) continue;
        if (!strcmp(key, "texture_size")) tex_size = atoi(strtok(NULL, " \t"));
        else if (!strcmp(key, "scale")) cm->scale = (float)atof(strtok(NULL, " \t"));
        else if (!strcmp(key, "yaw_offset")) cm->yaw_offset = (float)atof(strtok(NULL, " \t")) * DEG2RAD;
    }
    for (int i = 0; i < nlines; i++) {
        char buf[512]; snprintf(buf, sizeof buf, "%s", lines[i]);
        char *key = strtok(buf, " \t"); if (!key) continue;
        if (!strcmp(key, "model")) {
            char path[512]; snprintf(path, sizeof path, "%s/%s", HOLLOW_ASSET_DIR, strtok(NULL, " \t"));
            if (!model_load(g, &cm->model, path, tex_size)) { SDL_free(text); return false; }
            cm->loaded = true;
        }
    }
    if (!cm->loaded) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: no model line", config_path); SDL_free(text); return false; }
    for (int i = 0; i < nlines; i++) {
        ln = i + 1;
        char buf[512]; snprintf(buf, sizeof buf, "%s", lines[i]);
        char *key = strtok(buf, " \t"); if (!key) continue;
        if (!strcmp(key, "hide")) { char *nm; while ((nm = strtok(NULL, " \t"))) model_hide_node(&cm->model, nm, true); }
        else if (!strcmp(key, "anim")) {
            char *nm = strtok(NULL, " \t"), *clip = strtok(NULL, " \t");
            if (!nm || !clip) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d bad anim line", config_path, ln); continue; }
            Anim a = anim_from_name(nm);
            if (strcmp(anim_name(a), nm) != 0) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d unknown anim %s", config_path, ln, nm); continue; }
            AnimBinding b = { .clip = model_find_clip(&cm->model, clip), .contact = -1, .rate = 1 };
            if (b.clip < 0) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d clip %s not in model", config_path, ln, clip);
            char *k;
            while ((k = strtok(NULL, " \t"))) {
                if (!strcmp(k, "loop")) b.loop = true;
                else if (!strcmp(k, "hold")) b.hold = true;
                else if (!strcmp(k, "contact")) b.contact = (float)atof(strtok(NULL, " \t"));
                else if (!strcmp(k, "rate")) b.rate = (float)atof(strtok(NULL, " \t"));
            }
            cm->bind[a] = b;
        }
        else if (strcmp(key, "model") && strcmp(key, "scale") && strcmp(key, "texture_size") && strcmp(key, "yaw_offset"))
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d unknown key %s", config_path, ln, key);
    }
    SDL_free(text);
    return true;
}

void charmodel_destroy(Gfx *g, CharModel *cm) { if (cm->loaded) model_destroy(g, &cm->model); cm->loaded = false; }

// Did the character start a new animation since we last looked?
static bool anim_changed(CharModel *cm, const Character *c) {
    bool changed = c->anim != cm->last_anim || c->anim_t < cm->last_anim_t - 1e-4f;
    cm->last_anim = c->anim; cm->last_anim_t = c->anim_t;
    return changed;
}

static void play_binding(CharModel *cm, Anim a, float lead, float tail, float fade) {
    const AnimBinding *b = &cm->bind[a];
    if (b->clip < 0) { b = &cm->bind[ANIM_IDLE]; if (b->clip < 0) return; }
    const AnimClip *clip = &cm->model.clips[b->clip];
    if (b->contact >= 0 && (lead > 0 || tail > 0))
        anim_play_fitted(&cm->player, &cm->model, b->clip, b->contact * clip->duration, lead, tail, fade);
    else if (!b->loop && !b->hold && tail > 0)
        anim_play_fitted(&cm->player, &cm->model, b->clip, 0, 0, lead + tail, fade);   // stretch whole clip over the state
    else
        anim_play(&cm->player, &cm->model, b->clip, b->rate, b->loop, b->hold || !b->loop, fade);
}

void charmodel_drive_player(CharModel *cm, const Player *p, float dt) {
    if (!cm->loaded) return;
    const PlayerDef *d = &p->def;
    if (anim_changed(cm, &p->c)) {
        switch (p->c.anim) {
        case ANIM_ATTACK:    play_binding(cm, ANIM_ATTACK, d->attack_windup, d->attack_active + d->attack_recovery, 0.05f); break;
        case ANIM_PARRY:     play_binding(cm, ANIM_PARRY, d->parry_window * 0.5f, d->parry_window * 0.5f + d->parry_recovery, 0.03f); break;
        case ANIM_PARRY_HIT: play_binding(cm, ANIM_PARRY_HIT, 0.04f, d->parry_recovery, 0.0f); break;
        case ANIM_DODGE:     play_binding(cm, ANIM_DODGE, 0, d->dodge_time, 0.05f); break;
        case ANIM_HURT:      play_binding(cm, ANIM_HURT, 0, d->hurt_time, 0.0f); break;
        default:             play_binding(cm, p->c.anim, 0, 0, 0.12f); break;
        }
    }
    anim_update(&cm->player, &cm->model, dt);
}

void charmodel_drive_boss(CharModel *cm, const Boss *b, float dt) {
    if (!cm->loaded) return;
    const BossDef *d = &b->def;
    bool changed = anim_changed(cm, &b->c);
    if (changed) {
        switch (b->c.anim) {
        case ANIM_WINDUP: {
            const BossMove *m = &d->moves[b->move];
            int clip = m->clip[0] ? model_find_clip(&cm->model, m->clip) : -1;
            float windup = m->windup * (b->phase2 ? d->phase2_windup_mult : 1.0f);
            if (clip >= 0) anim_play_fitted(&cm->player, &cm->model, clip, m->contact * cm->model.clips[clip].duration, windup, m->active + m->recovery, 0.08f);
            else play_binding(cm, ANIM_ATTACK, windup, m->active + m->recovery, 0.08f);
        } break;
        case ANIM_STRIKE: break;   // the windup clip keeps playing through the strike and recovery
        case ANIM_STAGGER:   play_binding(cm, ANIM_STAGGER, 0, d->stagger_time, 0.05f); break;
        case ANIM_HURT:      play_binding(cm, ANIM_HURT, 0, 0.4f, 0.0f); break;
        default:             play_binding(cm, b->c.anim, 0, 0, 0.15f); break;
        }
    }
    anim_update(&cm->player, &cm->model, dt);
}

void charmodel_draw(Gfx *g, CharModel *cm, const Character *c, Vec4 tint) {
    if (!cm->loaded) return;
    model_pose(&cm->model, &cm->player, &cm->pose);
    Mat4 world = m4_trs(c->pos, c->yaw + cm->yaw_offset, v3(cm->scale, cm->scale, cm->scale));
    model_draw(g, &cm->model, &cm->pose, world, tint);
}
