// Game flow: explore under fixed cameras, cutscenes, one boss fight, death and restart.
#pragma once
#include "platform.h"
#include "gfx.h"
#include "level.h"
#include "camera.h"
#include "scene.h"
#include "combat.h"
#include "render_world.h"
#include "charmodel.h"
#include "props.h"
#include "particles.h"
#include "battle.h"
#include "editor.h"
#include "leveled.h"
#include "widgets.h"

#define INTERNAL_W 1280
#define INTERNAL_H 800

typedef enum GState { GS_EXPLORE, GS_SCENE, GS_FIGHT, GS_DEAD, GS_END, GS_BATTLE, GS_EDITOR } GState;

typedef struct Game {
    double   time; unsigned tick;
    Gfx      gfx; WorldTextures wt;
    Level    level; Camera cam; Scene scene;
    Player   player; Boss boss; PlayerDef player_def; BossDef boss_def;
    CharModel player_model, boss_model;
    PropCache props; Particles particles; Battle battle; Uifx fx; bool battle_loaded;
    Editor editor; bool editor_open;
    LevelEd leveled; bool leveled_ready;
    Ui ui; int tool_mode;            // 0 none, 1 debugger, 2 environment editor, 3 sprite editor
    float sprite_refresh_t;
    struct { char name[32]; Texture tex; } portraits[16]; int nportraits;
    Texture emotes[31];                       // pack emote bubbles by number, 1..30
    float dlg_shown_chars; float dlg_blip_t; char dlg_last_line[200];
    char hero_config[128];        // override for assets/characters/<name>.txt
    GState   state, after_scene; float state_t;
    float    hitstop, letterbox, fade, fight_intensity;
    bool     paused, step_once;
    char     msg[128]; float msg_t;
    // stats
    float    fps; unsigned frames; double fps_t;
    float    last_hit_text_t; char hit_text[32];
    unsigned parries, hits_taken, deaths;
    // feedback
    Vec3  flash_color; float flash;
    float hint_t;
    bool     bot;                 // test harness: plays the fight by itself
    char     level_path[512];     // override (harness --level)
    Platform *pf;
} Game;

void game_init(Game *g);
bool game_init_gfx(Game *g, Platform *pf);
void game_tick(Game *g, const Input *in, double dt);
void game_render(Game *g, Platform *pf, float alpha);
void game_shutdown(Game *g);
void game_screenshot(Game *g, const char *path);
// Test harness: jump to a state ("explore", "fight", "end").
void game_start_at(Game *g, const char *where);
// Harness: true once when the named battle moment is on screen (ring closing, a judgement burst, a card in flight).
bool game_shot_moment(Game *g, const char *when);
// Open the sprite editor on a character (creates it if missing).
void game_open_editor(Game *g, const char *name, int frame_size);
// Switch the tool window: 0 closes, 1 debugger, 2 environment editor, 3 sprite editor.
void game_set_tool(Game *g, int mode);
