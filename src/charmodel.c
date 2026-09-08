#include "charmodel.h"
static void sprite_settle(CharModel *cm);
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
        } else if (!strcmp(key, "sprite")) {
            char path[512]; snprintf(path, sizeof path, "%s/%s", HOLLOW_ASSET_DIR, strtok(NULL, " \t"));
            if (!sprite_def_load(g, &cm->sdef, path)) { SDL_free(text); return false; }
            sprite_actor_init(&cm->sprite, &cm->sdef);
            cm->is_sprite = true; cm->loaded = true;
        }
    }
    if (!cm->loaded) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: no model or sprite line", config_path); SDL_free(text); return false; }
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
            AnimBinding b = { .clip = cm->is_sprite ? sprite_find_anim(&cm->sdef, clip) : model_find_clip(&cm->model, clip), .contact = -1, .rate = 1 };
            if (b.clip < 0) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d clip %s not found", config_path, ln, clip);
            char *k;
            while ((k = strtok(NULL, " \t"))) {
                if (!strcmp(k, "loop")) b.loop = true;
                else if (!strcmp(k, "hold")) b.hold = true;
                else if (!strcmp(k, "contact")) b.contact = (float)atof(strtok(NULL, " \t"));
                else if (!strcmp(k, "rate")) b.rate = (float)atof(strtok(NULL, " \t"));
            }
            cm->bind[a] = b;
        }
        else if (strcmp(key, "model") && strcmp(key, "sprite") && strcmp(key, "scale") && strcmp(key, "texture_size") && strcmp(key, "yaw_offset"))
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d unknown key %s", config_path, ln, key);
    }
    SDL_free(text);
    return true;
}

void charmodel_destroy(Gfx *g, CharModel *cm) { if (cm->loaded && cm->is_sprite) sprite_def_destroy(g, &cm->sdef); else if (cm->loaded) model_destroy(g, &cm->model); cm->loaded = false; }

// Did the character start a new animation since we last looked?
static bool anim_changed(CharModel *cm, const Character *c) {
    bool changed = c->anim != cm->last_anim || c->anim_t < cm->last_anim_t - 1e-4f;
    cm->last_anim = c->anim; cm->last_anim_t = c->anim_t;
    return changed;
}

static void play_binding(CharModel *cm, Anim a, float lead, float tail, float fade) {
    const AnimBinding *b = &cm->bind[a];
    if (b->clip < 0) { b = &cm->bind[ANIM_IDLE]; if (b->clip < 0) return; }
    if (cm->is_sprite) {
        const SpriteAnim *sa = &cm->sdef.anims[b->clip];
        if (sa->ncontact > 0 && lead > 0) sprite_play_fitted(&cm->sprite, b->clip, lead);
        else if (!sa->loop && tail > 0) { float nat = sprite_anim_duration(&cm->sdef, b->clip, 1); sprite_play(&cm->sprite, b->clip, nat > 0.01f ? nat / (lead + tail) : 1, true); }
        else sprite_play(&cm->sprite, b->clip, b->rate, false);
        return;
    }
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
        case ANIM_ATTACK: case ANIM_ATTACK2: case ANIM_ATTACK3:
            play_binding(cm, p->c.anim, d->attack_windup, d->attack_active + d->attack_recovery, 0.05f); break;
        case ANIM_PARRY:     play_binding(cm, ANIM_PARRY, d->parry_window * 0.5f, d->parry_window * 0.5f + d->parry_recovery, 0.03f); break;
        case ANIM_PARRY_HIT: play_binding(cm, ANIM_PARRY_HIT, 0.04f, d->parry_recovery, 0.0f); break;
        case ANIM_DODGE:     play_binding(cm, ANIM_DODGE, 0, d->dodge_time, 0.05f); break;
        case ANIM_HURT:      play_binding(cm, ANIM_HURT, 0, d->hurt_time, 0.0f); break;
        default:             play_binding(cm, p->c.anim, 0, 0, 0.12f); break;
        }
    }
    if (cm->is_sprite) { sprite_update(&cm->sprite, dt); sprite_settle(cm); } else anim_update(&cm->player, &cm->model, dt);
}

void charmodel_drive_boss(CharModel *cm, const Boss *b, float dt) {
    if (!cm->loaded) return;
    const BossDef *d = &b->def;
    bool changed = anim_changed(cm, &b->c);
    if (changed) {
        switch (b->c.anim) {
        case ANIM_WINDUP: {
            const BossMove *m = &d->moves[b->move];
            if (cm->is_sprite) { float windup = m->windup * (b->phase2 ? d->phase2_windup_mult : 1.0f); charmodel_sprite_play(cm, m->clip[0] ? m->clip : "attack", windup, true); break; }
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
    if (cm->is_sprite) sprite_update(&cm->sprite, dt); else anim_update(&cm->player, &cm->model, dt);
}

// One-shot sprite animations return to idle when they finish, unless they are meant to hold.
static void sprite_settle(CharModel *cm) {
    if (!cm->is_sprite || cm->sprite.anim < 0 || !cm->sprite.finished) return;
    const SpriteAnim *an = &cm->sdef.anims[cm->sprite.anim];
    if (an->loop) return;
    if (!strcmp(an->name, "dead") || !strcmp(an->name, "kneel")) return;
    int idle = cm->bind[ANIM_IDLE].clip;
    if (idle >= 0) sprite_play(&cm->sprite, idle, 1, true);
}

void charmodel_sprite_play(CharModel *cm, const char *anim, float lead, bool restart) {
    if (!cm->is_sprite) return;
    int a = sprite_find_anim(&cm->sdef, anim);
    if (a < 0) { a = cm->bind[ANIM_IDLE].clip; if (a < 0) return; }
    if (lead > 0 && cm->sdef.anims[a].ncontact > 0) sprite_play_fitted(&cm->sprite, a, lead);
    else sprite_play(&cm->sprite, a, 1, restart);
}

void charmodel_sprite_settle(CharModel *cm) { sprite_settle(cm); }

float charmodel_sprite_contact(const CharModel *cm, const char *anim, int i, float lead) {
    if (!cm->is_sprite) return lead + i * 0.25f;
    int a = sprite_find_anim(&cm->sdef, anim);
    if (a < 0) return lead + i * 0.25f;
    float nat = sprite_contact_time(&cm->sdef, a, 0, 1.0f);
    float rate = (lead > 0.01f && nat > 0.01f) ? nat / lead : 1.0f;
    return sprite_contact_time(&cm->sdef, a, i, rate);
}

void charmodel_draw(Gfx *g, CharModel *cm, const Character *c, Vec4 tint) {
    if (!cm->loaded) return;
    if (cm->is_sprite) {
        Vec3 fwd = v3_cross(v3(0, 1, 0), g->cam_right);   // camera forward on the ground plane
        Vec3 face = v3(sinf(c->yaw + cm->yaw_offset), 0, cosf(c->yaw + cm->yaw_offset));
        sprite_set_facing(&cm->sprite, face, fwd, g->cam_right);
        sprite_actor_draw(g, &cm->sprite, c->pos, tint, cm->scale);
        return;
    }
    model_pose(&cm->model, &cm->player, &cm->pose);
    Mat4 world = m4_trs(c->pos, c->yaw + cm->yaw_offset, v3(cm->scale, cm->scale, cm->scale));
    model_draw(g, &cm->model, &cm->pose, world, tint);
}

static void bind_by_names(CharModel *cm) {
    // Standard names, with the same fallbacks the editor writes into character files
    static const struct { Anim a; const char *names[3]; } T[] = {
        { ANIM_IDLE, {"idle", NULL} }, { ANIM_WALK, {"walk", NULL} }, { ANIM_RUN, {"run", "walk", NULL} },
        { ANIM_ATTACK, {"attack", NULL} }, { ANIM_ATTACK2, {"attack2", "attack", NULL} }, { ANIM_ATTACK3, {"attack3", "attack", NULL} },
        { ANIM_PARRY, {"parry", "hit", NULL} }, { ANIM_PARRY_HIT, {"parry_hit", "hit", NULL} }, { ANIM_DODGE, {"dodge", "roll", NULL} },
        { ANIM_HURT, {"hurt", "hit", NULL} }, { ANIM_KNEEL, {"kneel", "dead", NULL} }, { ANIM_DEAD, {"dead", NULL} },
        { ANIM_ROAR, {"roar", "cheer", NULL} }, { ANIM_STAGGER, {"stagger", "hit", NULL} }, { ANIM_WINDUP, {"windup", "attack", NULL} }, { ANIM_STRIKE, {"strike", "attack", NULL} } };
    for (int i = 0; i < ANIM_COUNT; i++) cm->bind[i].clip = -1;
    for (size_t t = 0; t < sizeof T / sizeof *T; t++) {
        AnimBinding b = { .clip = -1, .contact = -1, .rate = 1 };
        for (int k = 0; k < 3 && T[t].names[k]; k++) { int a = sprite_find_anim(&cm->sdef, T[t].names[k]); if (a >= 0) { b.clip = a; break; } }
        if (b.clip >= 0) { const SpriteAnim *sa = &cm->sdef.anims[b.clip]; b.loop = sa->loop; b.hold = !sa->loop; if (sa->ncontact > 0) b.contact = 0; }
        cm->bind[T[t].a] = b;
    }
}

void charmodel_refresh_from_doc(CharModel *cm, Gfx *g, const PixDoc *doc) {
    if (cm->loaded && cm->is_sprite) sprite_def_destroy(g, &cm->sdef); else if (cm->loaded) model_destroy(g, &cm->model);
    SpriteDef *d = &cm->sdef;
    memset(d, 0, sizeof *d);
    d->size = doc->size; d->frame_w = doc->nanims ? doc->anims[0].fw : 32; d->frame_h = doc->nanims ? doc->anims[0].fh : 32;
    for (int i = 0; i < doc->nanims && d->nsheets < SPRITE_MAX_SHEETS && d->nanims < SPRITE_MAX_ANIMS; i++) {
        const PixAnim *a = &doc->anims[i];
        int w, h; uint8_t *px = pix_compose_sheet(a, &w, &h);
        if (!px) continue;
        SpriteSheet *sh = &d->sheets[d->nsheets];
        memset(sh, 0, sizeof *sh);
        snprintf(sh->name, sizeof sh->name, "%s", a->name);
        sh->tex = gfx_texture_create(g, px, w, h); free(px);
        sh->cols = a->ndirs; sh->rows = a->nframes; sh->fw = a->fw; sh->fh = a->fh;
        SpriteAnim *an = &d->anims[d->nanims];
        memset(an, 0, sizeof *an);
        snprintf(an->name, sizeof an->name, "%s", a->name);
        an->sheet = d->nsheets; an->sheet_right = -1; an->fps = a->fps; an->loop = a->loop; an->directional = true; an->dir_cols = true; an->row = -1; an->first = 0; an->last = a->nframes - 1;
        for (int c = 0; c < a->ncontact && an->ncontact < SPRITE_MAX_CONTACT; c++) an->contact[an->ncontact++] = a->contact[c];
        d->nsheets++; d->nanims++;
    }
    int keep_anim = cm->sprite.anim; float keep_t = cm->sprite.time; Facing keep_f = cm->sprite.facing;
    sprite_actor_init(&cm->sprite, d);
    cm->is_sprite = true; cm->loaded = d->nanims > 0; cm->scale = cm->scale > 0 ? cm->scale : 1;
    bind_by_names(cm);
    if (keep_anim >= 0 && keep_anim < d->nanims) { cm->sprite.anim = keep_anim; cm->sprite.time = keep_t; }
    else if (cm->bind[ANIM_IDLE].clip >= 0) sprite_play(&cm->sprite, cm->bind[ANIM_IDLE].clip, 1, true);
    cm->sprite.facing = keep_f;
    cm->last_anim = ANIM_COUNT;
}
