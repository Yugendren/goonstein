#include "charmodel.h"
#include "voice_dsp.h"   // --- voice --- the `voice` line names an effect
static void sprite_settle(CharModel *cm);
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void spec_defaults(CharSpec *sp) { memset(sp, 0, sizeof *sp); sp->scale = 1; sp->tex_size = 256; sp->voice_pitch = 1; sp->voice_formant = 1; sp->voice_effect = 0; for (int a = 0; a < ANIM_COUNT; a++) sp->anims[a].contact = -1, sp->anims[a].rate = 1; }

bool charmodel_spec_load(CharSpec *sp, const char *config_path) {
    spec_defaults(sp);
    size_t n; char *text = SDL_LoadFile(config_path, &n);
    if (!text) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "character config missing: %s", config_path); return false; }
    char *cur = text; int ln = 0;
    while (*cur) {
        char *line = cur; char *nl = strchr(cur, '\n'); if (nl) { *nl = 0; cur = nl + 1; } else cur += strlen(cur);
        ln++;
        char *hash = strchr(line, '#'); if (hash) *hash = 0;
        char *key = strtok(line, " \t\r"); if (!key) continue;
        if (!strcmp(key, "model")) { char *v = strtok(NULL, " \t\r"); if (v) snprintf(sp->model, sizeof sp->model, "%s", v); }
        else if (!strcmp(key, "sprite")) { char *v = strtok(NULL, " \t\r"); if (v) snprintf(sp->sprite, sizeof sp->sprite, "%s", v); }
        else if (!strcmp(key, "texture_size")) { char *v = strtok(NULL, " \t\r"); if (v) sp->tex_size = atoi(v); }
        else if (!strcmp(key, "scale")) { char *v = strtok(NULL, " \t\r"); if (v) sp->scale = (float)atof(v); }
        else if (!strcmp(key, "voice")) {   // voice PITCH FORMANT EFFECT  (voice chat preset, see src/voice.h)
            char *vp = strtok(NULL, " \t\r"), *vf = strtok(NULL, " \t\r"), *ve = strtok(NULL, " \t\r");
            if (vp) sp->voice_pitch = (float)atof(vp);
            if (vf) sp->voice_formant = (float)atof(vf);
            if (ve) { int e = voice_effect_from_name(ve); sp->voice_effect = e < 0 ? 0 : e; }
            sp->has_voice = true;
        }
        else if (!strcmp(key, "yaw_offset")) { char *v = strtok(NULL, " \t\r"); if (v) sp->yaw_offset_deg = (float)atof(v); }
        else if (!strcmp(key, "hide")) { char *nm; while ((nm = strtok(NULL, " \t\r")) && sp->nhidden < SPEC_MAX_HIDDEN) snprintf(sp->hidden[sp->nhidden++], 48, "%s", nm); }
        else if (!strcmp(key, "recolor")) {   // recolor r g b  r2 g2 b2   (0-255)
            int v[6], k = 0; char *t; while (k < 6 && (t = strtok(NULL, " \t\r"))) v[k++] = atoi(t);
            if (k == 6 && sp->nrecolor < SPEC_MAX_RECOLOR) { for (int c = 0; c < 3; c++) { sp->rc_from[sp->nrecolor][c] = (unsigned char)v[c]; sp->rc_to[sp->nrecolor][c] = (unsigned char)v[3 + c]; } sp->nrecolor++; }
            else SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d bad recolor line", config_path, ln);
        }
        else if (!strcmp(key, "attach")) {   // attach FILE BONE x y z yaw pitch roll scale
            char *file = strtok(NULL, " \t\r"), *bone = strtok(NULL, " \t\r"); float f[7] = { 0, 0, 0, 0, 0, 0, 1 }; int k = 0; char *t;
            while (k < 7 && (t = strtok(NULL, " \t\r"))) f[k++] = (float)atof(t);
            if (file && bone && sp->nattach < SPEC_MAX_ATTACH) { int i = sp->nattach++; snprintf(sp->attach[i].file, 128, "%s", file); snprintf(sp->attach[i].bone, 48, "%s", bone); sp->attach[i].pos = v3(f[0], f[1], f[2]); sp->attach[i].yaw = f[3]; sp->attach[i].pitch = f[4]; sp->attach[i].roll = f[5]; sp->attach[i].scale = f[6] > 0 ? f[6] : 1; }
            else SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d bad attach line", config_path, ln);
        }
        else if (!strcmp(key, "borrow")) {   // borrow FILE NODE
            char *file = strtok(NULL, " \t\r"), *node = strtok(NULL, " \t\r");
            if (file && node && sp->nborrow < SPEC_MAX_BORROW) { int i = sp->nborrow++; snprintf(sp->borrow[i].file, 128, "%s", file); snprintf(sp->borrow[i].node, 48, "%s", node); }
            else SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d bad borrow line", config_path, ln);
        }
        else if (!strcmp(key, "anim")) {
            char *nm = strtok(NULL, " \t\r"), *clip = strtok(NULL, " \t\r");
            if (!nm || !clip) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d bad anim line", config_path, ln); continue; }
            Anim a = anim_from_name(nm);
            if (strcmp(anim_name(a), nm) != 0) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d unknown anim %s", config_path, ln, nm); continue; }
            snprintf(sp->anims[a].clip, sizeof sp->anims[a].clip, "%s", clip); sp->anims[a].set = true;
            char *k;
            while ((k = strtok(NULL, " \t\r"))) {
                if (!strcmp(k, "loop")) sp->anims[a].loop = true;
                else if (!strcmp(k, "hold")) sp->anims[a].hold = true;
                else if (!strcmp(k, "contact")) { char *v = strtok(NULL, " \t\r"); if (v) sp->anims[a].contact = (float)atof(v); }
                else if (!strcmp(k, "rate")) { char *v = strtok(NULL, " \t\r"); if (v) sp->anims[a].rate = (float)atof(v); }
            }
        }
        else SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s:%d unknown key %s", config_path, ln, key);
    }
    SDL_free(text);
    if (!sp->model[0] && !sp->sprite[0]) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: no model or sprite line", config_path); return false; }
    return true;
}

bool charmodel_spec_save(const CharSpec *sp, const char *config_path) {
    FILE *f = fopen(config_path, "wb");
    if (!f) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "cannot write %s", config_path); return false; }
    fprintf(f, "# Character built in the character builder. model / hide / borrow / recolor / attach / anim lines; see src/charmodel.h\n");
    if (sp->model[0]) fprintf(f, "model %s\n", sp->model); else fprintf(f, "sprite %s\n", sp->sprite);
    fprintf(f, "scale %.3f\ntexture_size %d\nyaw_offset %.1f\n", sp->scale, sp->tex_size, sp->yaw_offset_deg);
    if (sp->has_voice) fprintf(f, "voice %.2f %.2f %s\n", sp->voice_pitch, sp->voice_formant, voice_effect_name(sp->voice_effect));
    if (sp->nhidden) { fprintf(f, "hide"); for (int i = 0; i < sp->nhidden; i++) fprintf(f, " %s", sp->hidden[i]); fprintf(f, "\n"); }
    for (int i = 0; i < sp->nrecolor; i++) fprintf(f, "recolor %d %d %d  %d %d %d\n", sp->rc_from[i][0], sp->rc_from[i][1], sp->rc_from[i][2], sp->rc_to[i][0], sp->rc_to[i][1], sp->rc_to[i][2]);
    for (int i = 0; i < sp->nborrow; i++) fprintf(f, "borrow %s %s\n", sp->borrow[i].file, sp->borrow[i].node);
    for (int i = 0; i < sp->nattach; i++) fprintf(f, "attach %s %s  %.3f %.3f %.3f  %.1f %.1f %.1f  %.3f\n", sp->attach[i].file, sp->attach[i].bone, sp->attach[i].pos.x, sp->attach[i].pos.y, sp->attach[i].pos.z, sp->attach[i].yaw, sp->attach[i].pitch, sp->attach[i].roll, sp->attach[i].scale);
    for (int a = 0; a < ANIM_COUNT; a++) {
        if (!sp->anims[a].set) continue;
        fprintf(f, "anim %-10s %s", anim_name((Anim)a), sp->anims[a].clip);
        if (sp->anims[a].loop) fprintf(f, " loop");
        if (sp->anims[a].hold) fprintf(f, " hold");
        if (sp->anims[a].contact >= 0) fprintf(f, " contact %.2f", sp->anims[a].contact);
        if (sp->anims[a].rate != 1) fprintf(f, " rate %.2f", sp->anims[a].rate);
        fprintf(f, "\n");
    }
    fclose(f);
    return true;
}

bool charmodel_apply(Gfx *g, CharModel *cm, const CharSpec *sp) {
    memset(cm, 0, sizeof *cm);
    cm->spec = *sp;
    cm->scale = sp->scale; cm->yaw_offset = sp->yaw_offset_deg * DEG2RAD;
    cm->player.clip = -1; cm->player.prev = -1; cm->last_anim = ANIM_COUNT; cm->last_move = -1;
    for (int i = 0; i < ANIM_COUNT; i++) cm->bind[i].clip = -1;
    if (sp->model[0]) {
        char path[640]; snprintf(path, sizeof path, "%s/%s", HOLLOW_ASSET_DIR, sp->model);
        if (!model_load(g, &cm->model, path, sp->tex_size)) return false;
    } else {
        char path[640]; snprintf(path, sizeof path, "%s/%s", HOLLOW_ASSET_DIR, sp->sprite);
        if (!sprite_def_load(g, &cm->sdef, path)) return false;
        sprite_actor_init(&cm->sprite, &cm->sdef);
        cm->is_sprite = true;
    }
    cm->loaded = true;
    cm->nsub = 0;
    for (int i = 0; i < sp->nattach && i < SPEC_MAX_ATTACH; i++) {
        const char *file = sp->attach[i].file; size_t L = strlen(file);
        PartDoc doc; Piece single = { .size = v3(1, 1, 1), .tint = v4(1, 1, 1, 1), .tex = -1, .tex_tile = 1 }; const Piece *pieces = &single; int np = 1;
        if (L > 5 && !strcmp(file + L - 5, ".part")) { char pp[640]; snprintf(pp, sizeof pp, "%s/%s", HOLLOW_ASSET_DIR, file); if (part_load(&doc, pp)) { pieces = doc.pieces; np = doc.n; } else np = 0; }
        else snprintf(single.file, sizeof single.file, "%s", file);
        for (int k = 0; k < np && cm->nsub < 32; k++) {
            char path[640]; snprintf(path, sizeof path, "%s/%s", HOLLOW_ASSET_DIR, pieces[k].file);
            if (!model_load(g, &cm->sub[cm->nsub].model, path, 256)) continue;
            AnimPlayer rest = { .clip = -1, .prev = -1 }; model_pose(&cm->sub[cm->nsub].model, &rest, &cm->sub[cm->nsub].rest);
            cm->sub[cm->nsub].local = piece_matrix(&pieces[k]); cm->sub[cm->nsub].tint = pieces[k].tint; cm->sub[cm->nsub].attach = i;
            cm->nsub++;
        }
    }
    // borrowed parts: load every distinct source file once, then check the node is there and skinned
    cm->nlent = 0;
    for (int i = 0; i < SPEC_MAX_BORROW; i++) cm->borrow_lent[i] = -1;
    for (int i = 0; i < sp->nborrow && i < SPEC_MAX_BORROW; i++) {
        if (cm->is_sprite) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "character: borrow needs a model, not a sprite"); break; }
        int e = -1;
        for (int k = 0; k < cm->nlent; k++) if (!strcmp(cm->lent[k].file, sp->borrow[i].file)) e = k;
        if (e < 0) {
            if (cm->nlent >= CHAR_MAX_BORROW_FILES) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "character: too many borrowed files, %s skipped", sp->borrow[i].file); continue; }
            char path[640]; snprintf(path, sizeof path, "%s/%s", HOLLOW_ASSET_DIR, sp->borrow[i].file);
            if (!model_load(g, &cm->lent[cm->nlent].model, path, sp->tex_size)) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "character: borrow file failed to load: %s", sp->borrow[i].file); continue; }
            snprintf(cm->lent[cm->nlent].file, 128, "%s", sp->borrow[i].file);
            e = cm->nlent++;
        }
        const Model *lm = &cm->lent[e].model;
        int node = model_find_node(lm, sp->borrow[i].node);
        if (node < 0) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "character: borrow %s: no node %s", sp->borrow[i].file, sp->borrow[i].node); continue; }
        bool mesh = false, skinned = false;
        for (int mi = 0; mi < lm->nmeshes; mi++) if (lm->meshes[mi].node == node) { mesh = true; if (lm->meshes[mi].skinned) skinned = true; }
        if (!mesh) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "character: borrow %s: node %s carries no mesh, skipped", sp->borrow[i].file, sp->borrow[i].node); continue; }
        if (skinned) {
            if (lm->njoints != cm->model.njoints) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "character: borrow %s has %d joints, the body has %d; the part may be distorted", sp->borrow[i].file, lm->njoints, cm->model.njoints);
        } else {   // rigid accessory: it needs the bone it hangs off to exist on the body
            Mat4 local; int host = model_host_node(lm, &cm->model, node, &local);
            if (host < 0) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "character: borrow %s %s hangs off a bone the body has not got, skipped", sp->borrow[i].file, sp->borrow[i].node); continue; }
        }
        cm->borrow_lent[i] = e;
    }
    for (int i = 0; i < sp->nhidden; i++) model_hide_node(&cm->model, sp->hidden[i], true);
    if (!cm->is_sprite && sp->nrecolor) model_recolor(g, &cm->model, sp->rc_from, sp->rc_to, sp->nrecolor);
    for (int a = 0; a < ANIM_COUNT; a++) {
        if (!sp->anims[a].set) continue;
        AnimBinding b = { .clip = cm->is_sprite ? sprite_find_anim(&cm->sdef, sp->anims[a].clip) : model_find_clip(&cm->model, sp->anims[a].clip),
                          .loop = sp->anims[a].loop, .hold = sp->anims[a].hold, .contact = sp->anims[a].contact, .rate = sp->anims[a].rate };
        if (b.clip < 0) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "character: clip %s not found for %s", sp->anims[a].clip, anim_name((Anim)a));
        cm->bind[a] = b;
    }
    return true;
}

bool charmodel_load(Gfx *g, CharModel *cm, const char *config_path) {
    CharSpec sp;
    memset(cm, 0, sizeof *cm);
    if (!charmodel_spec_load(&sp, config_path)) return false;
    return charmodel_apply(g, cm, &sp);
}

// Clip-name matching for the packs we ship. Each action tries its candidates in order.
int charmodel_spec_autobind(CharSpec *sp, const Model *m, int style) {
    static const char *ATK[4][3] = { { "1H_Melee_Attack_Slice_Horizontal", "1H_Melee_Attack_Slice_Diagonal", "1H_Melee_Attack_Chop" },
                                     { "2H_Melee_Attack_Slice", "2H_Melee_Attack_Chop", "2H_Melee_Attack_Stab" },
                                     { "Spellcast_Shoot", "Spellcast_Raise", "Spellcast_Long" },
                                     { "Unarmed_Melee_Attack_Punch_A", "Unarmed_Melee_Attack_Punch_B", "Unarmed_Melee_Attack_Kick" } };
    static const float ATK_CONTACT[4] = { 0.42f, 0.45f, 0.5f, 0.4f };
    struct { Anim a; const char *cands[4]; bool loop, hold; float contact; } T[] = {
        { ANIM_IDLE,      { "Idle", "Idle_A", "Idle_Loop", NULL }, true, false, -1 },
        { ANIM_WALK,      { "Walking_A", "Walking_B", "Walk", NULL }, true, false, -1 },
        { ANIM_RUN,       { "Running_A", "Running_B", "Run", NULL }, true, false, -1 },
        { ANIM_PARRY,     { "Block", "Blocking", NULL, NULL }, false, false, 0.25f },
        { ANIM_PARRY_HIT, { "Block_Hit", "Blocking_Hit", NULL, NULL }, false, false, 0.15f },
        { ANIM_DODGE,     { "Dodge_Backward", "Dodge_Left", "Roll", NULL }, false, false, -1 },
        { ANIM_HURT,      { "Hit_A", "Hit", "HitReact", NULL }, false, false, -1 },
        { ANIM_STAGGER,   { "Hit_B", "Hit_A", NULL, NULL }, false, false, -1 },
        { ANIM_KNEEL,     { "Sit_Floor_Down", "Sit_Chair_Down", "Kneel", NULL }, false, true, -1 },
        { ANIM_DEAD,      { "Death_A", "Death_B", "Death", NULL }, false, true, -1 },
        { ANIM_ROAR,      { "Cheer", "Taunt", "Yes", NULL }, false, false, -1 },
        { ANIM_DOWN,        { "Death01", NULL, NULL, NULL }, false, true, -1 },
        { ANIM_GETUP,       { "LayToIdle", NULL, NULL, NULL }, false, false, -1 },
        { ANIM_KNOCKED,     { "Hit_Knockback", "Hit_B", NULL, NULL }, false, false, -1 },
        { ANIM_GUN_IDLE,    { "Pistol_Idle_Loop", NULL, NULL, NULL }, true, false, -1 },
        { ANIM_GUN_FIRE,    { "Pistol_Shoot", NULL, NULL, NULL }, false, false, -1 },
        { ANIM_GUN_RELOAD,  { "Pistol_Reload", NULL, NULL, NULL }, false, false, -1 },
        { ANIM_MELEE_IDLE,  { "Sword_Idle", NULL, NULL, NULL }, true, false, -1 },
        { ANIM_MELEE_SWING, { "Sword_Attack", NULL, NULL, NULL }, false, false, -1 },
    };
    int bound = 0;
    for (size_t i = 0; i < sizeof T / sizeof *T; i++) {
        for (int c = 0; c < 4 && T[i].cands[c]; c++) if (model_find_clip(m, T[i].cands[c]) >= 0) {
            snprintf(sp->anims[T[i].a].clip, 64, "%s", T[i].cands[c]); sp->anims[T[i].a].set = true; sp->anims[T[i].a].loop = T[i].loop; sp->anims[T[i].a].hold = T[i].hold; sp->anims[T[i].a].contact = T[i].contact; sp->anims[T[i].a].rate = 1;
            bound++; break;
        }
    }
    if (style < 0 || style > 3) style = 0;
    Anim atk[3] = { ANIM_ATTACK, ANIM_ATTACK2, ANIM_ATTACK3 };
    for (int k = 0; k < 3; k++) {
        const char *pick = NULL;
        for (int s2 = 0; s2 < 4 && !pick; s2++) { int st = (style + s2) % 4; if (model_find_clip(m, ATK[st][k]) >= 0) pick = ATK[st][k]; }
        if (!pick) continue;
        snprintf(sp->anims[atk[k]].clip, 64, "%s", pick); sp->anims[atk[k]].set = true; sp->anims[atk[k]].loop = false; sp->anims[atk[k]].hold = false; sp->anims[atk[k]].contact = ATK_CONTACT[style]; sp->anims[atk[k]].rate = 1;
        bound++;
    }
    return bound;
}

void charmodel_destroy(Gfx *g, CharModel *cm) {
    if (cm->loaded && cm->is_sprite) sprite_def_destroy(g, &cm->sdef); else if (cm->loaded) model_destroy(g, &cm->model);
    for (int i = 0; i < cm->nsub; i++) model_destroy(g, &cm->sub[i].model);
    for (int i = 0; i < cm->nlent; i++) model_destroy(g, &cm->lent[i].model);
    cm->nsub = 0; cm->nlent = 0; cm->loaded = false;
}

void charmodel_draw_posed(Gfx *g, const CharModel *cm, const ModelPose *pose, Mat4 world, Vec4 tint) {
    model_draw(g, &cm->model, pose, world, tint);
    // borrowed parts: same joint matrices, each source file's own texture
    for (int e = 0; e < cm->nlent; e++) {
        const char *names[SPEC_MAX_BORROW]; int n = 0;
        for (int i = 0; i < cm->spec.nborrow && i < SPEC_MAX_BORROW; i++) if (cm->borrow_lent[i] == e) names[n++] = cm->spec.borrow[i].node;
        if (n) model_draw_nodes(g, &cm->lent[e].model, &cm->model, pose, world, tint, names, n);
    }
    for (int i = 0; i < cm->nsub; i++) {
        int a = cm->sub[i].attach; if (a < 0 || a >= cm->spec.nattach) continue;
        const CharSpec *sp = &cm->spec;
        int node = model_find_node(&cm->model, sp->attach[a].bone);
        Mat4 bone = node >= 0 ? pose->global[node] : m4_identity();
        Mat4 local = m4_mul(m4_translate(sp->attach[a].pos), m4_mul(m4_rotate_y(sp->attach[a].yaw * DEG2RAD), m4_mul(m4_rotate_x(sp->attach[a].pitch * DEG2RAD), m4_mul(m4_rotate_z(sp->attach[a].roll * DEG2RAD), m4_scale(v3(sp->attach[a].scale, sp->attach[a].scale, sp->attach[a].scale))))));
        Vec4 t = v4(tint.x * cm->sub[i].tint.x, tint.y * cm->sub[i].tint.y, tint.z * cm->sub[i].tint.z, tint.w);
        Mat4 M = m4_mul(world, m4_mul(bone, m4_mul(local, cm->sub[i].local)));
        model_draw(g, &cm->sub[i].model, &cm->sub[i].rest, M, t);
    }
}

// Did the character start a new animation since we last looked?
static bool anim_changed(CharModel *cm, const Character *c) {
    bool changed = c->anim != cm->last_anim || c->anim_t < cm->last_anim_t - 1e-4f;
    cm->last_anim = c->anim; cm->last_anim_t = c->anim_t;
    return changed;
}

// Clips the packs ship for actions a character file has not bound. The file always wins; this is
// only so a character written before deflect / block / sprint existed still plays them. Falling
// through to `alt` means "use whatever that action plays instead".
static const struct { Anim a; const char *clips[3]; float contact; bool loop, hold; Anim alt; } CLIP_DEFAULTS[] = {
    { ANIM_SPRINT,     { "Sprint_Loop", "Running_B", NULL },       -1,    true,  false, ANIM_RUN },
    { ANIM_BLOCK,      { "Sword_Block", "Block", "Blocking" },      0.30f, false, true,  ANIM_PARRY },
    { ANIM_HURT_HEAD,  { "Hit_Head", "Hit_B", NULL },              -1,    false, false, ANIM_HURT },
    { ANIM_HURT_HEAVY, { "Hit_Knockback", "Hit_B", NULL },         -1,    false, false, ANIM_HURT },
    { ANIM_ATTACK_RUN, { "Sword_Dash", "Shield_Dash", NULL },       0.20f, false, false, ANIM_ATTACK },
    { ANIM_DOWN,        { "Death01", NULL, NULL },                  -1,    false, true,  ANIM_KNEEL },
    { ANIM_GETUP,       { "LayToIdle", NULL, NULL },                -1,    false, false, ANIM_IDLE },
    { ANIM_KNOCKED,     { "Hit_Knockback", "Hit_B", NULL },         -1,    false, false, ANIM_HURT_HEAVY },
    { ANIM_GUN_IDLE,    { "Pistol_Idle_Loop", NULL, NULL },         -1,    true,  false, ANIM_IDLE },
    { ANIM_GUN_FIRE,    { "Pistol_Shoot", NULL, NULL },             -1,    false, false, ANIM_ATTACK },
    { ANIM_GUN_RELOAD,  { "Pistol_Reload", NULL, NULL },            -1,    false, false, ANIM_IDLE },
    { ANIM_MELEE_IDLE,  { "Sword_Idle", NULL, NULL },               -1,    true,  false, ANIM_IDLE },
    { ANIM_MELEE_SWING, { "Sword_Attack", NULL, NULL },             -1,    false, false, ANIM_ATTACK },
};

// The binding to actually play for an action: the character file's, else a default clip found by
// name in the model (looked up once; clip -2 records "searched, nothing there"), else the
// stand-in action, else idle.
static const AnimBinding *resolve_binding(CharModel *cm, Anim a) {
    AnimBinding *b = &cm->bind[a];
    if (b->clip == -1 && !cm->is_sprite) {
        b->clip = -2; b->contact = -1; b->rate = 1;
        for (size_t i = 0; i < sizeof CLIP_DEFAULTS / sizeof *CLIP_DEFAULTS; i++) {
            if (CLIP_DEFAULTS[i].a != a) continue;
            for (int k = 0; k < 3 && CLIP_DEFAULTS[i].clips[k]; k++) {
                int c = model_find_clip(&cm->model, CLIP_DEFAULTS[i].clips[k]);
                if (c < 0) continue;
                b->clip = c; b->contact = CLIP_DEFAULTS[i].contact; b->loop = CLIP_DEFAULTS[i].loop; b->hold = CLIP_DEFAULTS[i].hold;
                break;
            }
            break;
        }
    }
    if (b->clip < 0) {
        for (size_t i = 0; i < sizeof CLIP_DEFAULTS / sizeof *CLIP_DEFAULTS; i++)
            if (CLIP_DEFAULTS[i].a == a) { const AnimBinding *alt = resolve_binding(cm, CLIP_DEFAULTS[i].alt); if (alt->clip >= 0) return alt; }
        return &cm->bind[ANIM_IDLE];
    }
    return b;
}

bool charmodel_clip_timing(CharModel *cm, Anim a, float *contact, float *duration) {
    if (!cm->loaded || cm->is_sprite) return false;
    const AnimBinding *b = resolve_binding(cm, a);
    if (b->clip < 0 || b->clip >= cm->model.nclips || b->contact < 0) return false;
    float dur = cm->model.clips[b->clip].duration;
    if (contact) *contact = b->contact * dur;
    if (duration) *duration = dur;
    return true;
}

// Ground speed a locomotion clip was authored for, in m/s: the cycle is rate-matched to it so the
// feet keep up with the ground instead of skating. Unknown clips fall back to the character's own top speed.
static float clip_ground_speed(const char *name, float fallback) {
    static const struct { const char *name; float mps; } T[] = {
        { "Walk_Loop", 1.5f }, { "Walking_A", 1.5f }, { "Walking_B", 1.6f }, { "Walk", 1.5f }, { "Walk_Carry_Loop", 1.4f },
        { "Jog_Fwd_Loop", 3.6f }, { "Running_A", 3.8f }, { "Running_B", 4.2f }, { "Run", 3.8f }, { "Sprint_Loop", 5.6f },
    };
    for (size_t i = 0; i < sizeof T / sizeof *T; i++) if (!strcmp(name, T[i].name)) return T[i].mps;
    return fallback;
}

// Speed-driven locomotion: blend the two gaits that bracket the current speed, phase-synced, with
// the cycle rate-matched to the ground. Standing still drops to a plain idle loop.
static void drive_locomotion(CharModel *cm, const Character *c, float top_speed, float fade) {
    const Model *m = &cm->model;
    int idle = resolve_binding(cm, ANIM_IDLE)->clip;
    static const Anim GAITS[3] = { ANIM_WALK, ANIM_RUN, ANIM_SPRINT };
    int gait[3]; float ref[3]; int n = 0;
    for (int i = 0; i < 3; i++) {
        int clip = i == 2 ? resolve_binding(cm, ANIM_SPRINT)->clip : cm->bind[GAITS[i]].clip;
        if (clip < 0 || clip >= m->nclips || m->clips[clip].duration <= 0.01f) continue;
        if (n && gait[n - 1] == clip) continue;                  // the file bound two gaits to one clip
        gait[n] = clip; ref[n] = clip_ground_speed(m->clips[clip].name, top_speed); n++;
    }
    if (!n || c->speed < 0.12f) {                                 // standing (or nothing to walk with)
        if (idle >= 0 && !(cm->player.clip == idle && cm->player.blend < 0)) anim_play(&cm->player, m, idle, 1, true, false, fade);
        return;
    }
    int i = 0;
    while (i + 1 < n && c->speed > ref[i + 1]) i++;
    float dur = m->clips[gait[i]].duration;
    if (i + 1 < n && c->speed > ref[i]) {
        float k = clampf((c->speed - ref[i]) / fmaxf(ref[i + 1] - ref[i], 0.01f), 0, 1);
        anim_play_blend(&cm->player, m, gait[i], gait[i + 1], k, lerpf(dur, m->clips[gait[i + 1]].duration, k), fade);
    } else {   // slower than the slowest gait (idle mixed underneath) or faster than the fastest
        float cycle = clampf(dur * ref[i] / c->speed, dur * 0.62f, dur * 1.9f);
        anim_play_blend(&cm->player, m, gait[i], i == 0 ? idle : -1, i == 0 ? clampf(1.0f - c->speed / ref[0], 0, 0.85f) : 0, cycle, fade);
    }
}

static void play_binding(CharModel *cm, Anim a, float lead, float tail, float fade) {
    const AnimBinding *b = resolve_binding(cm, a);
    if (b->clip < 0) return;
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
    bool changed = anim_changed(cm, &p->c);
    Anim a = p->c.anim;
    bool loco = (a == ANIM_IDLE || a == ANIM_WALK || a == ANIM_RUN) && (p->state == PS_FREE || p->state == PS_SCRIPTED);
    if (loco && !cm->is_sprite) drive_locomotion(cm, &p->c, d->speed * d->sprint_mult, 0.12f);
    else if (changed) {
        switch (a) {
        case ANIM_ATTACK: case ANIM_ATTACK2: case ANIM_ATTACK3: case ANIM_ATTACK_RUN:
            play_binding(cm, a, p->atk_lead > 0.001f ? p->atk_lead : d->attack_windup,
                         p->atk_active + p->atk_recovery > 0.001f ? p->atk_active + p->atk_recovery : d->attack_active + d->attack_recovery, 0.06f); break;
        case ANIM_PARRY:     play_binding(cm, ANIM_PARRY, d->parry_window * 0.6f, d->parry_window + d->parry_recovery, 0.04f); break;
        case ANIM_BLOCK:     play_binding(cm, ANIM_BLOCK, 0.12f, 6.0f, 0.10f); break;   // raise the guard, then hold it
        case ANIM_PARRY_HIT: play_binding(cm, ANIM_PARRY_HIT, 0.04f, d->parry_recovery, 0.0f); break;
        case ANIM_DODGE:     play_binding(cm, ANIM_DODGE, 0, d->dodge_time, 0.05f); break;
        case ANIM_HURT: case ANIM_HURT_HEAD: play_binding(cm, a, 0, d->hurt_time, 0.0f); break;
        case ANIM_HURT_HEAVY: play_binding(cm, a, 0, d->hurt_time * 1.3f, 0.0f); break;
        case ANIM_STAGGER:   play_binding(cm, ANIM_STAGGER, 0, d->stagger_time, 0.05f); break;
        default:             play_binding(cm, a, 0, 0, 0.12f); break;
        }
    }
    if (cm->is_sprite) { sprite_update(&cm->sprite, dt); sprite_settle(cm); } else anim_update(&cm->player, &cm->model, dt);
}

void charmodel_drive_simple(CharModel *cm, const Character *c, float dt) {
    if (!cm->loaded) return;
    if (anim_changed(cm, c) || (cm->is_sprite ? cm->sprite.anim < 0 : cm->player.clip < 0)) play_binding(cm, c->anim, 0, 0, 0.12f);
    if (cm->is_sprite) { sprite_update(&cm->sprite, dt); sprite_settle(cm); } else anim_update(&cm->player, &cm->model, dt);
}

void charmodel_drive_boss(CharModel *cm, const Boss *b, float dt) {
    if (!cm->loaded) return;
    const BossDef *d = &b->def;
    bool changed = anim_changed(cm, &b->c);
    if ((b->c.anim == ANIM_IDLE || b->c.anim == ANIM_WALK || b->c.anim == ANIM_RUN) && !cm->is_sprite &&
        (b->state == BS_IDLE || b->state == BS_APPROACH)) {
        drive_locomotion(cm, &b->c, d->speed * 1.25f, 0.15f);
        anim_update(&cm->player, &cm->model, dt);
        return;
    }
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

// A character's world transform: position, yaw (its own plus the model's yaw_offset so it faces
// the way its clips were authored for) and scale. Shared by charmodel_draw and charmodel_bone_world
// so the two can never drift apart.
static Mat4 char_world(const CharModel *cm, const Character *c) {
    return m4_trs(c->pos, c->yaw + cm->yaw_offset, v3(cm->scale, cm->scale, cm->scale));
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
    Mat4 world = char_world(cm, c);
    charmodel_draw_posed(g, cm, &cm->pose, world, tint);
}

bool charmodel_bone_world(const CharModel *cm, const Character *c, const char *bone, Mat4 *out) {
    if (!cm->loaded || cm->is_sprite) return false;
    int node = model_find_node(&cm->model, bone);
    if (node < 0) return false;
    *out = m4_mul(char_world(cm, c), cm->pose.global[node]);
    return true;
}


