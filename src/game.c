#include "game.h"
#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSET(rel) (HOLLOW_ASSET_DIR "/" rel)
#define MUSIC(rel) (HOLLOW_ASSET_DIR "/sprites/ninja/Audio/Musics/" rel)

static void say(Game *g, const char *m) { snprintf(g->msg, sizeof g->msg, "%s", m); g->msg_t = 2.5f; }

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
    scene_start(&g->scene);
    g->state = GS_SCENE; g->after_scene = after; g->state_t = 0;
    g->player.state = PS_SCRIPTED; character_set_anim(&g->player.c, ANIM_IDLE);
    g->boss.state = BS_SCRIPTED;
}

// ---------------------------------------------------------------- setup and resets

static void setup_level_content(Game *g) {
    props_load_level(&g->gfx, &g->props, &g->level);
    particles_clear(&g->particles);
    for (int i = 0; i < g->level.nemitters; i++) {
        const LevelEmitter *le = &g->level.emitters[i];
        Emitter e = { .type = particle_type_from_name(le->type), .pos = le->pos, .extent = le->extent, .rate = le->rate,
                      .color = le->color, .size = le->size, .life = le->life, .active = true };
        particles_add_emitter(&g->particles, &e);
    }
    particles_prewarm(&g->particles, 8.0f);
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
    audio_music_play(MUSIC("1 - Adventure Begin.ogg"), true, 0.55f, 2.0f);
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
    audio_music_play(MUSIC("17 - Fight.ogg"), true, 0.6f, 0.8f);
}

void game_init(Game *g) {
    // g is static-zeroed by main; do not memset here (command-line overrides are already in it)
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
    g->battle_loaded = battle_load(&g->battle, ASSET("cards/cards.txt"), ASSET("decks/knight.txt"), ASSET("enemies/warden_battle.txt"));
    if (!g->battle_loaded) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "battle data failed to load; boss door falls back to real-time fight");
    battle_load_fx(&g->battle, &g->gfx, ASSET("sprites/fx.txt"));
    setup_level_content(g);
    reset_to_start(g);
    return true;
}

void game_shutdown(Game *g) {
    if (g->editor_open) editor_shutdown(&g->editor);
    props_clear(&g->gfx, &g->props);
    charmodel_destroy(&g->gfx, &g->player_model);
    charmodel_destroy(&g->gfx, &g->boss_model);
    world_textures_destroy(&g->gfx, &g->wt);
    gfx_shutdown(&g->gfx);
    audio_shutdown();
}

void game_screenshot(Game *g, const char *path) { gfx_screenshot(&g->gfx, path); }

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
    apply_events(g, &ev);
    camera_iso(&g->cam, g->player.c.pos, &g->level, dt);
    Trigger *t = level_trigger_at(&g->level, g->player.c.pos);
    if (t) {
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
            audio_music_play(MUSIC("11 - Clearing.ogg"), true, 0.5f, 1.5f);
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

    if (in->pause_toggle) { g->paused = !g->paused; say(g, g->paused ? "paused (F3 steps one tick)" : "resumed"); }
    if (in->step) g->step_once = true;
    if (in->reload) {
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

static void draw_hud(Game *g, Platform *pf) {
    Gfx *x = &g->gfx;
    if (g->state == GS_BATTLE) {
        battle_draw_ui(&g->battle, x, camera_view_proj(&g->cam, (float)INTERNAL_W / INTERNAL_H));
        uifx_draw(&g->fx, x);
        if (g->msg_t > 0) gfx_ui_text(x, 12, INTERNAL_H - 16, 1.0f, v4(0.9f, 0.8f, 0.4f, 1), g->msg);
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
    if (g->state == GS_SCENE && g->scene.subtitle[0]) {
        if (g->scene.speaker[0]) text_center(x, W * 0.5f, H - 78, 1.0f, v4(0.8f, 0.6f, 0.5f, 1), g->scene.speaker);
        text_center(x, W * 0.5f, H - 64, 1.4f, white, g->scene.subtitle);
    }
    if (g->msg_t > 0) gfx_ui_text(x, 12, H - 16, 1.0f, v4(0.9f, 0.8f, 0.4f, 1), g->msg);
    if (g->hint_t > 0 && g->state == GS_EXPLORE) {
        float a = fminf(1, g->hint_t);
        text_center(x, W * 0.5f, 30, 1.0f, v4(0.85f, 0.85f, 0.8f, a), "WASD move   Shift sprint   E interact   walk the path");
        text_center(x, W * 0.5f, 44, 1.0f, v4(0.6f, 0.6f, 0.55f, a), "Enter skips cutscenes   F1 debug   F5 reload   Esc quit");
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

    if (pf->debug) {
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
        snprintf(l[n++], 160, "F1 debug  F2 pause  F3 step  F5 reload  Enter skip scene  Esc quit");
        for (int i = 0; i < n; i++) gfx_ui_text(x, 8, 8 + i * 11, 1.0f, v4(0.7f, 1, 0.7f, 1), l[i]);
    }
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
    gfx_begin(x, pf, &fp);
    draw_level(x, lv, &g->wt);
    props_draw(x, &g->props, lv, t);
    {
        Vec4 pt = v4(lerpf(1, 1.6f, pc->flash), lerpf(1, 1.6f, pc->flash), lerpf(1, 1.6f, pc->flash), 1);
        Vec4 bt = v4(1, 1, 1, 1);
        if (bc->flash > 0) bt = v4(lerpf(bt.x, 1.8f, bc->flash), lerpf(bt.y, 1.8f, bc->flash), lerpf(bt.z, 1.8f, bc->flash), 1);
        Material pm = material_default(); pm.rim = 0.35f; pm.rim_color = v3(0.6f, 0.8f, 1.0f);
        Material bm = material_default(); bm.rim = 0.5f; bm.rim_color = v3(0.5f, 0.9f, 0.7f);
        if (bc->tell > 0) { float k = bc->tell * bc->tell * (0.6f + 0.4f * sinf(bc->anim_t * 30.0f)); bm.emissive = v3_scale(bc->tell_color, 0.8f * k); bm.rim_color = bc->tell_color; bm.rim = 0.5f + k; }
        gfx_set_material(x, &pm);
        if (g->player_model.loaded) charmodel_draw(x, &g->player_model, pc, pt);
        else draw_character(x, pc, g->player_def.color, g->player_def.size, false, &g->wt.tex[TEX_PLASTER]);
        gfx_set_material(x, &bm);
        if (g->boss_model.loaded) charmodel_draw(x, &g->boss_model, bc, bt);
        else draw_character(x, bc, g->boss_def.color, g->boss_def.size, true, &g->wt.tex[TEX_METAL]);
        gfx_set_material(x, NULL);
        if (!SDL_getenv("HOLLOW_NOBLOB")) { draw_blob_shadow(x, pc->pos, pc->radius * 2.2f, 0.55f); draw_blob_shadow(x, bc->pos, bc->radius * 2.2f, 0.6f); }
    }
    if (g->state == GS_BATTLE) battle_draw_world(&g->battle, x);
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
    PostParams pp = { .grain = 0.025f, .vignette = 0.45f, .fade = g->fade, .flash_color = g->flash_color, .flash = g->flash,
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
    g->state = GS_EDITOR; g->state_t = 0;
    audio_music_stop(0.5f);
}
