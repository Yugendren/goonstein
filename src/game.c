#include "game.h"
#include "audio.h"
#include <stdio.h>
#include <string.h>

#define ASSET(rel) (HOLLOW_ASSET_DIR "/" rel)

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
    static const char *names[SND_COUNT] = { "footstep", "swing", "hit", "parry", "hurt", "stagger", "roar", "death", "blip", "heart", "door", "sting" };
    for (int i = 0; i < SND_COUNT; i++) if (!strcmp(name, names[i])) { audio_play((SoundId)i, 0.9f, 1.0f); return; }
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "scene: unknown sound %s", name);
}
static const SceneHost HOST_TEMPLATE = { NULL, host_move, host_face, host_anim, host_teleport, host_sound };

static void play_scene(Game *g, const char *path, GState after) {
    if (!scene_load(&g->scene, path)) { say(g, "scene failed to load"); return; }
    scene_start(&g->scene);
    g->state = GS_SCENE; g->after_scene = after; g->state_t = 0;
    g->player.state = PS_SCRIPTED; character_set_anim(&g->player.c, ANIM_IDLE);
    g->boss.state = BS_SCRIPTED;
    g->cam.mode = CAM_SCENE;
}

// ---------------------------------------------------------------- setup and resets

static bool load_defs(Game *g) {
    bool ok = true;
    ok &= level_load(&g->level, ASSET("levels/corridor.txt"));
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
    camera_init(&g->cam);
    camera_set_fixed(&g->cam, &g->level, g->player.c.pos);
}

static void restart_fight(Game *g) {
    Vec3 p = v3(0, 0, g->level.arena_min.z + 2.0f);
    player_reset(&g->player, p, 0);
    boss_reset(&g->boss, g->level.boss_spawn, g->level.boss_yaw);
    g->state = GS_FIGHT; g->state_t = 0; g->hitstop = 0;
    g->fade = 0;
    camera_set_follow(&g->cam, g->player.c.pos, g->player.c.yaw, g->boss.c.pos, 0, &g->level);
    g->cam.blend = 1; g->cam.eye = g->cam.goal_eye; g->cam.target = g->cam.goal_target;
}

void game_init(Game *g) {
    memset(g, 0, sizeof *g);
    if (!audio_init()) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio unavailable, running silent");
    audio_set_master(0.8f);
}

bool game_init_gfx(Game *g, Platform *pf) {
    if (!gfx_init(&g->gfx, pf, INTERNAL_W, INTERNAL_H)) return false;
    world_textures_create(&g->gfx, &g->wt);
    if (!load_defs(g)) return false;
    reset_to_start(g);
    return true;
}

void game_shutdown(Game *g) {
    world_textures_destroy(&g->gfx, &g->wt);
    gfx_shutdown(&g->gfx);
    audio_shutdown();
}

void game_screenshot(Game *g, const char *path) { gfx_screenshot(&g->gfx, path); }

void game_start_at(Game *g, const char *where) {
    if (!strcmp(where, "fight")) restart_fight(g);
    else if (!strcmp(where, "end")) { g->state = GS_END; g->state_t = 0; }
    else if (!strcmp(where, "boss_intro")) {
        g->player.c.pos = v3(0, 0, 24.2f); g->player.c.yaw = 0;
        play_scene(g, ASSET("scenes/boss_intro.txt"), GS_FIGHT);
    } else if (!strcmp(where, "victory")) {
        restart_fight(g);
        g->player.c.pos = v3(0.8f, 0, 32.0f);
        g->boss.c.hp = 0; g->boss.state = BS_DEAD; character_set_anim(&g->boss.c, ANIM_DEAD);
        play_scene(g, ASSET("scenes/victory.txt"), GS_END);
    }
}

// A deliberately simple bot: parry when a parryable windup is about to land, dodge the rest,
// otherwise close in and attack. Exists so the fight can be exercised headlessly.
static void bot_input(Game *g, Input *in) {
    const Boss *b = &g->boss; const Player *p = &g->player;
    in->move_x = in->move_y = 0; in->attack = in->parry = in->dodge = false;
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
            Vec3 f = v3_sub(g->cam.target, g->cam.eye); f.y = 0; f = v3_norm(f);
            Vec3 r = v3(f.z, 0, -f.x);
            Vec3 n = v3_scale(d, 1.0f / dist);
            in->move_x = v3_dot(n, r); in->move_y = -v3_dot(n, f);
        } else if (b->state != BS_ACTIVE) in->attack = true;
    }
}

// ---------------------------------------------------------------- feedback

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
    if (ev->parried) { audio_play(SND_PARRY, 1.0f, 1.0f); g->parries++; snprintf(g->hit_text, sizeof g->hit_text, "PARRY"); g->last_hit_text_t = 0.7f; }
    if (ev->player_hit) { audio_play(SND_HURT, 0.9f, 1.0f); g->hits_taken++; }
    if (ev->boss_staggered) { audio_play(SND_STAGGER, 1.0f, 1.0f); snprintf(g->hit_text, sizeof g->hit_text, "POSTURE BROKEN"); g->last_hit_text_t = 1.2f; }
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
    camera_set_fixed(&g->cam, &g->level, g->player.c.pos);
    Trigger *t = level_trigger_at(&g->level, g->player.c.pos);
    if (t) {
        if (!strcmp(t->name, "intro")) play_scene(g, ASSET("scenes/intro.txt"), GS_EXPLORE);
        else if (!strcmp(t->name, "boss_door")) play_scene(g, ASSET("scenes/boss_intro.txt"), GS_FIGHT);
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
        if (g->after_scene == GS_FIGHT) {
            g->boss.state = BS_IDLE; g->boss.think = 1.2f;
            character_set_anim(&g->boss.c, ANIM_IDLE);
            g->state = GS_FIGHT;
            camera_set_follow(&g->cam, g->player.c.pos, g->player.c.yaw, g->boss.c.pos, 0, &g->level);
        } else if (g->after_scene == GS_END) {
            g->state = GS_END;
        } else {
            g->boss.state = BS_SCRIPTED;
            g->state = GS_EXPLORE;
            camera_set_fixed(&g->cam, &g->level, g->player.c.pos);
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
    camera_set_follow(&g->cam, g->player.c.pos, g->player.c.yaw, g->boss.c.pos, in->look_x, &g->level);
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
    camera_update(&g->cam, dt);
    bool boss_dead = g->boss.state == BS_DEAD;
    if (boss_dead) {
        audio_set_fight(fmaxf(0, 1.0f - g->state_t));
        if (g->state_t > 1.6f) play_scene(g, ASSET("scenes/victory.txt"), GS_END);
    } else {
        audio_set_fight(fmaxf(0, 1.0f - g->state_t * 0.5f));
        g->fade = fmaxf(0.0f, 1.0f - (g->state_t - 1.5f));
        if (g->state_t > 3.0f) restart_fight(g);
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
    }
    if (level_reload_if_changed(&g->level)) say(g, "level hot-reloaded");

    if (g->paused && !g->step_once) { camera_update(&g->cam, dt); return; }
    g->step_once = false;

    if (g->hitstop > 0) { g->hitstop -= dt; camera_update(&g->cam, dt); return; }

    switch (g->state) {
    case GS_EXPLORE: tick_explore(g, in, dt); break;
    case GS_SCENE:   tick_scene(g, in, dt); break;
    case GS_FIGHT:   tick_fight(g, in, dt); break;
    case GS_DEAD:    tick_dead(g, in, dt); break;
    case GS_END:     tick_end(g, in, dt); break;
    }
    if (g->state != GS_SCENE) {
        g->letterbox = damp(g->letterbox, 0, 6, dt);
        if (g->state != GS_DEAD) g->fade = damp(g->fade, 1, 3, dt);
    }
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
        if (g->last_hit_text_t > 0) text_center(x, W * 0.5f, H * 0.5f - 60, 2.0f, v4(1, 0.9f, 0.6f, fminf(1, g->last_hit_text_t * 2)), g->hit_text);
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

    if (pf->debug) {
        static const char *GS[] = { "EXPLORE", "SCENE", "FIGHT", "DEAD", "END" };
        static const char *PS[] = { "FREE", "ATTACK", "PARRY", "DODGE", "HURT", "DEAD", "SCRIPTED" };
        static const char *BS[] = { "IDLE", "APPROACH", "WINDUP", "ACTIVE", "RECOVER", "STAGGER", "DEAD", "SCRIPTED" };
        char l[8][160]; int n = 0;
        snprintf(l[n++], 160, "fps %.0f  draws %u  tick %u  %s", g->fps, g->gfx.draw_calls, g->tick, g->paused ? "PAUSED" : "");
        snprintf(l[n++], 160, "game %s %.2fs   cam %s  vol %s", GS[g->state], g->state_t, g->cam.mode == CAM_FIXED ? "fixed" : g->cam.mode == CAM_FOLLOW ? "follow" : "scene", g->cam.vol ? g->cam.vol->name : "-");
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
    if (g->time - g->fps_t >= 0.5) { g->fps = (float)(g->frames / (g->time - g->fps_t)); g->frames = 0; g->fps_t = g->time; }

    const Level *lv = &g->level;
    FrameParams fp = {
        .view_proj = camera_view_proj(&g->cam, (float)INTERNAL_W / INTERNAL_H),
        .fog_color = lv->fog_color, .fog_near = lv->fog_near, .fog_far = lv->fog_far,
        .light_dir = v3_norm(lv->light_dir), .ambient = lv->ambient, .light_color = lv->light_color,
    };
    Gfx *x = &g->gfx;
    gfx_begin(x, pf, &fp);
    draw_level(x, lv, &g->wt);
    gfx_set_ambient(x, fmaxf(lv->ambient, 0.5f));   // characters must read against the dark
    draw_character(x, &g->player.c, g->player_def.color, g->player_def.size, false, &g->wt.tex[TEX_PLASTER]);
    draw_character(x, &g->boss.c, g->boss_def.color, g->boss_def.size, true, &g->wt.tex[TEX_METAL]);
    gfx_set_ambient(x, lv->ambient);

    if (pf->debug) {
        // Hitboxes: character capsules, the boss's current move reach, the player's attack reach.
        const Character *pc = &g->player.c, *bc = &g->boss.c;
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
            const Trigger *t = &lv->triggers[i];
            gfx_draw_box_wire(x, v3_scale(v3_add(t->vmin, t->vmax), 0.5f), v3_sub(t->vmax, t->vmin), t->fired ? v4(0.3f, 0.3f, 0.3f, 1) : v4(1, 1, 0.2f, 1));
        }
        for (int i = 0; i < lv->ncams; i++) {
            const CamVolume *c = &lv->cams[i];
            gfx_draw_box_wire(x, v3_scale(v3_add(c->vmin, c->vmax), 0.5f), v3_sub(c->vmax, c->vmin), c == g->cam.vol ? v4(0.2f, 1, 1, 1) : v4(0.2f, 0.4f, 0.5f, 1));
            gfx_draw_box(x, &x->white, c->eye, v3(0.15f, 0.15f, 0.15f), 0, v4(0.2f, 1, 1, 1), 0);
        }
    }
    draw_hud(g, pf);
    PostParams pp = { .grain = 0.07f, .vignette = 0.6f, .fade = g->fade };
    gfx_end(x, pf, &pp, g->time);
}
