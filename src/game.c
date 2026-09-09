#include "game.h"
#include "audio.h"
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSET(rel) (HOLLOW_ASSET_DIR "/" rel)
#define MUSIC(rel) (HOLLOW_ASSET_DIR "/sprites/ninja/Audio/Musics/" rel)

static void say(Game *g, const char *m) { snprintf(g->msg, sizeof g->msg, "%s", m); g->msg_t = 2.5f; }

static const char *GS_NAMES[] = { "EXPLORE", "SCENE", "FIGHT", "DEAD", "END", "BATTLE", "EDITOR" };
static const char *BT_NAMES[] = { "INTRO", "PLAYER", "CARD", "ENEMY_TELL", "ENEMY_ATTACK", "ENEMY_RECOVER", "WIN", "LOSE" };

static void debug_snapshot(Game *g) {
    char h[2048]; size_t n = 0;
    const Battle *b = &g->battle;
    n += (size_t)snprintf(h + n, sizeof h - n, "game %s t=%.2f fps %.0f level %s\n", GS_NAMES[g->state], g->state_t, g->fps, g->level.path);
    n += (size_t)snprintf(h + n, sizeof h - n, "player pos %.1f %.1f %.1f yaw %.0f hp %.0f anim %s | boss hp %.0f anim %s\n", g->player.c.pos.x, g->player.c.pos.y, g->player.c.pos.z, g->player.c.yaw / DEG2RAD, g->player.c.hp, anim_name(g->player.c.anim), g->boss.c.hp, anim_name(g->boss.c.anim));
    if (g->state == GS_BATTLE) {
        n += (size_t)snprintf(h + n, sizeof h - n, "battle %s t=%.2f round %d energy %d/%d banked %d hp %d/%d enemy %d/%d guard %d combo %d\n", BT_NAMES[b->state], b->t, b->round, b->energy, b->energy_max, b->banked, b->player_hp, b->player_hp_max, b->enemy_hp, b->enemy_hp_max, b->guard, b->combo);
        n += (size_t)snprintf(h + n, sizeof h - n, "hand:"); for (int i = 0; i < b->nhand; i++) n += (size_t)snprintf(h + n, sizeof h - n, " %s(p%d)", b->cards[b->hand[i].def].name, b->hand[i].phase);
        n += (size_t)snprintf(h + n, sizeof h - n, "\nhovered %d dragging %d drop_target %d | attack %s hit_t", b->hovered, b->dragging, b->drop_target, b->enemy.attacks[b->cur_attack].name);
        for (int i = 0; i < b->enemy.attacks[b->cur_attack].hits; i++) n += (size_t)snprintf(h + n, sizeof h - n, " %.3f%s", b->hit_t[i], b->hit_done[i] ? "*" : "");
        n += (size_t)snprintf(h + n, sizeof h - n, "\nlast press %.3f used %d last judge %d offset %+.3f\n", b->parry_pressed_t, b->press_used, b->last_judge, b->last_offset);
    }
    float mx, my; platform_mouse_ui(g->pf, INTERNAL_W, INTERNAL_H, &mx, &my);
    n += (size_t)snprintf(h + n, sizeof h - n, "mouse ui %.0f %.0f held %d | hero %s\n", mx, my, g->pf->input.mouse_held, g->hero_config[0] ? g->hero_config : "hero");
    say(g, dbg_snapshot(h) ? "snapshot copied to clipboard and hollow_snapshot.txt" : "snapshot: clipboard or file failed");
}

// ---------------------------------------------------------------- scene host

static Character *actor(Game *g, const char *name) {
    if (!strcmp(name, "player")) return &g->player.c;
    if (!strcmp(name, "boss")) return &g->boss.c;
    return NULL;
}
static void host_move(void *ud, const char *a, Vec3 pos, float dur) { Character *c = actor(ud, a); if (c) character_script_move(c, pos, dur); }
static void host_face(void *ud, const char *a, Vec3 t) { Character *c = actor(ud, a); if (c) c->yaw = atan2f(t.x - c->pos.x, t.z - c->pos.z); }
static void host_anim(void *ud, const char *a, const char *anim) { Character *c = actor(ud, a); if (c) character_set_anim(c, anim_from_name(anim)); }
static void host_teleport(void *ud, const char *a, Vec3 pos, float yaw) { Character *c = actor(ud, a); if (c) { c->pos = pos; c->yaw = yaw * DEG2RAD; c->scripted_moving = false; } }
static void host_sound(void *ud, const char *name) {
    (void)ud;
    static const char *names[SND_COUNT] = { "footstep", "swing", "hit", "parry", "hurt", "stagger", "roar", "death", "blip", "heart", "door", "sting", "whiff", "fail" };
    for (int i = 0; i < SND_COUNT; i++) if (!strcmp(name, names[i])) { audio_play((SoundId)i, 0.9f, 1.0f); return; }
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "scene: unknown sound %s", name);
}
static const SceneHost HOST_TEMPLATE = { NULL, host_move, host_face, host_anim, host_teleport, host_sound };

static void play_scene(Game *g, const char *name, GState after) {
    char path[640]; snprintf(path, sizeof path, "%s/scenes/%s", HOLLOW_ASSET_DIR, name);
    if (!scene_load(&g->scene, path)) { say(g, "scene failed to load"); return; }
    dbg_log("scene %s", name);
    scene_start(&g->scene);
    g->state = GS_SCENE; g->after_scene = after; g->state_t = 0;
    g->player.state = PS_SCRIPTED; character_set_anim(&g->player.c, ANIM_IDLE);
    g->boss.state = BS_SCRIPTED;
}

// ---------------------------------------------------------------- setup and resets

// Characters stand on the terrain surface wherever the level has one.
static void snap_to_terrain(Game *g) {
    if (!g->terrain.present) return;
    Character *cs[2] = { &g->player.c, &g->boss.c };
    for (int i = 0; i < 2; i++) if (terrain_inside(&g->terrain, cs[i]->pos.x, cs[i]->pos.z)) cs[i]->pos.y = terrain_height(&g->terrain, cs[i]->pos.x, cs[i]->pos.z);
}

static void setup_level_content(Game *g) {
    props_load_level(&g->gfx, &g->props, &g->level);
    // terrain follows the level: reload when the level names one, drop it otherwise
    if (g->level.terrain_file[0]) {
        if (!g->terrain.present || strcmp(g->terrain.file, g->level.terrain_file) != 0) {
            terrain_destroy(&g->gfx, &g->terrain);
            if (!terrain_load(&g->terrain, HOLLOW_ASSET_DIR, g->level.terrain_file)) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "terrain %s failed to load", g->level.terrain_file);
        }
    } else if (g->terrain.present && !(g->tool_mode == 2 && g->leveled.open)) { terrain_destroy(&g->gfx, &g->terrain); g->terrain.present = false; }
    if (!g->terrain.present && SDL_getenv("HOLLOW_TERRAIN_DEMO")) {   // headless check of the sculpting path
        terrain_init(&g->terrain, 1.5f, v3(-96, 0, -96), 0, v3(0.20f, 0.34f, 0.16f));
        terrain_generate_mountains(&g->terrain, 28);
        terrain_auto_biome(&g->terrain, 14, 0.45f, v3(0.20f, 0.34f, 0.16f), v3(0.36f, 0.34f, 0.35f), v3(0.88f, 0.90f, 0.95f), v3(0.30f, 0.22f, 0.15f));
        snprintf(g->terrain.file, sizeof g->terrain.file, "%s", "levels/demo_terrain");
        if (!strcmp(SDL_getenv("HOLLOW_TERRAIN_DEMO"), "save")) terrain_save(&g->terrain, HOLLOW_ASSET_DIR);
    }
    snap_to_terrain(g);
    particles_clear(&g->particles);
    for (int i = 0; i < g->level.nemitters; i++) {
        const LevelEmitter *le = &g->level.emitters[i];
        Emitter e = { .type = particle_type_from_name(le->type), .pos = le->pos, .extent = le->extent, .rate = le->rate,
                      .color = le->color, .size = le->size, .life = le->life, .active = true };
        particles_add_emitter(&g->particles, &e);
    }
    particles_prewarm(&g->particles, 8.0f);
}

static void load_portraits(Game *g) {
    size_t n; char *text = SDL_LoadFile(ASSET("portraits.txt"), &n);
    if (!text) return;
    char *cur = text;
    while (*cur) {
        char *line = cur; char *nl = strchr(cur, '\n'); if (nl) { *nl = 0; cur = nl + 1; } else cur += strlen(cur);
        char *hash = strchr(line, '#'); if (hash) *hash = 0;
        // NAME may be quoted to allow spaces: "The Warden" path
        char *p = line; while (*p == ' ' || *p == '\t') p++;
        if (!*p) continue;
        char name[32]; char *path;
        if (*p == '"') { p++; char *q = strchr(p, '"'); if (!q) continue; *q = 0; snprintf(name, sizeof name, "%s", p); path = q + 1; }
        else { char *sp = p; while (*sp && *sp != ' ' && *sp != '\t') sp++; if (!*sp) continue; *sp = 0; snprintf(name, sizeof name, "%s", p); path = sp + 1; }
        while (*path == ' ' || *path == '\t') path++;
        char *end = path + strlen(path); while (end > path && (end[-1] == ' ' || end[-1] == '\r' || end[-1] == '\t')) *--end = 0;
        if (g->nportraits >= 16) break;
        snprintf(g->portraits[g->nportraits].name, 32, "%s", name);
        if (!strncmp(path, "model:", 6)) {   // rendered live from the 3D character: model:hero or model:boss
            g->portraits[g->nportraits].model = !strcmp(path + 6, "boss") ? 2 : 1;
        } else {
            char full[640]; snprintf(full, sizeof full, "%s/%s", HOLLOW_ASSET_DIR, path);
            g->portraits[g->nportraits].tex = gfx_texture_load_exact(&g->gfx, full);
        }
        g->nportraits++;
    }
    SDL_free(text);
    for (int i = 1; i <= 30; i++) { char full[640]; snprintf(full, sizeof full, "%s/sprites/ninja/Ui/Emote/emote%d.png", HOLLOW_ASSET_DIR, i); g->emotes[i] = gfx_texture_load_exact(&g->gfx, full); }
}

static int portrait_model_for(Game *g, const char *speaker) {
    for (int i = 0; i < g->nportraits; i++) if (!strcmp(g->portraits[i].name, speaker)) return g->portraits[i].model;
    return 0;
}
static const Texture *portrait_for(Game *g, const char *speaker) {
    for (int i = 0; i < g->nportraits; i++) if (!strcmp(g->portraits[i].name, speaker)) return g->portraits[i].model ? (g->gfx.portrait.tex ? &g->gfx.portrait : NULL) : &g->portraits[i].tex;
    return NULL;
}

static int emote_number(const char *e) {
    static const struct { const char *name; int n; } T[] = {
        {"shout", 1}, {"laugh", 2}, {"dead", 3}, {"squint", 4}, {"bored", 5}, {"happy", 6}, {"smug", 7}, {"wink", 8}, {"sly", 9}, {"annoyed", 10},
        {"smile", 11}, {"nervous", 12}, {"sad", 13}, {"neutral", 14}, {"shock", 15}, {"cry", 16}, {"frown", 17}, {"down", 18}, {"cheeky", 19}, {"grit", 20},
        {"surprise", 21}, {"alert", 22}, {"question", 23}, {"think", 24}, {"confused", 25}, {"heartbreak", 26}, {"love", 27}, {"sleep", 28}, {"furious", 29}, {"angry", 30} };
    for (size_t i = 0; i < sizeof T / sizeof *T; i++) if (!strcmp(T[i].name, e)) return T[i].n;
    return 0;
}

static bool load_defs(Game *g) {
    bool ok = true;
    ok &= level_load(&g->level, g->level_path[0] ? g->level_path : ASSET("levels/glade.txt"));
    ok &= player_def_load(&g->player_def, ASSET("player.txt"));
    ok &= boss_def_load(&g->boss_def, ASSET("enemies/warden.txt"));
    return ok;
}

static void reset_to_start(Game *g) {
    level_reset_triggers(&g->level);
    player_init(&g->player, &g->player_def, g->level.spawn, g->level.spawn_yaw);
    boss_init(&g->boss, &g->boss_def, g->level.boss_spawn, g->level.boss_yaw);
    g->boss.state = BS_SCRIPTED;   // dormant until the fight starts
    g->state = GS_EXPLORE; g->state_t = 0;
    g->fade = 0; g->letterbox = 0; g->hitstop = 0; g->fight_intensity = 0;
    audio_music_play(MUSIC("1 - Adventure Begin.ogg"), true, 0.28f, 2.0f);
    camera_init(&g->cam);
    camera_snap_behind(&g->cam, g->player.c.pos, g->player.c.yaw, &g->level);
    g->hint_t = 8.0f;
}

static void restart_fight(Game *g) {
    Vec3 p = v3(0, 0, g->level.arena_min.z + 9.0f);
    player_reset(&g->player, p, 0);
    boss_reset(&g->boss, g->level.boss_spawn, g->level.boss_yaw);
    g->state = GS_FIGHT; g->state_t = 0; g->hitstop = 0;
    g->fade = 0;
    camera_snap_behind(&g->cam, g->player.c.pos, g->player.c.yaw, &g->level);
    g->cam.locked = true;
}

static void start_battle(Game *g) {
    Vec3 f = v3(sinf(g->level.boss_yaw), 0, cosf(g->level.boss_yaw));   // direction the boss faces
    Vec3 centre = v3_add(g->level.boss_spawn, v3_scale(f, 2.6f));       // stage centre in front of the boss
    float stage_yaw = g->level.boss_yaw + PI;                            // the player faces the boss
    battle_start(&g->battle, centre, stage_yaw, 5.2f, (int)g->player.c.hp, (int)g->player.c.hp_max);
    g->boss.state = BS_SCRIPTED; g->player.state = PS_SCRIPTED;
    g->boss.c.hp = g->boss.c.hp_max;
    character_set_anim(&g->boss.c, ANIM_IDLE); character_set_anim(&g->player.c, ANIM_IDLE);
    if (g->boss_model.is_sprite) charmodel_sprite_play(&g->boss_model, "idle", 0, true);
    else if (g->boss_model.loaded) anim_play(&g->boss_model.player, &g->boss_model.model, g->boss_model.bind[ANIM_IDLE].clip, 1, true, false, 0.2f);
    if (g->player_model.is_sprite) charmodel_sprite_play(&g->player_model, "idle", 0, true);
    else if (g->player_model.loaded) anim_play(&g->player_model.player, &g->player_model.model, g->player_model.bind[ANIM_IDLE].clip, 1, true, false, 0.2f);
    g->state = GS_BATTLE; g->state_t = 0; g->fade = 1;
    uifx_clear(&g->fx);
    audio_music_play(MUSIC("17 - Fight.ogg"), true, 0.3f, 0.8f);
}

void game_init(Game *g) {
    (void)g;   // static-zeroed by main; do not memset here (command-line overrides are already in it)
    dbg_init("hollow.log");
    if (!audio_init()) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio unavailable, running silent");
    audio_set_master(0.8f);
}

bool game_init_gfx(Game *g, Platform *pf) {
    g->pf = pf;
    platform_set_cursor(pf, true);
    if (!gfx_init(&g->gfx, pf, INTERNAL_W, INTERNAL_H)) return false;
    world_textures_create(&g->gfx, &g->wt);
    if (!load_defs(g)) return false;
    // Skinned models are optional: without them the box figures draw.
    { char hp[640]; snprintf(hp, sizeof hp, "%s/characters/%s.txt", HOLLOW_ASSET_DIR, g->hero_config[0] ? g->hero_config : "hero"); charmodel_load(&g->gfx, &g->player_model, hp); }
    charmodel_load(&g->gfx, &g->boss_model, ASSET("characters/warden.txt"));
    particles_init(&g->particles);
    uifx_init(&g->fx);
    load_portraits(g);
    g->leveled_ready = leveled_init(&g->leveled, ASSET("kit.txt"));
    builder_init(&g->builder); g->builder_ready = g->builder.nfiles > 0;
    parted_init(&g->parted);
    g->battle_loaded = battle_load(&g->battle, ASSET("cards/cards.txt"), ASSET("decks/knight.txt"), ASSET("enemies/warden_battle.txt"));
    if (!g->battle_loaded) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "battle data failed to load; boss door falls back to real-time fight");
    battle_load_fx(&g->battle, &g->gfx, ASSET("sprites/fx.txt"));
    setup_level_content(g);
    reset_to_start(g);
    return true;
}

void game_shutdown(Game *g) {
    leveled_shutdown(&g->leveled);
    terrain_destroy(&g->gfx, &g->terrain);
    dbg_shutdown();
    if (g->editor_open) editor_shutdown(&g->editor);
    props_clear(&g->gfx, &g->props);
    charmodel_destroy(&g->gfx, &g->player_model);
    charmodel_destroy(&g->gfx, &g->boss_model);
    world_textures_destroy(&g->gfx, &g->wt);
    gfx_shutdown(&g->gfx);
    audio_shutdown();
}

void game_screenshot(Game *g, const char *path) { gfx_screenshot(&g->gfx, path); }
void game_tool_screenshot(Game *g, const char *path) { if (!gfx_tool_screenshot_save(&g->gfx, path)) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "tool screenshot: nothing drawn in the tool window"); }

void game_start_at(Game *g, const char *where) {
    if (!strncmp(where, "level:", 6)) { snprintf(g->level_path, sizeof g->level_path, "%s/levels/%s.txt", HOLLOW_ASSET_DIR, where + 6); load_defs(g); setup_level_content(g); reset_to_start(g); return; }
    if (!strcmp(where, "fight")) restart_fight(g);
    else if (!strcmp(where, "battle")) { if (g->battle_loaded) start_battle(g); }
    else if (!strcmp(where, "end")) { g->state = GS_END; g->state_t = 0; }
    else if (!strcmp(where, "boss_intro")) {
        g->player.c.pos = v3(0, 0, 24.2f); g->player.c.yaw = 0;
        play_scene(g, g->level.scene_boss, GS_FIGHT);
    } else if (!strcmp(where, "victory")) {
        restart_fight(g);
        g->player.c.pos = v3_add(g->level.boss_spawn, v3(0.9f, 0, -1.6f));
        g->boss.c.hp = 0; g->boss.state = BS_DEAD; character_set_anim(&g->boss.c, ANIM_DEAD);
        play_scene(g, g->level.scene_victory, GS_END);
    }
}

// A deliberately simple bot: parry when a parryable windup is about to land, dodge the rest,
// otherwise close in and attack. Exists so the fight can be exercised headlessly.
static void bot_input(Game *g, Input *in) {
    const Boss *b = &g->boss; const Player *p = &g->player;
    in->move_x = in->move_y = 0; in->attack = in->parry = in->dodge = false;
    if (g->state == GS_EXPLORE) {
        // walk the path toward +Z, expressed in camera-relative stick terms
        Vec3 f = v3(sinf(g->cam.yaw), 0, cosf(g->cam.yaw)), r = v3(-f.z, 0, f.x), want = v3(0, 0, 1);
        in->move_x = v3_dot(want, r); in->move_y = -v3_dot(want, f);
        if (g->tick % 90 == 0) in->skip = false;
        return;
    }
    if (g->state == GS_SCENE) { in->skip = g->tick % 30 == 0; return; }   // skip cutscenes quickly
    if (g->state != GS_FIGHT || p->state == PS_DEAD) return;
    Vec3 d = v3_sub(b->c.pos, p->c.pos); d.y = 0; float dist = v3_len(d);
    if (b->state == BS_WINDUP) {
        const BossMove *m = &b->def.moves[b->move];
        float w = m->windup * (b->phase2 ? b->def.phase2_windup_mult : 1.0f);
        float remaining = w - b->t + m->active * 0.5f;
        if (m->parryable && remaining < p->def.parry_window * 0.6f && p->state == PS_FREE) in->parry = true;
        else if (!m->parryable && remaining < 0.25f && p->state == PS_FREE) in->dodge = true;
        return;
    }
    if (p->state == PS_FREE) {
        if (dist > p->def.attack_range + b->c.radius - 0.2f) {
            // camera-relative input: convert world direction back through the camera basis
            Vec3 f = v3(sinf(g->cam.yaw), 0, cosf(g->cam.yaw));
            Vec3 r = v3(-f.z, 0, f.x);
            Vec3 n = v3_scale(d, 1.0f / dist);
            in->move_x = v3_dot(n, r); in->move_y = -v3_dot(n, f);
        } else if (b->state != BS_ACTIVE) in->attack = true;
    }
}

// ---------------------------------------------------------------- feedback

static void update_particles(Game *g, float dt) {
    particles_update(&g->particles, dt);
    g->flash = fmaxf(0, g->flash - dt * 6.0f);
}

static void screen_flash(Game *g, Vec3 color, float amount) { g->flash_color = color; g->flash = fmaxf(g->flash, amount); }

static void readout(Game *g, const char *text, float dur) { snprintf(g->hit_text, sizeof g->hit_text, "%s", text); g->last_hit_text_t = dur; }

static void apply_events(Game *g, const CombatEvents *ev) {
    if (g->bot) {
        if (ev->boss_swing) SDL_Log("t=%.2f boss swing %s", g->time, g->boss.def.moves[g->boss.move].name);
        if (ev->parried) SDL_Log("t=%.2f PARRY  boss posture %.0f", g->time, g->boss.c.posture);
        if (ev->player_hit) SDL_Log("t=%.2f player hit, hp %.0f", g->time, g->player.c.hp);
        if (ev->boss_hit) SDL_Log("t=%.2f boss hit, hp %.0f", g->time, g->boss.c.hp);
        if (ev->boss_staggered) SDL_Log("t=%.2f BOSS STAGGERED", g->time);
        if (ev->phase2) SDL_Log("t=%.2f phase 2", g->time);
        if (ev->boss_died) SDL_Log("t=%.2f boss died", g->time);
        if (ev->player_died) SDL_Log("t=%.2f player died", g->time);
    }
    if (ev->footstep) audio_play(SND_FOOTSTEP, 0.5f, 0.95f + 0.1f * (float)(g->tick % 3));
    if (ev->boss_footstep) { audio_play(SND_FOOTSTEP, 0.9f, 0.5f); camera_add_shake(&g->cam, 0.08f); }
    if (ev->player_swing) audio_play(SND_SWING, 0.6f, 1.1f);
    if (ev->boss_swing) audio_play(SND_SWING, 0.9f, 0.6f);
    if (ev->boss_hit) audio_play(SND_HIT, 0.8f, 1.0f);
    if (ev->parried) {
        audio_play(SND_PARRY, 1.0f, 1.0f); g->parries++; readout(g, "PARRY", 0.7f);
        particles_burst(&g->particles, PT_SPARK, ev->contact, v3(0, 0.6f, 0), 40, 7.0f, v3(3.0f, 2.4f, 1.2f), 0.07f, 0.5f);
        screen_flash(g, v3(1, 1, 0.9f), 0.22f);
    }
    if (ev->parry_whiff) audio_play(SND_WHIFF, 0.6f, 1.0f);
    if (ev->player_hit) {
        g->hits_taken++;
        particles_burst(&g->particles, PT_SPARK, ev->contact, v3(0, 0.4f, 0), 16, 4.0f, v3(2.5f, 0.3f, 0.2f), 0.08f, 0.6f);
        screen_flash(g, v3(0.6f, 0.0f, 0.0f), 0.3f);
        if (ev->parry_early) { audio_play(SND_FAIL, 1.0f, 1.0f); audio_play(SND_HURT, 0.7f, 1.0f); readout(g, "TOO EARLY", 0.8f); }
        else if (ev->parry_unblockable) { audio_play(SND_FAIL, 1.0f, 0.7f); audio_play(SND_HURT, 0.7f, 1.0f); readout(g, "UNBLOCKABLE - DODGE", 1.0f); }
        else audio_play(SND_HURT, 0.9f, 1.0f);
    }
    if (ev->boss_hit) particles_burst(&g->particles, PT_SPARK, ev->contact, v3(0, 0.5f, 0), 10, 3.5f, v3(2.0f, 1.6f, 1.0f), 0.05f, 0.35f);
    if (ev->boss_staggered) { audio_play(SND_STAGGER, 1.0f, 1.0f); readout(g, "POSTURE BROKEN", 1.2f); screen_flash(g, v3(1, 0.8f, 0.4f), 0.5f); }
    if (ev->phase2) { audio_play(SND_ROAR, 1.0f, 0.85f); audio_play(SND_STING, 0.7f, 1.0f); camera_add_shake(&g->cam, 0.5f); }
    if (ev->boss_died) { audio_play(SND_DEATH, 1.0f, 0.7f); audio_play(SND_STAGGER, 0.8f, 0.6f); }
    if (ev->player_died) { audio_play(SND_DEATH, 1.0f, 1.0f); g->deaths++; }
    if (ev->shake > 0) camera_add_shake(&g->cam, ev->shake);
    if (ev->hitstop > g->hitstop) g->hitstop = ev->hitstop;
}

// ---------------------------------------------------------------- tick

static void tick_explore(Game *g, const Input *in, float dt) {
    CombatEvents ev = {0};
    Vec3 dir = camera_move_dir(&g->cam, in->move_x, in->move_y);
    player_update(&g->player, in, dir, &g->level, NULL, dt, &ev);
    snap_to_terrain(g);
    apply_events(g, &ev);
    camera_iso(&g->cam, g->player.c.pos, &g->level, dt);
    Trigger *t = level_trigger_at(&g->level, g->player.c.pos);
    if (t) {
        dbg_log("trigger %s at %.1f %.1f", t->name, g->player.c.pos.x, g->player.c.pos.z);
        if (!strcmp(t->name, "intro")) play_scene(g, g->level.scene_intro, GS_EXPLORE);
        else if (!strcmp(t->name, "boss_door")) play_scene(g, g->level.scene_boss, GS_FIGHT);
        else if (!strcmp(t->name, "arena")) audio_play(SND_STING, 0.6f, 0.9f);
    }
    audio_set_drone(0.45f);
    audio_set_fight(0.0f);
}

static void tick_scene(Game *g, const Input *in, float dt) {
    SceneHost host = HOST_TEMPLATE; host.ud = g;
    if (in->skip) scene_skip(&g->scene, &host);
    else scene_update(&g->scene, dt, &host);
    character_script_update(&g->player.c, dt);
    character_script_update(&g->boss.c, dt);
    if (g->scene.cam_valid) camera_set_scene(&g->cam, g->scene.cam_eye, g->scene.cam_target, g->scene.cam_fov, true);
    if (g->scene.shake > 0) camera_add_shake(&g->cam, g->scene.shake);
    g->fade = g->scene.fade;
    g->letterbox = damp(g->letterbox, g->scene.letterbox, 6, dt);
    if (g->scene.subtitle[0] && g->scene.speaker[0]) {
        float t = g->scene.time - g->scene.say_start; int total = (int)strlen(g->scene.subtitle);
        int shown = (int)(t * 42.0f); if (shown > total) shown = total;
        if (shown / 3 != (int)g->dlg_shown_chars / 3 && shown < total) {
            bool is_player = !strcmp(g->scene.speaker, "Ninja") || !strcmp(g->scene.speaker, "player");
            audio_play(SND_BLIP, 0.25f, is_player ? 1.6f : 0.7f);
        }
        g->dlg_shown_chars = (float)shown;
    } else g->dlg_shown_chars = 0;
    audio_set_drone(0.5f);
    if (g->scene.done) {
        g->player.state = PS_FREE; character_set_anim(&g->player.c, ANIM_IDLE);
        g->player.c.scripted_moving = false; g->boss.c.scripted_moving = false;
        if (g->after_scene == GS_FIGHT && g->battle_loaded) {
            start_battle(g);
        } else if (g->after_scene == GS_FIGHT) {
            g->boss.state = BS_IDLE; g->boss.think = 1.2f;
            character_set_anim(&g->boss.c, ANIM_IDLE);
            g->state = GS_FIGHT;
            camera_snap_behind(&g->cam, g->player.c.pos, g->player.c.yaw, &g->level);
            g->cam.locked = true;
        } else if (g->after_scene == GS_END) {
            g->state = GS_END;
        } else {
            g->boss.state = BS_SCRIPTED;
            g->state = GS_EXPLORE;
            camera_snap_behind(&g->cam, g->player.c.pos, g->player.c.yaw, &g->level);
        }
        g->state_t = 0;
    }
}

static void tick_fight(Game *g, const Input *in, float dt) {
    CombatEvents ev = {0};
    Vec3 dir = camera_move_dir(&g->cam, in->move_x, in->move_y);
    player_update(&g->player, in, dir, &g->level, &g->boss, dt, &ev);
    boss_update(&g->boss, &g->player, &g->level, dt, &ev);
    if (g->boss.state != BS_DEAD) character_separate(&g->player.c, &g->boss.c, &g->level);
    snap_to_terrain(g);
    apply_events(g, &ev);
    if (in->lockon) camera_toggle_lock(&g->cam);
    camera_orbit(&g->cam, g->player.c.pos, in->look_x, in->look_y, g->boss.state != BS_DEAD, g->boss.c.pos, &g->level, dt);
    g->fight_intensity = damp(g->fight_intensity, g->boss.phase2 ? 1.0f : 0.7f, 2, dt);
    audio_set_drone(0.35f);
    audio_set_fight(g->fight_intensity);
    if (ev.boss_died) {
        g->state = GS_DEAD; g->state_t = 0;  // brief hold on the kill before the scene
        g->player.state = PS_SCRIPTED;
        character_set_anim(&g->player.c, ANIM_IDLE);
    } else if (ev.player_died) {
        g->state = GS_DEAD; g->state_t = 0;
    }
}

static void tick_dead(Game *g, const Input *in, float dt) {
    (void)in;
    g->state_t += dt;
    character_script_update(&g->player.c, dt);
    g->boss.c.anim_t += dt;
    camera_orbit(&g->cam, g->player.c.pos, 0, 0, false, v3(0, 0, 0), &g->level, dt);
    bool boss_dead = g->boss.state == BS_DEAD;
    if (boss_dead) {
        audio_set_fight(fmaxf(0, 1.0f - g->state_t));
        if (g->state_t > 1.6f) play_scene(g, g->level.scene_victory, GS_END);
    } else {
        audio_set_fight(fmaxf(0, 1.0f - g->state_t * 0.5f));
        g->fade = fmaxf(0.0f, 1.0f - (g->state_t - 1.5f));
        if (g->state_t > 3.0f) restart_fight(g);
    }
}

static void tick_battle(Game *g, const Input *in_real, Platform *pf, float dt) {
    Input in = *in_real;
    float mx, my; platform_mouse_ui(pf, INTERNAL_W, INTERNAL_H, &mx, &my);
    if (g->bot) battle_bot(&g->battle, &g->boss_model, &in, &mx, &my, g->tick);
    CombatEvents ev = {0};
    battle_tick(&g->battle, &in, mx, my, dt, &g->player, &g->boss, &g->player_model, &g->boss_model, &g->cam, &g->particles, &g->fx, &ev);
    if (ev.shake > 0) camera_add_shake(&g->cam, ev.shake);
    if (ev.parried) { screen_flash(g, v3(1, 1, 0.9f), 0.18f); g->parries++; }
    if (ev.player_hit) { screen_flash(g, v3(0.6f, 0, 0), 0.25f); g->hits_taken++; }
    g->fight_intensity = damp(g->fight_intensity, 0.8f, 2, dt);
    audio_set_drone(0.3f); audio_set_fight(g->fight_intensity);
    bool won;
    if (battle_over(&g->battle, &won)) {
        if (won) {
            g->boss.state = BS_DEAD; g->boss.c.hp = 0; character_set_anim(&g->boss.c, ANIM_DEAD);
            g->player.state = PS_SCRIPTED;
            play_scene(g, g->level.scene_victory, GS_END);
            audio_music_play(MUSIC("11 - Clearing.ogg"), true, 0.26f, 1.5f);
        } else {
            g->deaths++;
            g->player.c.hp = g->player.c.hp_max;
            start_battle(g);
        }
    }
}

static void tick_end(Game *g, const Input *in, float dt) {
    g->state_t += dt;
    g->fade = 0;
    audio_set_drone(0.2f); audio_set_fight(0);
    if (in->interact && g->state_t > 1.0f) reset_to_start(g);
}

void game_tick(Game *g, const Input *in_real, double ddt) {
    float dt = (float)ddt;
    Input bot_in; const Input *in = in_real;
    if (g->bot) { bot_in = *in_real; bot_input(g, &bot_in); in = &bot_in; }
    g->time += ddt; g->tick++;
    if (g->msg_t > 0) g->msg_t -= dt;
    if (g->last_hit_text_t > 0) g->last_hit_text_t -= dt;

    dbg_set_time(g->time);
    if (in->key_down[SDL_SCANCODE_F8]) debug_snapshot(g);
    // Keys typed into the game window open the tools: [ environment editor, ] sprite editor,
    // \ debugger. Keys typed into a tool window belong to that tool (tool_key_down), except \ and
    // Esc which close it. Inside the environment editor the brackets scale the piece instead.
    if (g->tool_mode != 0 && !g->pf->console && !SDL_getenv("HOLLOW_CONSOLE_INLINE")) game_set_tool(g, 0);   // window closed with its close button
    bool tool_esc = in->tool_key_down[SDL_SCANCODE_ESCAPE], tool_bs = in->tool_key_down[SDL_SCANCODE_BACKSLASH] || in->tool_key_down[SDL_SCANCODE_GRAVE];
    if (in->key_down[SDL_SCANCODE_BACKSLASH] || in->key_down[SDL_SCANCODE_GRAVE] || tool_bs) game_set_tool(g, 1);
    if (g->tool_mode != 2 && in->key_down[SDL_SCANCODE_LEFTBRACKET]) game_set_tool(g, 2);
    if (g->tool_mode != 2 && in->key_down[SDL_SCANCODE_RIGHTBRACKET]) game_set_tool(g, 4);
    if (in->key_down[SDL_SCANCODE_F6] || (in->ctrl && (in->key_down[SDL_SCANCODE_E] || in->tool_key_down[SDL_SCANCODE_E]))) game_set_tool(g, 2);
    if (in->key_down[SDL_SCANCODE_F7] || (in->ctrl && (in->key_down[SDL_SCANCODE_P] || in->tool_key_down[SDL_SCANCODE_P]))) game_set_tool(g, 3);
    if (g->tool_mode != 2 && in->key_down[SDL_SCANCODE_EQUALS]) game_set_tool(g, 5);
    if ((in->key_down[SDL_SCANCODE_ESCAPE] || tool_esc) && g->tool_mode != 0) {
        bool selected = g->tool_mode == 2 && (g->leveled.sel_prop >= 0 || g->leveled.sel_light >= 0 || g->leveled.sel_emitter >= 0);
        if (!selected) game_set_tool(g, g->tool_mode);   // same mode again closes it
    }
    if (in->ctrl && in->key_down[SDL_SCANCODE_D]) g->pf->debug = !g->pf->debug;                              // wireframe overlay (F1)
    if (in->ctrl && in->key_down[SDL_SCANCODE_G]) debug_snapshot(g);                                          // snapshot (F8)
    if (g->tool_mode == 3 && g->editor_open) {
        // sprite editor runs in the tool window with that window's mouse; keys are shared
        Input ein = *in; ein.click = in->tool_pressed; ein.mouse_held = in->tool_down; ein.rclick = in->tool_rpressed; ein.rmouse_held = in->tool_rdown; ein.wheel = in->tool_wheel;
        memcpy(ein.key_down, in->tool_key_down, sizeof ein.key_down);   // only keys typed into the editor window
        editor_set_size(&g->editor, g->pf->console_win ? (float)g->pf->tool_w : 1280, g->pf->console_win ? (float)g->pf->tool_h : 800);
        editor_tick(&g->editor, &ein, in->tool_mx, in->tool_my, dt);
        g->sprite_refresh_t += dt;
        if (g->editor.dirty && g->sprite_refresh_t > 0.12f) { charmodel_refresh_from_doc(&g->player_model, &g->gfx, &g->editor.doc); g->sprite_refresh_t = 0; }
    }
    if (g->tool_mode == 4) { charmodel_drive_player(&g->player_model, &g->player, dt); }
    if (g->tool_mode == 5) {
        float mx, my; platform_mouse_ui(g->pf, INTERNAL_W, INTERNAL_H, &mx, &my);
        Vec3 hit; bool ok = leveled_ground_hit(&g->cam, &g->terrain, mx, my, &hit);
        if (ok) hit = v3_sub(hit, g->bench);
        if (parted_world(&g->parted, in, hit, ok) & PE_REBUILD) g->part_dirty = true;
    }
    if (g->tool_mode == 2 && g->leveled.open && g->state != GS_BATTLE && g->state != GS_SCENE) {
        float mx, my; platform_mouse_ui(g->pf, INTERNAL_W, INTERNAL_H, &mx, &my);
        leveled_tick(&g->leveled, &g->level, &g->terrain, &g->cam, in, mx, my, dt, &g->gfx, &g->props);
        props_load_level(&g->gfx, &g->props, &g->level);
        snap_to_terrain(g);
        charmodel_drive_player(&g->player_model, &g->player, dt);
        uifx_update(&g->fx, dt); update_particles(g, dt);
        g->fade = 1; g->letterbox = 0; g->hint_t = 0;
        return;
    }
    { static GState last = (GState)-1; if (g->state != last) { dbg_log("state -> %s", GS_NAMES[g->state]); last = g->state; } }
    if (in->pause_toggle) { g->paused = !g->paused; say(g, g->paused ? "paused (F3 steps one tick)" : "resumed"); }
    if (in->step) g->step_once = true;
    if (in->reload || (in->ctrl && in->key_down[SDL_SCANCODE_R])) {
        bool ok = load_defs(g);
        g->player.def = g->player_def; g->boss.def = g->boss_def;
        g->boss.c.hp_max = g->boss_def.hp; g->boss.c.posture_max = g->boss_def.posture;
        say(g, ok ? "reloaded level, player, boss" : "reload failed, see log");
        setup_level_content(g);
    }
    if (level_reload_if_changed(&g->level)) { say(g, "level hot-reloaded"); setup_level_content(g); }

    if (g->paused && !g->step_once) { camera_update(&g->cam, dt); return; }
    g->step_once = false;

    if (g->hitstop > 0) { g->hitstop -= dt; camera_update(&g->cam, dt); return; }

    switch (g->state) {
    case GS_EXPLORE: tick_explore(g, in, dt); break;
    case GS_SCENE:   tick_scene(g, in, dt); break;
    case GS_FIGHT:   tick_fight(g, in, dt); break;
    case GS_DEAD:    tick_dead(g, in, dt); break;
    case GS_END:     tick_end(g, in, dt); break;
    case GS_BATTLE:  tick_battle(g, in, g->pf, dt); break;
    case GS_EDITOR: { float mx, my; platform_mouse_ui(g->pf, INTERNAL_W, INTERNAL_H, &mx, &my); editor_tick(&g->editor, in, mx, my, dt); } break;
    }
    if (g->state != GS_SCENE) {
        g->letterbox = damp(g->letterbox, 0, 6, dt);
        if (g->state != GS_DEAD) g->fade = damp(g->fade, 1, 3, dt);
    }
    if (g->state != GS_BATTLE) { charmodel_drive_player(&g->player_model, &g->player, dt); charmodel_drive_boss(&g->boss_model, &g->boss, dt); }
    uifx_update(&g->fx, dt);
    update_particles(g, dt);
    if (g->hint_t > 0) g->hint_t -= dt;
    camera_update(&g->cam, dt);
}

// ---------------------------------------------------------------- render

static void bar(Gfx *g, float x, float y, float w, float h, float k, Vec4 back, Vec4 front) {
    gfx_ui_rect(g, x - 1, y - 1, w + 2, h + 2, v4(0, 0, 0, 0.7f));
    gfx_ui_rect(g, x, y, w, h, back);
    gfx_ui_rect(g, x, y, w * clampf(k, 0, 1), h, front);
}

static void text_center(Gfx *g, float cx, float y, float scale, Vec4 c, const char *s) {
    float w = gfx_ui_text_width(scale, s);
    gfx_ui_text(g, cx - w * 0.5f, y, scale, c, s);
}

static void sprite_line(char *out, size_t n, const char *who, const CharModel *cm) {
    if (!cm->loaded) { snprintf(out, n, "%s: no model", who); return; }
    if (!cm->is_sprite) { snprintf(out, n, "%s: skinned model", who); return; }
    if (cm->sprite.anim < 0) { snprintf(out, n, "%s: no animation playing", who); return; }
    const SpriteAnim *an = &cm->sdef.anims[cm->sprite.anim];
    int frame = an->first + (int)(cm->sprite.time * an->fps * cm->sprite.rate); if (frame > an->last) frame = an->last;
    const SpriteSheet *sh = &cm->sdef.sheets[an->sheet >= 0 ? an->sheet : 0];
    snprintf(out, n, "%s: %s frame %d/%d  sheet %s %dx%d cells %dx%d  facing %d  rate %.2f%s", who, an->name, frame - an->first + 1, an->last - an->first + 1, sh->name, sh->fw, sh->fh, sh->cols, sh->rows, cm->sprite.facing, cm->sprite.rate, cm->sprite.finished ? " (done)" : "");
}

// Console text clipped to the panel width (long lines are cut with a dot rather than spilling).
static void ctext(Gfx *x, float lx, float y, float maxw, float scale, Vec4 c, const char *text) {
    char buf[300]; snprintf(buf, sizeof buf, "%s", text);
    size_t n = strlen(buf);
    while (n > 1 && gfx_ui_text_width(scale, buf) > maxw) { buf[--n] = 0; buf[n - 1] = '.'; }
    gfx_ui_text(x, lx, y, scale, c, buf);
}

static void apply_builder(Game *g, int flags);

// Dialogue portrait: the speaking character's head, posed by its emotion, through the pixel pass.
static const char *emote_clip(const Model *m, const char *e) {
    const char *c = NULL;
    if (!strcmp(e, "happy") || !strcmp(e, "laugh") || !strcmp(e, "smile") || !strcmp(e, "cheeky") || !strcmp(e, "wink") || !strcmp(e, "love") || !strcmp(e, "smug")) c = "Cheer";
    else if (!strcmp(e, "angry") || !strcmp(e, "furious") || !strcmp(e, "shout") || !strcmp(e, "annoyed") || !strcmp(e, "grit")) c = "Taunt";
    else if (!strcmp(e, "sad") || !strcmp(e, "cry") || !strcmp(e, "down") || !strcmp(e, "heartbreak") || !strcmp(e, "bored") || !strcmp(e, "sleep")) c = "Sit_Floor_Idle";
    else if (!strcmp(e, "surprise") || !strcmp(e, "shock") || !strcmp(e, "alert") || !strcmp(e, "confused") || !strcmp(e, "question") || !strcmp(e, "nervous")) c = "Hit_A";
    else if (!strcmp(e, "think")) c = "Interact";
    if (c && model_find_clip(m, c) >= 0) return c;
    if (model_find_clip(m, "Idle") >= 0) return "Idle";
    return m->nclips > 0 ? m->clips[0].name : NULL;
}
static void render_portrait(Game *g, Platform *pf, const FrameParams *fp) {
    if (g->state != GS_SCENE || !g->scene.subtitle[0] || !g->scene.speaker[0]) return;
    int which = portrait_model_for(g, g->scene.speaker);
    if (!which) return;
    CharModel *cm = which == 2 ? &g->boss_model : &g->player_model;
    if (!cm->loaded || cm->is_sprite) return;
    Model *m = &cm->model;
    // restart the emotion clip when the line or the speaker changes
    if (strcmp(g->portrait_emote, g->scene.emote) != 0 || g->portrait_start != g->scene.say_start || g->portrait_model != which) {
        snprintf(g->portrait_emote, sizeof g->portrait_emote, "%s", g->scene.emote); g->portrait_start = g->scene.say_start; g->portrait_model = which;
        const char *clip = emote_clip(m, g->scene.emote); int ci = clip ? model_find_clip(m, clip) : -1;
        g->portrait_player.clip = -1; g->portrait_player.prev = -1;
        if (ci >= 0) anim_play(&g->portrait_player, m, ci, 1, true, false, 0);
    }
    anim_update(&g->portrait_player, m, 1.0f / 60.0f);
    model_pose(m, &g->portrait_player, &g->portrait_pose);
    // camera on the face: the model stands at the origin facing +Z; the head is near the top of its bounds
    float top = m->bmax.y * cm->scale, head_y = top * 0.80f, size = fmaxf(top * 0.34f, 0.35f);
    Vec3 target = v3(0, head_y, 0), eye = v3(0.28f * size, head_y + 0.05f * size, 3.6f * size);
    Vec3 fwd = v3_norm(v3_sub(target, eye)), right = v3_norm(v3_cross(fwd, v3(0, 1, 0))), up = v3_cross(right, fwd);
    Mat4 view = m4_look_at(eye, target, v3(0, 1, 0)), proj = m4_perspective(26.0f * DEG2RAD, 1.0f, 0.05f, 20.0f);
    FrameParams pp = *fp; pp.view_proj = m4_mul(proj, view); pp.cam_pos = eye; pp.cam_right = right; pp.cam_up = up;
    pp.fog_density = 0; pp.nlights = 0;
    pp.sun_dir = v3_norm(v3(-0.4f, -0.5f, -0.75f));   // key light from the camera side
    pp.sun_intensity = fmaxf(pp.sun_intensity, 1.0f);
    Vec3 backdrop = which == 2 ? v3(0.16f, 0.09f, 0.09f) : v3(0.10f, 0.13f, 0.11f);
    gfx_portrait_begin(&g->gfx, pf, &pp, 64, backdrop);
    Material pm = material_default(); pm.rim = 0.3f; pm.rim_color = v3(0.7f, 0.8f, 1.0f); gfx_set_material(&g->gfx, &pm);
    charmodel_draw_posed(&g->gfx, cm, &g->portrait_pose, m4_trs(v3(0, 0, 0), 0, v3(cm->scale, cm->scale, cm->scale)), v4(1, 1, 1, 1));
    gfx_set_material(&g->gfx, NULL);
    gfx_portrait_end(&g->gfx);
}

static void draw_console(Game *g, Platform *pf) {
    if (!pf->console || g->tool_mode != 1) return;
    Gfx *x = &g->gfx;
    const Battle *b = &g->battle;
    bool windowed = pf->console_win != NULL;
    float px = windowed ? 0 : 640, pw = windowed ? (float)pf->tool_w : 640, ph = windowed ? (float)pf->tool_h : 800;
    float LH = gfx_ui_line_h(1.0f) + 2, SH = gfx_ui_line_h(1.1f) + 4;   // body line, section header
    gfx_ui_target(x, windowed ? 1 : 0);
    if (!windowed) { gfx_ui_rect(x, px, 0, pw, ph, v4(0.02f, 0.02f, 0.04f, 0.9f)); gfx_ui_rect(x, px, 0, 2, ph, v4(0.5f, 0.8f, 1, 0.8f)); }
    Vec4 head = v4(0.6f, 0.9f, 1, 1), txt = v4(0.85f, 0.9f, 0.95f, 1), dim = v4(0.55f, 0.6f, 0.65f, 1), red = v4(1, 0.45f, 0.4f, 1), amber = v4(1, 0.85f, 0.5f, 1), green = v4(0.75f, 0.95f, 0.8f, 1);
    float y = 10, lx = px + 12; char l[240];
    ctext(x, lx, y, pw - 24, 1.4f, head, windowed ? "DEBUGGER     \\ closes     Ctrl+G / F8 copies everything to the clipboard" : "DEBUGGER  (window failed, inline)   \\ closes   F8 copies"); y += gfx_ui_line_h(1.4f) + 10;

    // 1. Warnings from data files and assets: the usual cause of "why is this not showing up"
    int nw = dbg_warning_count();
    ctext(x, lx, y, pw - 24, 1.1f, nw ? red : dim, nw ? "WARNINGS  (missing files, bad lines in data)" : "WARNINGS  none"); y += SH;
    for (int i = (nw > 4 ? nw - 4 : 0); i < nw; i++) { ctext(x, lx, y, pw - 24, 1.0f, red, dbg_warning(i)); y += LH; }
    y += 6;
    // 2. Where we are
    snprintf(l, sizeof l, "STATE  %s %.1fs   fps %.0f%s", GS_NAMES[g->state], g->state_t, g->fps, g->paused ? "   PAUSED" : ""); ctext(x, lx, y, pw - 24, 1.1f, head, l); y += SH;
    if (g->state == GS_BATTLE) { snprintf(l, sizeof l, "battle %s %.2fs  round %d  energy %d (+%d banked)  hp %d  enemy %d  combo %d", BT_NAMES[b->state], b->t, b->round, b->energy, b->banked, b->player_hp, b->enemy_hp, b->combo); ctext(x, lx, y, pw - 24, 1.0f, txt, l); y += LH; }
    y += 6;
    // 3. Cards: is the mouse where the game thinks, and what did a press land on
    if (g->state == GS_BATTLE) {
        float mx, my; platform_mouse_ui(pf, INTERNAL_W, INTERNAL_H, &mx, &my);
        ctext(x, lx, y, pw - 24, 1.1f, head, "CARDS"); y += SH;
        snprintf(l, sizeof l, "mouse %.0f %.0f  button %s   hover %s   dragging %s   target %s", mx, my, pf->input.mouse_held ? "DOWN" : "up",
                 b->hovered >= 0 ? b->cards[b->hand[b->hovered].def].name : "-", b->dragging >= 0 ? b->cards[b->hand[b->dragging].def].name : "-",
                 b->drop_target == 1 ? "ENEMY" : b->drop_target == 2 ? "SELF" : "-"); ctext(x, lx, y, pw - 24, 1.0f, txt, l); y += LH;
        l[0] = 0; for (int i = 0; i < b->nhand; i++) { char c[48]; snprintf(c, sizeof c, "%s@%.0f,%.0f%s  ", b->cards[b->hand[i].def].name, b->hand[i].x, b->hand[i].y, b->hand[i].phase == CP_DRAWING ? "(dealing)" : b->hand[i].phase == CP_DRAG ? "(held)" : b->hand[i].phase == CP_PLAYING ? "(playing)" : ""); strncat(l, c, sizeof l - strlen(l) - 1); }
        ctext(x, lx, y, pw - 24, 1.0f, dim, l); y += LH + 8;
        // 4. Parry: the last judgements as marks on an early/late bar
        ctext(x, lx, y, pw - 24, 1.1f, head, "PARRY  last presses vs the beat (left = early, right = late)"); y += SH;
        float bx0 = lx, bw = pw - 24;
        gfx_ui_rect(x, bx0, y, bw, 10, v4(0.2f, 0.25f, 0.3f, 1));
        gfx_ui_rect(x, bx0 + bw * 0.5f - bw * 0.5f * (0.15f / 0.3f) , y, bw * (0.15f / 0.3f), 10, v4(0.3f, 0.45f, 0.4f, 1));   // good
        gfx_ui_rect(x, bx0 + bw * 0.5f - bw * 0.5f * (0.045f / 0.3f), y, bw * (0.045f / 0.3f), 10, v4(0.7f, 0.6f, 0.3f, 1));   // perfect
        for (int i = 0; i < b->nhist; i++) {
            float o = clampf(b->hist_offset[i] / 0.3f, -1, 1); Vec4 c = b->hist_judge[i] == J_PERFECT ? amber : b->hist_judge[i] == J_GREAT ? green : b->hist_judge[i] == J_GOOD ? v4(0.7f, 0.85f, 1, 1) : red;
            gfx_ui_rect(x, bx0 + bw * 0.5f + o * bw * 0.5f - 2, y - 3 + (i % 2) * 8, 4, 8, c);
        }
        y += 18;
        snprintf(l, sizeof l, "last press %.3fs  beat %.3fs  offset %+.0f ms  ->  %s", b->parry_pressed_t, b->hit_t[0], b->last_offset * 1000.0f, b->last_judge == J_PERFECT ? "PERFECT" : b->last_judge == J_GREAT ? "GREAT" : b->last_judge == J_GOOD ? "GOOD" : b->last_judge == J_MISS ? "MISS" : "-"); ctext(x, lx, y, pw - 24, 1.0f, txt, l); y += LH + 8;
    }
    // 5. Sprites: which frame of which sheet, the usual cause of "the art looks wrong"
    ctext(x, lx, y, pw - 24, 1.1f, head, "SPRITES"); y += SH;
    sprite_line(l, sizeof l, "hero", &g->player_model); ctext(x, lx, y, pw - 24, 1.0f, txt, l); y += LH;
    sprite_line(l, sizeof l, "boss", &g->boss_model); ctext(x, lx, y, pw - 24, 1.0f, txt, l); y += LH + 8;
    // 6. Inputs and the actions they caused
    ctext(x, lx, y, pw - 24, 1.1f, head, "INPUT -> ACTION   (amber = what you pressed, green = what the game did)"); y += SH;
    int total = dbg_line_count(), rows = (int)((ph - y - 8) / LH); int show = total < rows ? total : rows;
    for (int i = 0; i < show; i++) {
        const char *line = dbg_line(total - show + i);
        bool input = strstr(line, "[in]") != NULL, warn = strstr(line, "[warn]") != NULL;
        ctext(x, lx, y + i * LH, pw - 24, 1.0f, warn ? red : input ? amber : green, line);
    }
    gfx_ui_target(x, 0);
}

static void draw_tool_window(Game *g, Platform *pf) {
    Gfx *x = &g->gfx;
    bool windowed = pf->console_win != NULL;
    if (!windowed && !SDL_getenv("HOLLOW_CONSOLE_INLINE")) return;   // tools live only in their own window
    if (g->tool_mode == 1) { draw_console(g, pf); return; }
    if (g->tool_mode == 2) {
        float w = windowed ? (float)pf->tool_w : 720, h = windowed ? (float)pf->tool_h : 800;
        gfx_ui_target(x, windowed ? 1 : 0);
        if (!windowed) gfx_ui_rect(x, 560, 0, 720, 800, v4(0.03f, 0.03f, 0.05f, 0.92f));
        UiInput uin = { .mx = windowed ? pf->input.tool_mx : 0, .my = windowed ? pf->input.tool_my : 0, .down = pf->input.tool_down, .pressed = pf->input.tool_pressed, .released = pf->input.tool_released, .wheel = pf->input.tool_wheel };
        if (!windowed) { float mx, my; platform_mouse_ui(pf, INTERNAL_W, INTERNAL_H, &mx, &my); uin.mx = mx - 560; uin.my = my; uin.down = pf->input.mouse_held; uin.pressed = pf->input.click; uin.released = false; uin.wheel = pf->input.wheel; }
        ui_begin(&g->ui, x, uin);
        if (!windowed) { /* draw at an offset by shifting coordinates through a translated call */ }
        leveled_panel(&g->leveled, &g->level, &g->terrain, &g->ui, w, h);
        ui_end(&g->ui);
        gfx_ui_target(x, 0);
        return;
    }
    if (g->tool_mode == 5) {
        float w = (float)pf->tool_w, h = (float)pf->tool_h;
        gfx_ui_target(x, 1);
        UiInput uin = { .mx = pf->input.tool_mx, .my = pf->input.tool_my, .down = pf->input.tool_down, .pressed = pf->input.tool_pressed, .released = pf->input.tool_released, .wheel = pf->input.tool_wheel };
        Input keys = pf->input; memcpy(keys.key_down, pf->input.tool_key_down, sizeof keys.key_down);
        ui_begin(&g->ui, x, uin);
        int flags = parted_panel(&g->parted, &g->ui, &keys, w, h);
        ui_end(&g->ui);
        gfx_ui_target(x, 0);
        if (flags & PE_REBUILD) g->part_dirty = true;
        if (flags & PE_SAVE) {
            char dir[640]; snprintf(dir, sizeof dir, "%s/models/own", HOLLOW_ASSET_DIR); SDL_CreateDirectory(dir);
            char path[700]; snprintf(path, sizeof path, "%s/%s.part", dir, g->parted.name[0] ? g->parted.name : "my_part");
            if (part_save(&g->parted.doc, path)) { g->parted.dirty = false; snprintf(g->parted.msg, sizeof g->parted.msg, "saved models/own/%s.part: place it from the environment editor or attach it in the builder", g->parted.name); g->parted.msg_t = 4;
                // the new part joins the palettes without a restart
                if (g->leveled_ready) { LevelEd *le = &g->leveled; char rel[160]; snprintf(rel, sizeof rel, "models/own/%s.part", g->parted.name); bool known = false; for (int i = 0; i < le->nkit; i++) if (!strcmp(le->kit[i].file, rel)) known = true;
                    if (!known && le->nkit < KIT_MAX) { KitPiece *k = &le->kit[le->nkit++]; memset(k, 0, sizeof *k); snprintf(k->category, sizeof k->category, "own"); snprintf(k->name, sizeof k->name, "%s", g->parted.name); snprintf(k->file, sizeof k->file, "%s", rel); k->scale = 1; bool kc = false; for (int i = 0; i < le->ncat; i++) if (!strcmp(le->categories[i], "own")) kc = true; if (!kc && le->ncat < LEVELED_MAX_CATS) snprintf(le->categories[le->ncat++], 16, "own"); }
                    props_clear(&g->gfx, &g->props); props_load_level(&g->gfx, &g->props, &g->level); }   // reload so an edited part updates in the level
                parted_init_files(&g->parted);
            } else { snprintf(g->parted.msg, sizeof g->parted.msg, "save failed (see hollow.log)"); g->parted.msg_t = 4; }
        }
        return;
    }
    if (g->tool_mode == 4 && g->builder_ready) {
        float w = windowed ? (float)pf->tool_w : 720, h = windowed ? (float)pf->tool_h : 800;
        gfx_ui_target(x, windowed ? 1 : 0);
        if (!windowed) gfx_ui_rect(x, 560, 0, 720, 800, v4(0.03f, 0.03f, 0.05f, 0.92f));
        UiInput uin = { .mx = windowed ? pf->input.tool_mx : 0, .my = windowed ? pf->input.tool_my : 0, .down = pf->input.tool_down, .pressed = pf->input.tool_pressed, .released = pf->input.tool_released, .wheel = pf->input.tool_wheel };
        if (!windowed) { float mx, my; platform_mouse_ui(pf, INTERNAL_W, INTERNAL_H, &mx, &my); uin.mx = mx - 560; uin.my = my; uin.down = pf->input.mouse_held; uin.pressed = pf->input.click; uin.released = false; uin.wheel = pf->input.wheel; }
        Input keys = pf->input; memcpy(keys.key_down, pf->input.tool_key_down, sizeof keys.key_down);
        ui_begin(&g->ui, x, uin);
        int flags = builder_panel(&g->builder, &g->ui, &keys, w, h, g->player_model.loaded && !g->player_model.is_sprite ? &g->player_model.model : NULL);
        ui_end(&g->ui);
        gfx_ui_target(x, 0);
        apply_builder(g, flags);
        return;
    }
    if (g->tool_mode == 3 && g->editor_open) {
        gfx_ui_target(x, windowed ? 1 : 0);
        editor_set_size(&g->editor, windowed ? (float)pf->tool_w : 1280, windowed ? (float)pf->tool_h : 800);
        editor_draw(&g->editor, x);
        gfx_ui_target(x, 0);
    }
}

static void draw_debug_overlay(Game *g, Platform *pf) {
    Gfx *x = &g->gfx;
    draw_tool_window(g, pf);
    if (!pf->debug) return;
    {
        static const char *GS[] = { "EXPLORE", "SCENE", "FIGHT", "DEAD", "END", "BATTLE" };
        static const char *PS[] = { "FREE", "ATTACK", "PARRY", "DODGE", "HURT", "DEAD", "SCRIPTED" };
        static const char *BS[] = { "IDLE", "APPROACH", "WINDUP", "ACTIVE", "RECOVER", "STAGGER", "DEAD", "SCRIPTED" };
        char l[8][160]; int n = 0;
        snprintf(l[n++], 160, "fps %.0f  draws %u  tick %u  %s", g->fps, g->gfx.draw_calls, g->tick, g->paused ? "PAUSED" : "");
        snprintf(l[n++], 160, "game %s %.2fs   cam %s  vol %s", GS[g->state], g->state_t, g->cam.mode == CAM_ORBIT ? (g->cam.locked ? "orbit+lock" : "orbit") : "scene", "-");
        snprintf(l[n++], 160, "player %s t=%.2f  pos %.1f %.1f %.1f  yaw %.0f  hp %.0f  anim %s", PS[g->player.state], g->player.t, g->player.c.pos.x, g->player.c.pos.y, g->player.c.pos.z, g->player.c.yaw / DEG2RAD, g->player.c.hp, anim_name(g->player.c.anim));
        const BossMove *m = &g->boss.def.moves[g->boss.move];
        snprintf(l[n++], 160, "boss %s t=%.2f move %s  hp %.0f  posture %.0f  %s", BS[g->boss.state], g->boss.t, m->name, g->boss.c.hp, g->boss.c.posture, g->boss.phase2 ? "PHASE2" : "");
        if (g->state == GS_SCENE) snprintf(l[n++], 160, "scene t=%.2f next %d/%d  fade %.2f", g->scene.time, g->scene.next, g->scene.n, g->scene.fade);
        snprintf(l[n++], 160, "F1 debug  F2 pause  F3 step  F5 reload  F8 snapshot  Enter skip scene  Esc quit");
        for (int i = 0; i < n; i++) gfx_ui_text(x, 8, 8 + i * 11, 1.0f, v4(0.7f, 1, 0.7f, 1), l[i]);
        if (g->state == GS_BATTLE) {
            const Battle *b = &g->battle; char bl[200];
            snprintf(bl, sizeof bl, "battle %s t=%.2f hov %d drag %d target %d energy %d banked %d combo %d | press %.3f used %d judge %d off %+.3f",
                     BT_NAMES[b->state], b->t, b->hovered, b->dragging, b->drop_target, b->energy, b->banked, b->combo, b->parry_pressed_t, b->press_used, b->last_judge, b->last_offset);
            gfx_ui_text(x, 8, 8 + n * 11, 1.0f, v4(1, 0.9f, 0.6f, 1), bl);
            float mx, my; platform_mouse_ui(pf, INTERNAL_W, INTERNAL_H, &mx, &my);
            snprintf(bl, sizeof bl, "mouse %.0f %.0f held %d  hit_t %.2f %.2f %.2f", mx, my, pf->input.mouse_held, b->hit_t[0], b->hit_t[1], b->hit_t[2]);
            gfx_ui_text(x, 8, 8 + (n + 1) * 11, 1.0f, v4(1, 0.9f, 0.6f, 1), bl);
        }
        // event log, newest at the bottom (the \ console shows more)
        int total = pf->console ? 0 : dbg_line_count(), show = total < 18 ? total : 18;
        gfx_ui_rect(x, 860, 90, 412, 12 + show * 11 + 14, v4(0, 0, 0, 0.55f));
        gfx_ui_text(x, 866, 96, 1.0f, v4(0.8f, 0.8f, 0.8f, 1), "EVENTS   (F8 copies a snapshot to the clipboard)");
        for (int i = 0; i < show; i++) gfx_ui_text(x, 866, 110 + i * 11, 1.0f, v4(0.75f, 0.9f, 1, 1), dbg_line(total - show + i));
    }
}

static void draw_hud(Game *g, Platform *pf) {
    Gfx *x = &g->gfx;
    if (g->tool_mode == 2 && g->leveled.open) { if (g->leveled.msg_t > 0) gfx_ui_text(x, 12, INTERNAL_H - 16, 1.1f, v4(1, 0.85f, 0.4f, 1), g->leveled.msg); draw_debug_overlay(g, pf); return; }
    if (g->state == GS_BATTLE) {
        battle_draw_ui(&g->battle, x, camera_view_proj(&g->cam, (float)INTERNAL_W / INTERNAL_H));
        uifx_draw(&g->fx, x);
        if (g->msg_t > 0) gfx_ui_text(x, 12, INTERNAL_H - 16, 1.0f, v4(0.9f, 0.8f, 0.4f, 1), g->msg);
        draw_debug_overlay(g, pf);
        return;
    }
    uifx_draw(&g->fx, x);
    const float W = INTERNAL_W, H = INTERNAL_H;
    Vec4 white = v4(0.9f, 0.88f, 0.85f, 1), dim = v4(0.6f, 0.58f, 0.55f, 1);

    if (g->state == GS_FIGHT || (g->state == GS_DEAD)) {
        // Player
        bar(x, 24, H - 40, 180, 8, g->player.c.hp / g->player.c.hp_max, v4(0.25f, 0.05f, 0.05f, 1), v4(0.75f, 0.15f, 0.12f, 1));
        // Boss: health and posture
        float bw = 320, bx = (W - bw) * 0.5f;
        gfx_ui_text(x, bx, H - 64, 1.0f, dim, g->boss.def.name);
        bar(x, bx, H - 50, bw, 7, g->boss.c.hp / g->boss.c.hp_max, v4(0.2f, 0.05f, 0.08f, 1), v4(0.8f, 0.2f, 0.25f, 1));
        bar(x, bx, H - 40, bw, 4, g->boss.c.posture / g->boss.c.posture_max, v4(0.15f, 0.12f, 0.05f, 1), v4(0.95f, 0.75f, 0.25f, 1));
        if (g->last_hit_text_t > 0) {
            float a = fminf(1, g->last_hit_text_t * 2);
            float tw = gfx_ui_text_width(2.0f, g->hit_text);
            gfx_ui_rect(x, W * 0.5f - tw * 0.5f - 8, H * 0.5f - 66, tw + 16, 26, v4(0, 0, 0, 0.6f * a));
            text_center(x, W * 0.5f, H * 0.5f - 60, 2.0f, v4(1, 0.9f, 0.6f, a), g->hit_text);
        }
    }
    if (g->state == GS_DEAD && g->boss.state != BS_DEAD && g->state_t > 0.6f) {
        float a = fminf(1, (g->state_t - 0.6f) * 1.5f);
        gfx_ui_rect(x, 0, H * 0.5f - 30, W, 60, v4(0, 0, 0, 0.6f * a));
        text_center(x, W * 0.5f, H * 0.5f - 8, 2.5f, v4(0.7f, 0.1f, 0.1f, a), "THE COUNT CONTINUES");
    }
    if (g->state == GS_END) {
        text_center(x, W * 0.5f, H * 0.5f - 30, 3.0f, white, "hollow");
        text_center(x, W * 0.5f, H * 0.5f + 10, 1.0f, dim, "skeleton build");
        char s[128]; snprintf(s, sizeof s, "parries %u   hits taken %u   deaths %u", g->parries, g->hits_taken, g->deaths);
        text_center(x, W * 0.5f, H * 0.5f + 30, 1.0f, dim, s);
        if (g->state_t > 1.0f) text_center(x, W * 0.5f, H - 40, 1.0f, dim, "E / A to begin again");
    }
    // Letterbox and subtitles
    if (g->letterbox > 0.01f) {
        float lb = 46 * g->letterbox;
        gfx_ui_rect(x, 0, 0, W, lb, v4(0, 0, 0, 1));
        gfx_ui_rect(x, 0, H - lb, W, lb, v4(0, 0, 0, 1));
    }
    if (g->state == GS_SCENE && g->scene.subtitle[0] && !g->scene.speaker[0]) {
        text_center(x, W * 0.5f, H - 64, 1.4f, white, g->scene.subtitle);   // narration
    } else if (g->state == GS_SCENE && g->scene.subtitle[0]) {
        // Dialogue: JRPG box with the speaker's portrait, emotion bubble and typed text
        float t = g->scene.time - g->scene.say_start;
        const char *e = g->scene.emote; int en = emote_number(e);
        bool is_player = !strcmp(g->scene.speaker, "Ninja") || !strcmp(g->scene.speaker, "player") || portrait_model_for(g, g->scene.speaker) == 1;
        float bw = 900, bh = 150, bx = (W - bw) * 0.5f, by = H - bh - 60;
        // portrait motion by emotion
        float ox = 0, oy = 0, sc = 1.0f;
        if (!strcmp(e, "angry") || !strcmp(e, "furious") || !strcmp(e, "shout")) { float k = fmaxf(0, 1 - t * 1.5f); ox = sinf(t * 70) * 5 * (0.3f + k); oy = cosf(t * 90) * 2 * k; }
        else if (!strcmp(e, "happy") || !strcmp(e, "laugh") || !strcmp(e, "wink") || !strcmp(e, "love") || !strcmp(e, "cheeky") || !strcmp(e, "smile")) { oy = -fabsf(sinf(t * 6.0f)) * 8; }
        else if (!strcmp(e, "sad") || !strcmp(e, "cry") || !strcmp(e, "heartbreak") || !strcmp(e, "down")) { oy = 6 + sinf(t * 1.5f) * 2; }
        else if (!strcmp(e, "surprise") || !strcmp(e, "alert") || !strcmp(e, "shock") || !strcmp(e, "question") || !strcmp(e, "confused")) { float k = clampf(t / 0.25f, 0, 1); sc = 1.0f + 0.25f * (1 - k) * (1 - k) + 0.04f * sinf(t * 4); }
        else if (!strcmp(e, "nervous")) { ox = sinf(t * 30) * 2; }
        else { sc = 1.0f + 0.015f * sinf(t * 2.5f); }
        // box
        gfx_ui_rect(x, bx + 6, by + 8, bw, bh, v4(0, 0, 0, 0.55f));
        gfx_ui_rect(x, bx, by, bw, bh, v4(0.08f, 0.07f, 0.1f, 0.94f));
        Vec4 frame = is_player ? v4(0.55f, 0.8f, 0.55f, 1) : v4(0.85f, 0.45f, 0.35f, 1);
        gfx_ui_rect(x, bx, by, bw, 3, frame); gfx_ui_rect(x, bx, by + bh - 3, bw, 3, frame); gfx_ui_rect(x, bx, by, 3, bh, frame); gfx_ui_rect(x, bx + bw - 3, by, 3, bh, frame);
        // portrait (player on the left, others on the right)
        const Texture *pt = portrait_for(g, g->scene.speaker);
        float ps = 128 * sc, px = is_player ? bx + 20 : bx + bw - 20 - 128, py = by + 11;
        float pcx = px + 64 + ox, pcy = py + 64 + oy;
        gfx_ui_rect(x, pcx - 68, pcy - 68, 136, 136, v4(0.14f, 0.12f, 0.16f, 1));
        if (pt) gfx_ui_image(x, pt, pcx - ps * 0.5f, pcy - ps * 0.5f, ps, ps, NULL, v4(1, 1, 1, 1));
        else gfx_ui_rect(x, pcx - 40, pcy - 40, 80, 80, frame);
        // emote bubble pops in above the portrait
        if (en > 0 && g->emotes[en].tex) {
            float k = clampf(t / 0.25f, 0, 1), bs = (1.2f - 0.2f * k) * 56, bob = sinf(t * 5.0f) * 3;
            gfx_ui_image(x, &g->emotes[en], pcx + 40 - bs * 0.5f, pcy - 68 - bs + 6 + bob, bs, bs * 13.0f / 14.0f, NULL, v4(1, 1, 1, k));
        }
        // name plate
        float nx = is_player ? bx + 170 : bx + 24;
        gfx_ui_rect(x, nx - 8, by - 16, gfx_ui_text_width(1.5f, g->scene.speaker) + 16, 24, v4(0.08f, 0.07f, 0.1f, 0.94f));
        gfx_ui_text(x, nx, by - 10, 1.5f, frame, g->scene.speaker);
        // typed text, wrapped
        int total = (int)strlen(g->scene.subtitle);
        int shown = (int)(t * 42.0f); if (shown > total) shown = total;
        char buf[200]; snprintf(buf, sizeof buf, "%.*s", shown, g->scene.subtitle);
        float tx = is_player ? bx + 170 : bx + 24, maxw = bw - 200;
        { char line[200] = ""; char word[64]; const char *p = buf; float ly = by + 30;
          while (*p) { int wl = 0; while (*p && *p != ' ' && wl < 63) word[wl++] = *p++; word[wl] = 0; while (*p == ' ') p++;
              char test[200]; snprintf(test, sizeof test, "%s%s%s", line, line[0] ? " " : "", word);
              if (gfx_ui_text_width(1.7f, test) > maxw && line[0]) { gfx_ui_text(x, tx, ly, 1.7f, white, line); ly += 26; snprintf(line, sizeof line, "%s", word); }
              else snprintf(line, sizeof line, "%s", test); }
          if (line[0]) gfx_ui_text(x, tx, ly, 1.7f, white, line); }
        if (shown >= total && fmodf(t, 0.8f) < 0.4f) gfx_ui_text(x, bx + bw - 30, by + bh - 22, 1.3f, dim, "v");
    }
    if (g->msg_t > 0) gfx_ui_text(x, 12, H - 16, 1.0f, v4(0.9f, 0.8f, 0.4f, 1), g->msg);
    if (g->hint_t > 0 && g->state == GS_EXPLORE) {
        float a = fminf(1, g->hint_t);
        text_center(x, W * 0.5f, 30, 1.0f, v4(0.85f, 0.85f, 0.8f, a), "WASD move   Shift sprint   E interact   walk the path");
        text_center(x, W * 0.5f, 44, 1.0f, v4(0.6f, 0.6f, 0.55f, a), "[ environment editor   ] character builder   \\ debugger   Esc quit");
    }
    if (g->state == GS_FIGHT && g->cam.locked && g->boss.state != BS_DEAD) {
        // lock-on marker: a small diamond over the boss, projected
        Mat4 vp = camera_view_proj(&g->cam, (float)INTERNAL_W / INTERNAL_H);
        Vec3 bp = v3_add(g->boss.c.pos, v3(0, g->boss.c.height * 0.75f, 0));
        float cx = vp.m[0] * bp.x + vp.m[4] * bp.y + vp.m[8] * bp.z + vp.m[12];
        float cy = vp.m[1] * bp.x + vp.m[5] * bp.y + vp.m[9] * bp.z + vp.m[13];
        float cw = vp.m[3] * bp.x + vp.m[7] * bp.y + vp.m[11] * bp.z + vp.m[15];
        if (cw > 0.1f) {
            float sx = (cx / cw * 0.5f + 0.5f) * W, sy = (0.5f - cy / cw * 0.5f) * H;
            gfx_ui_rect(x, sx - 4, sy - 1, 8, 2, v4(1, 0.9f, 0.5f, 0.9f));
            gfx_ui_rect(x, sx - 1, sy - 4, 2, 8, v4(1, 0.9f, 0.5f, 0.9f));
        }
    }

    draw_debug_overlay(g, pf);
}

void game_render(Game *g, Platform *pf, float alpha) {
    (void)alpha;
    g->frames++;
    if (g->state == GS_EDITOR) {
        FrameParams fp = { .view_proj = m4_identity(), .cam_pos = v3(0, 0, 0), .cam_right = v3(1, 0, 0), .cam_up = v3(0, 1, 0),
                           .fog_color = v3(0.09f, 0.09f, 0.11f), .sky_zenith = v3(0.09f, 0.09f, 0.11f), .sky_horizon = v3(0.09f, 0.09f, 0.11f), .sky_ground = v3(0.09f, 0.09f, 0.11f) };
        gfx_begin(&g->gfx, pf, &fp);
        editor_draw(&g->editor, &g->gfx);
        PostParams pp = { .grain = 0, .vignette = 0, .fade = 1, .exposure = 1, .saturation = 1, .contrast = 1, .bloom = 0, .gain = v3(1, 1, 1), .bloom_threshold = 10 };
        gfx_end(&g->gfx, pf, &pp, g->time);
        return;
    }
    if (g->time - g->fps_t >= 0.5) { g->fps = (float)(g->frames / (g->time - g->fps_t)); g->frames = 0; g->fps_t = g->time; }

    const Level *lv = &g->level; const Look *lk = &lv->look;
    Vec3 fwd = v3_norm(v3_sub(g->cam.target, g->cam.eye));
    Vec3 right = v3_norm(v3_cross(fwd, v3(0, 1, 0)));
    Vec3 up = v3_cross(right, fwd);
    FrameParams fp = {
        .view_proj = camera_view_proj(&g->cam, (float)INTERNAL_W / INTERNAL_H),
        .cam_pos = g->cam.eye, .cam_right = right, .cam_up = up,
        .sun_dir = v3_norm(lk->sun_dir), .sun_intensity = lk->sun_intensity, .sun_color = lk->sun_color,
        .sky_ambient = lk->sky_ambient, .ground_ambient = lk->ground_ambient,
        .fog_color = lk->fog_color, .fog_density = lk->fog_density, .fog_height_base = lk->fog_base,
        .fog_height_falloff = lk->fog_falloff, .fog_scatter = lk->fog_scatter, .fog_start = lk->fog_start,
        .toon_softness = lk->toon_softness, .shadow_floor = lk->shadow_floor, .rim_power = lk->rim_power,
        .sky_zenith = lk->sky_zenith, .sky_horizon = lk->sky_horizon, .sky_ground = lk->sky_ground,
        .sun_glow = lk->sun_glow, .stars = lk->stars, .sky_fog_blend = lk->sky_fog_blend,
    };
    // Lights: level lights (with flicker), then dynamic ones
    float t = (float)g->time;
    for (int i = 0; i < lv->nlights && fp.nlights < GFX_MAX_LIGHTS; i++) {
        const LevelLight *l = &lv->lights[i];
        float fl = l->flicker > 0 ? 1.0f + l->flicker * (0.5f * sinf(t * 13.0f + i * 1.7f) + 0.3f * sinf(t * 29.0f + i * 0.9f) + 0.2f * sinf(t * 7.0f + i)) : 1.0f;
        fp.lights[fp.nlights++] = (PointLight){ .pos = l->pos, .radius = l->radius, .color = l->color, .intensity = l->intensity * fl };
    }
    const Character *pc = &g->player.c, *bc = &g->boss.c;
    if (bc->tell > 0 && fp.nlights < GFX_MAX_LIGHTS)
        fp.lights[fp.nlights++] = (PointLight){ .pos = v3(bc->pos.x, bc->pos.y + bc->height * 0.6f, bc->pos.z), .radius = 6.0f,
                                                .color = bc->tell_color, .intensity = 2.5f * bc->tell * bc->tell };
    if (g->flash > 0 && fp.nlights < GFX_MAX_LIGHTS)
        fp.lights[fp.nlights++] = (PointLight){ .pos = v3(pc->pos.x, pc->pos.y + 1.2f, pc->pos.z), .radius = 7.0f, .color = g->flash_color, .intensity = 2.5f * g->flash };

    Gfx *x = &g->gfx;
    if (g->tool_mode == 5 && (g->part_dirty || !g->part_model_ok)) {
        if (g->part_model_ok) model_destroy(x, &g->part_model);
        Uint32 n = 0; Vertex *v = part_build(&g->parted.doc, HOLLOW_ASSET_DIR, &n);
        memset(&g->part_model, 0, sizeof g->part_model);
        g->part_model_ok = v && n > 0 && model_from_triangles(x, &g->part_model, v, n);
        free(v); g->part_dirty = false;
    }
    render_portrait(g, pf, &fp);
    gfx_begin(x, pf, &fp);
    draw_level(x, lv, &g->wt);
    if (g->terrain.present) { terrain_update_mesh(x, &g->terrain); terrain_draw(x, &g->terrain); }
    props_draw(x, &g->props, lv, t);
    {
        Vec4 pt = v4(lerpf(1, 1.6f, pc->flash), lerpf(1, 1.6f, pc->flash), lerpf(1, 1.6f, pc->flash), 1);
        Vec4 bt = v4(1, 1, 1, 1);
        if (bc->flash > 0) bt = v4(lerpf(bt.x, 1.8f, bc->flash), lerpf(bt.y, 1.8f, bc->flash), lerpf(bt.z, 1.8f, bc->flash), 1);
        Material pm = material_default(); pm.rim = 0.35f; pm.rim_color = v3(0.6f, 0.8f, 1.0f);
        Material bm = material_default(); bm.rim = 0.5f; bm.rim_color = v3(0.5f, 0.9f, 0.7f);
        if (bc->tell > 0) { float k = bc->tell * bc->tell * (0.6f + 0.4f * sinf(bc->anim_t * 30.0f)); bm.emissive = v3_scale(bc->tell_color, 0.8f * k); bm.rim_color = bc->tell_color; bm.rim = 0.5f + k; }
        // 3D characters go through the pixel-art layer: rendered small with the camera snapped to
        // that layer's texel grid, then composited with an outline. Sprite characters are pixels already.
        float pxs = lk->pixel_scale, pxl = lk->pixel_levels, pxo = lk->pixel_outline, pxp = lk->pixel_palette, pxi = lk->pixel_inner;
        if (SDL_getenv("HOLLOW_PIX")) sscanf(SDL_getenv("HOLLOW_PIX"), "%f %f %f %f %f", &pxs, &pxl, &pxo, &pxp, &pxi);   // tuning override: "scale levels outline palette inner"
        bool pix_on = pxs >= 1 && !SDL_getenv("HOLLOW_NOPIX");
        gfx_set_pixel_look(x, pix_on ? (int)pxs : 0, pxl, pxo, pxp, pxi);
        bool player_pix = pix_on && g->player_model.loaded && !g->player_model.is_sprite;
        bool boss_pix = pix_on && g->boss_model.loaded && !g->boss_model.is_sprite;
        if (player_pix || boss_pix) {
            float dist = fmaxf(v3_len(v3_sub(g->cam.target, g->cam.eye)), 0.5f);
            float texel = 2.0f * dist * tanf(g->cam.fov * DEG2RAD * 0.5f) / (float)(x->ph > 0 ? x->ph : 1);
            float er = v3_dot(g->cam.eye, right), eu = v3_dot(g->cam.eye, up);
            float dr = roundf(er / texel) * texel - er, du = roundf(eu / texel) * texel - eu;
            Vec3 off = v3_add(v3_scale(right, dr), v3_scale(up, du));
            float ox = -dr / texel, oy = du / texel;
            if (SDL_getenv("HOLLOW_PIXOFF")) { float mx2 = 1, my2 = 1; sscanf(SDL_getenv("HOLLOW_PIXOFF"), "%f %f", &mx2, &my2); ox *= mx2; oy *= my2; }   // alignment test aid
            gfx_pixel_begin(x, camera_view_proj_offset(&g->cam, (float)INTERNAL_W / INTERNAL_H, off), ox, oy);
            if (player_pix) { gfx_set_material(x, &pm); charmodel_draw(x, &g->player_model, pc, pt); }
            if (boss_pix) { gfx_set_material(x, &bm); charmodel_draw(x, &g->boss_model, bc, bt); }
            if (g->tool_mode == 5 && g->part_model_ok) { gfx_set_material(x, NULL); AnimPlayer rest = { .clip = -1, .prev = -1 }; ModelPose pose; model_pose(&g->part_model, &rest, &pose); model_draw(x, &g->part_model, &pose, m4_translate(g->bench), v4(1, 1, 1, 1)); }
            gfx_set_material(x, NULL);
            gfx_pixel_end(x);
        }
        if (!player_pix) {
            gfx_set_material(x, &pm);
            if (g->player_model.loaded) charmodel_draw(x, &g->player_model, pc, pt);
            else draw_character(x, pc, g->player_def.color, g->player_def.size, false, &g->wt.tex[TEX_PLASTER]);
        }
        if (!boss_pix) {
            gfx_set_material(x, &bm);
            if (g->boss_model.loaded) charmodel_draw(x, &g->boss_model, bc, bt);
            else draw_character(x, bc, g->boss_def.color, g->boss_def.size, true, &g->wt.tex[TEX_METAL]);
        }
        gfx_set_material(x, NULL);
        if (!SDL_getenv("HOLLOW_NOBLOB")) { draw_blob_shadow(x, pc->pos, pc->radius * 2.2f, 0.55f); draw_blob_shadow(x, bc->pos, bc->radius * 2.2f, 0.6f); }
    }
    if (g->state == GS_BATTLE) battle_draw_world(&g->battle, x);
    if (g->tool_mode == 2 && g->leveled.open) leveled_draw_world(&g->leveled, &g->level, x, &g->props);
    if (g->tool_mode == 5) {   // workbench: a grid on the ground and a frame around the selected shape
        Material m = material_default(); m.emissive = v3(0.6f, 0.6f, 0.5f); gfx_set_material(x, &m);
        for (int i = -4; i <= 4; i++) { gfx_draw_box(x, &x->white, v3(g->bench.x + i * 0.5f, g->bench.y + 0.005f, g->bench.z), v3(0.01f, 0.005f, 4), 0, v4(1, 1, 1, 0.2f), 0); gfx_draw_box(x, &x->white, v3(g->bench.x, g->bench.y + 0.005f, g->bench.z + i * 0.5f), v3(4, 0.005f, 0.01f), 0, v4(1, 1, 1, 0.2f), 0); }
        gfx_set_material(x, NULL);
        const PartEd *pe = &g->parted;
        if (pe->sel >= 0 && pe->sel < pe->doc.n) { const Shape *sh = &pe->doc.shapes[pe->sel]; Vec3 sz = v3(fmaxf(fabsf(sh->size.x), 0.1f), fmaxf(sh->size.y, 0.1f), fmaxf(fabsf(sh->size.z), 0.1f)); if (sh->kind == SH_OBJ) sz = v3(fmaxf(g->part_model.bmax.x - g->part_model.bmin.x, 0.2f) * 0.5f, 0.5f, fmaxf(g->part_model.bmax.z - g->part_model.bmin.z, 0.2f) * 0.5f);
            gfx_draw_box_wire(x, v3(g->bench.x + sh->pos.x, g->bench.y + sh->pos.y + sz.y * 0.5f, g->bench.z + sh->pos.z), v3(sz.x + 0.02f, sz.y + 0.02f, sz.z + 0.02f), v4(1, 0.85f, 0.4f, 1)); }
    }
    if (!SDL_getenv("HOLLOW_NOPART")) particles_draw(&g->particles, x);

    if (pf->debug) {
        gfx_draw_box_wire(x, v3(pc->pos.x, pc->pos.y + pc->height * 0.5f, pc->pos.z), v3(pc->radius * 2, pc->height, pc->radius * 2), v4(0.3f, 1, 0.3f, 1));
        gfx_draw_box_wire(x, v3(bc->pos.x, bc->pos.y + bc->height * 0.5f, bc->pos.z), v3(bc->radius * 2, bc->height, bc->radius * 2), v4(1, 0.3f, 0.3f, 1));
        if (g->boss.state == BS_WINDUP || g->boss.state == BS_ACTIVE) {
            const BossMove *m = &g->boss.def.moves[g->boss.move];
            Vec3 f = v3(sinf(bc->yaw), 0, cosf(bc->yaw));
            Vec3 c = v3_add(bc->pos, v3_scale(f, m->range * 0.5f));
            gfx_draw_box_wire(x, v3(c.x, 1.0f, c.z), v3(m->range, 0.2f, m->range), g->boss.state == BS_ACTIVE ? v4(1, 0, 0, 1) : v4(1, 0.6f, 0, 1));
        }
        if (g->player.state == PS_ATTACK) {
            Vec3 f = v3(sinf(pc->yaw), 0, cosf(pc->yaw));
            Vec3 c = v3_add(pc->pos, v3_scale(f, g->player.def.attack_range * 0.5f));
            gfx_draw_box_wire(x, v3(c.x, 1.0f, c.z), v3(g->player.def.attack_range, 0.2f, g->player.def.attack_range), v4(0.3f, 0.6f, 1, 1));
        }
        for (int i = 0; i < lv->ntriggers; i++) {
            const Trigger *tr = &lv->triggers[i];
            gfx_draw_box_wire(x, v3_scale(v3_add(tr->vmin, tr->vmax), 0.5f), v3_sub(tr->vmax, tr->vmin), tr->fired ? v4(0.3f, 0.3f, 0.3f, 1) : v4(1, 1, 0.2f, 1));
        }
        for (int i = 0; i < lv->nblocks; i++) if (lv->blocks[i].tex < 0) gfx_draw_box_wire(x, lv->blocks[i].center, lv->blocks[i].size, v4(0.6f, 0.4f, 1, 1));
        for (int i = 0; i < lv->nlights; i++) gfx_draw_box_wire(x, lv->lights[i].pos, v3(0.2f, 0.2f, 0.2f), v4(lv->lights[i].color.x, lv->lights[i].color.y, lv->lights[i].color.z, 1));
    }
    draw_hud(g, pf);
    PostParams pp = { .grain = 0.015f, .vignette = 0.45f, .fade = g->fade, .flash_color = g->flash_color, .flash = g->flash,
                      .exposure = lk->exposure, .saturation = lk->saturation, .contrast = lk->contrast, .bloom = lk->bloom,
                      .lift = lk->lift, .gain = lk->gain, .bloom_threshold = lk->bloom_threshold, .bloom_knee = 0.5f };
    gfx_end(x, pf, &pp, g->time);
}

bool game_shot_moment(Game *g, const char *when) {
    const Battle *b = &g->battle;
    if (g->state != GS_BATTLE) return false;
    if (!strcmp(when, "ring")) {
        if (b->state != BT_ENEMY_ATTACK && b->state != BT_ENEMY_TELL) return false;
        for (int i = 0; i < HITS_MAX; i++) { float r = b->state == BT_ENEMY_TELL ? (0.9f - b->t) + b->hit_t[i] : b->hit_t[i] - b->t; if (!b->hit_done[i] && r > 0.05f && r < 0.14f) return true; }
        return false;
    }
    if (!strcmp(when, "judge")) { for (int i = 0; i < HITS_MAX; i++) if (b->burst_t[i] > 0.08f && b->burst_t[i] < 0.16f) return true; return false; }
    if (!strcmp(when, "play")) { for (int i = 0; i < b->nhand; i++) if (b->hand[i].phase == CP_PLAYING && b->hand[i].phase_t > 0.12f && b->hand[i].phase_t < 0.2f) return true; return false; }
    if (!strcmp(when, "hover")) return b->hovered >= 0 && b->hand[b->hovered].hover > 0.9f;
    if (!strcmp(when, "drag")) return b->dragging >= 0 && b->drag_t > 0.25f && b->drop_target != 0;
    if (!strcmp(when, "arrow")) return b->dragging >= 0 && b->cards[b->hand[b->dragging].def].kind == CK_ATTACK && b->drag_t > 0.08f;
    return false;
}

void game_open_editor(Game *g, const char *name, int frame_size) {
    if (g->editor_open) editor_shutdown(&g->editor);
    editor_init(&g->editor, name, frame_size);
    g->editor_open = true;
    snprintf(g->hero_config, sizeof g->hero_config, "%s", name);
    charmodel_refresh_from_doc(&g->player_model, &g->gfx, &g->editor.doc);
    platform_tool_window(g->pf, true, 1280, 800, "hollow sprite editor");
    if (g->pf->console_win) { g->tool_mode = 3; return; }
    g->state = GS_EDITOR; g->state_t = 0;   // no second window: edit inline
    audio_music_stop(0.5f);
}

// The sprite editor edits a copy of the current hero: "<hero>_own". The first time, the hero's
// sheets are imported so the editor opens on the real character rather than an empty document.
static void open_sprite_editor_doc(Game *g) {
    const char *hero = g->hero_config[0] ? g->hero_config : "hero";
    char own[64]; snprintf(own, sizeof own, "%s_own", hero);
    editor_init(&g->editor, own, 32);
    char saved[640]; snprintf(saved, sizeof saved, "%s/sprites/own/%s.txt", HOLLOW_ASSET_DIR, own);
    FILE *f = fopen(saved, "rb"); if (f) { fclose(f); return; }   // an edited copy already exists
    CharModel *cm = &g->player_model, tmp = {0};
    if (cm->loaded && !cm->is_sprite) {   // 3D hero: import the sprite version of the hero instead
        char sp[640]; snprintf(sp, sizeof sp, "%s/characters/%s_sprite.txt", HOLLOW_ASSET_DIR, hero);
        if (!charmodel_load(&g->gfx, &tmp, sp)) { say(g, "no sprite version of this hero to import; editing a blank sheet"); return; }
        cm = &tmp;
    }
    if (!cm->loaded || !cm->is_sprite || cm->sdef.nsheets == 0) return;
    const SpriteDef *sd = &cm->sdef;
    if (sd->frame_w > PIX_MAX_SIZE || sd->frame_h > PIX_MAX_SIZE) { say(g, "hero frames too large for the editor (max 64px); editing a blank sheet"); return; }
    pix_doc_free(&g->editor.doc); pix_doc_init(&g->editor.doc, own); g->editor.doc.size = sd->size;
    int imported = 0;
    for (int i = 0; i < sd->nsheets && g->editor.doc.nanims < PIX_MAX_ANIMS; i++) {
        const SpriteSheet *sh = &sd->sheets[i];
        // playback settings come from the first animation that uses this sheet
        float fps = 8; bool loop = true, dircol = true; const int *contact = NULL; int ncontact = 0;
        for (int a = 0; a < sd->nanims; a++) if (sd->anims[a].sheet == i) { fps = sd->anims[a].fps; loop = sd->anims[a].loop; dircol = sd->anims[a].dir_cols; contact = sd->anims[a].contact; ncontact = sd->anims[a].ncontact; break; }
        int idx = pix_anim_import_sheet(&g->editor.doc, sh->path, sh->name, sh->fw, sh->fh, dircol, fps, loop);
        if (idx < 0) continue;
        PixAnim *pa = &g->editor.doc.anims[idx];
        for (int c = 0; c < ncontact && c < PIX_MAX_CONTACT; c++) pa->contact[c] = contact[c];
        pa->ncontact = ncontact < PIX_MAX_CONTACT ? ncontact : PIX_MAX_CONTACT;
        imported++;
    }
    g->editor.anim = 0; g->editor.dir = 0; g->editor.frame = 0;
    if (tmp.loaded) charmodel_destroy(&g->gfx, &tmp);
    char msg[96]; snprintf(msg, sizeof msg, "imported %d sheets from %s; Ctrl+S saves as %s", imported, hero, own);
    say(g, msg); snprintf(g->editor.msg, sizeof g->editor.msg, "%s", msg); g->editor.msg_t = 4;
}

// The builder edits a spec; the hero is rebuilt from it so every change shows in the world.
static void apply_builder(Game *g, int flags) {
    Builder *b = &g->builder; CharModel *cm = &g->player_model;
    if (flags & BLD_RELOAD) {
        charmodel_destroy(&g->gfx, cm);
        if (!charmodel_apply(&g->gfx, cm, &b->spec)) { snprintf(b->msg, sizeof b->msg, "model failed to load: %s", b->spec.model); b->msg_t = 4; }
        builder_model_loaded(b, cm->loaded && !cm->is_sprite ? &cm->model : NULL);
    }
    if ((flags & BLD_HIDE) && cm->loaded && !cm->is_sprite) {
        for (int i = 0; i < cm->model.nnodes; i++) cm->model.nodes[i].hidden = false;
        for (int i = 0; i < b->spec.nhidden; i++) model_hide_node(&cm->model, b->spec.hidden[i], true);
        cm->spec = b->spec; cm->scale = b->spec.scale; cm->yaw_offset = b->spec.yaw_offset_deg * DEG2RAD;
    }
    if ((flags & BLD_RECOLOR) && cm->loaded && !cm->is_sprite) { model_recolor(&g->gfx, &cm->model, b->spec.rc_from, b->spec.rc_to, b->spec.nrecolor); cm->spec = b->spec; }
    if ((flags & BLD_ATTACH) && cm->loaded && !cm->is_sprite) {   // offsets change live; a new or removed part reloads
        bool same = cm->spec.nattach == b->spec.nattach;
        for (int i = 0; same && i < b->spec.nattach; i++) if (strcmp(cm->spec.attach[i].file, b->spec.attach[i].file) != 0) same = false;
        if (same) cm->spec = b->spec;
        else { charmodel_destroy(&g->gfx, cm); charmodel_apply(&g->gfx, cm, &b->spec); builder_model_loaded(b, cm->loaded ? &cm->model : NULL); }
    }
    if ((flags & BLD_PLAY_CLIP) && cm->loaded && !cm->is_sprite && b->clip_sel >= 0) { anim_play(&cm->player, &cm->model, b->clip_sel, 1, true, false, 0.1f); cm->last_anim = g->player.c.anim; cm->last_anim_t = g->player.c.anim_t; }
    if (flags & BLD_SAVE) {
        if (!b->name[0]) snprintf(b->name, sizeof b->name, "%s", "my_hero");
        char path[640]; snprintf(path, sizeof path, "%s/characters/%s.txt", HOLLOW_ASSET_DIR, b->name);
        if (charmodel_spec_save(&b->spec, path)) { snprintf(b->msg, sizeof b->msg, "saved characters/%s.txt", b->name); b->msg_t = 3; dbg_log("builder: saved %s", path); }
        else { snprintf(b->msg, sizeof b->msg, "save failed (see hollow.log)"); b->msg_t = 4; }
    }
    if (flags & BLD_USE) {
        snprintf(g->hero_config, sizeof g->hero_config, "%s", b->name);
        // settings.txt: replace or add the hero line
        char sp[640]; snprintf(sp, sizeof sp, "%s/settings.txt", HOLLOW_ASSET_DIR);
        size_t n = 0; char *st = SDL_LoadFile(sp, &n); char out[4096] = {0}; size_t on = 0; bool had = false;
        if (st) { char *cur = st; while (*cur) { char *nl = strchr(cur, '\n'); size_t len = nl ? (size_t)(nl - cur) : strlen(cur); if (!strncmp(cur, "hero ", 5)) { on += (size_t)snprintf(out + on, sizeof out - on, "hero %s\n", b->name); had = true; } else if (len) on += (size_t)snprintf(out + on, sizeof out - on, "%.*s\n", (int)len, cur); cur = nl ? nl + 1 : cur + len; } SDL_free(st); }
        if (!had) on += (size_t)snprintf(out + on, sizeof out - on, "hero %s\n", b->name);
        FILE *f = fopen(sp, "wb"); if (f) { fwrite(out, 1, on, f); fclose(f); snprintf(b->msg, sizeof b->msg, "%s is now the hero (settings.txt)", b->name); b->msg_t = 3; }
    }
    if (flags & BLD_OPEN_SPRITE) game_set_tool(g, 3);
}

void game_set_tool(Game *g, int mode) {
    if (mode == g->tool_mode) mode = 0;
    if (g->tool_mode == 3 && mode != 3 && g->hero_was_model) {   // put the 3D hero back
        char hp[640]; snprintf(hp, sizeof hp, "%s/characters/%s.txt", HOLLOW_ASSET_DIR, g->hero_config[0] ? g->hero_config : "hero");
        charmodel_destroy(&g->gfx, &g->player_model); charmodel_load(&g->gfx, &g->player_model, hp); g->hero_was_model = false;
    }
    g->tool_mode = mode;
    g->pf->editing = mode == 2;
    g->leveled.open = false;
    if (mode == 0) { platform_tool_window(g->pf, false, 720, 820, ""); return; }
    if (mode == 1) platform_tool_window(g->pf, true, 720, 820, "hollow debugger");
    if (mode == 2) {
        platform_tool_window(g->pf, true, 720, 820, "hollow environment editor");
        if (!g->leveled_ready) { say(g, "environment editor unavailable: assets/kit.txt failed to load (see hollow.log)"); return; }
        if (g->state == GS_SCENE) { SceneHost host = HOST_TEMPLATE; host.ud = g; scene_skip(&g->scene, &host); }   // the editor needs the overworld
        leveled_open(&g->leveled, &g->level, &g->cam);
        if (g->state == GS_BATTLE) say(g, "editor works in the overworld; finish the battle first");
    }
    if (mode == 5) {
        platform_tool_window(g->pf, true, 720, 820, "hollow part editor");
        Vec3 fwd = v3_norm(v3_sub(g->cam.target, g->cam.eye)); Vec3 right = v3_norm(v3_cross(fwd, v3(0, 1, 0)));
        g->bench = v3_add(g->player.c.pos, v3_scale(right, 2.6f)); g->bench.y = g->terrain.present ? terrain_height(&g->terrain, g->bench.x, g->bench.z) : 0;
        parted_open(&g->parted); g->part_dirty = true;
        if (g->state == GS_SCENE) { SceneHost host = HOST_TEMPLATE; host.ud = g; scene_skip(&g->scene, &host); }
    }
    if (mode == 4) {
        platform_tool_window(g->pf, true, 720, 820, "hollow character builder");
        if (!g->builder_ready) { say(g, "character builder: no rigged .glb files found under assets/models"); return; }
        if (g->player_model.loaded && g->player_model.is_sprite) say(g, "the hero is a sprite; pick a base model to build a 3D hero");
        builder_open(&g->builder, &g->player_model.spec, g->hero_config[0] && strcmp(g->hero_config, "hero") ? g->hero_config : "my_hero");
        builder_model_loaded(&g->builder, g->player_model.loaded && !g->player_model.is_sprite ? &g->player_model.model : NULL);
        if (g->state == GS_SCENE) { SceneHost host = HOST_TEMPLATE; host.ud = g; scene_skip(&g->scene, &host); }
    }
    if (mode == 3) {
        platform_tool_window(g->pf, true, 1280, 800, "hollow sprite editor");
        if (!g->editor_open) { open_sprite_editor_doc(g); g->editor_open = true; }
        if (g->player_model.loaded && !g->player_model.is_sprite) { g->hero_was_model = true; say(g, "hero shows the sprite document while the editor is open; the 3D hero returns when it closes"); }
        charmodel_refresh_from_doc(&g->player_model, &g->gfx, &g->editor.doc);
    }
}
