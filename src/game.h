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
#include "leveled.h"
#include "builder.h"
#include "terrain.h"
#include "widgets.h"
#include "netgame.h"
#include "menu.h"   // --- menu --- the main menu, the Esc menu and the player list
#include "phys.h"
#include "items.h"
#include "weapons.h"

#define INTERNAL_W 1280
#define INTERNAL_H 800

// GS_MENU is the main menu at launch: the island renders behind it and nobody is being played.
typedef enum GState { GS_EXPLORE, GS_SCENE, GS_FIGHT, GS_DEAD, GS_END, GS_BATTLE, GS_MENU } GState;

// The local player and its model. Slots 1..3 are filled by joining clients.
#define PLAYER(g)       ((g)->players[(g)->local])
#define PLAYER_MODEL(g) ((g)->player_models[(g)->local])

// A level prop a cutscene drives like an actor (`actor prop:NAME move|teleport|face ...`). Anyone
// standing on its deck rides along: see game.c's prop_move_to.
#define GAME_PROP_ACTORS 8
typedef struct PropActor { int prop; bool moving; Vec3 from, to; float t, dur; } PropActor;

typedef struct Game {
    double   time; unsigned tick;
    Gfx      gfx; WorldTextures wt;
    Level    level; Camera cam; Scene scene;
    Player players[NET_MAX_PLAYERS]; int local;    // every seated player; `local` is the one this process drives
    Boss boss; PlayerDef player_def; BossDef boss_def;
    CharModel player_models[NET_MAX_PLAYERS], boss_model;
    PropCache props; Particles particles; Battle battle; Uifx fx; bool battle_loaded;
    PhysWorld phys; Items items;   // M2: rigid bodies and the loot that rides on them
    Weapons  weapons;              // --- weapons --- what is in the other hand, and who is on the floor
    LevelEd leveled; bool leveled_ready;
    Builder builder; bool builder_ready;
    Terrain terrain; bool gen_done;
    struct { Character c; CharModel model; bool ok; } npcs[LEVEL_MAX_NPCS]; int nnpcs; int talk_npc;   // talk_npc: the one in reach (-1 none)
    PropActor prop_actors[GAME_PROP_ACTORS]; int nprop_actors;   // props a scene is driving right now
    float daytime_from, daytime_to, daytime_t, daytime_dur;   // scene-driven time of day transition
    Menu menu;   // --- menu --- which page is up, the text fields, the LAN addresses
    Ui ui; int tool_mode;            // 0 none, 1 debugger, 2 environment editor, 3 sprite editor
    float sprite_refresh_t;
    struct { char name[32]; Texture tex; int model; } portraits[16]; int nportraits;   // model: 0 image, 1 hero, 2 boss
    AnimPlayer portrait_player; ModelPose portrait_pose; char portrait_emote[16]; float portrait_start; int portrait_model;
    Texture emotes[31];                       // pack emote bubbles by number, 1..30
    float dlg_shown_chars; float dlg_blip_t; char dlg_last_line[200];
    char hero_config[128];        // override for assets/characters/<name>.txt
    GState   state, after_scene; float state_t;
    float    hitstop, letterbox, fade, fight_intensity;
    bool     paused, step_once;
    bool     look_capture;   // the game window owns the mouse: the frame-rate view may turn
    float    crouch_k;       // 0..1 eased crouch, which is an eye height rather than a body height
    float    slide_k;        // --- traversal --- 0..1 of the way from a crouched eye to a sliding one
    char     msg[128]; float msg_t;
    // stats
    float    fps; unsigned frames, frames_total; double fps_t; float frame_ms;
    double   frame_wall;   // seconds, stamped at the top of the frame loop: the clock the trace measures pacing against
    float    render_alpha, render_frame_dt;   // where this frame sits between the last two ticks, and how long the last frame took   // frame_ms: smoothed render+present time
    Vec3 prev_players[NET_MAX_PLAYERS], prev_boss, prev_eye, prev_target; bool prev_valid;   // previous tick, for render interpolation
    float    last_hit_text_t; char hit_text[32];
    unsigned parries, hits_taken, deaths;
    // feedback
    Vec3  flash_color; float flash;
    float hint_t;
    Look look_authored; int look_cycle;   // F7 art-style preview over the level's own look
    bool     bot;                 // test harness: plays the fight by itself
    char     level_path[512];     // override (harness --level)
    Platform *pf;
    NetGame  net;
    bool     no_scenes;           // multiplayer: skip cutscene playback from triggers and NPC talk
    bool     force_third;         // --third: force third-person view
    bool     force_first;         // --first: force first-person view
    bool     slot_tinted[NET_MAX_PLAYERS];   // this slot fell back to hero.txt, so the slot colour is what tells it apart
    char     log_path[256];       // --log FILE (default hollow.log)
    char     test_mode[32];       // --test NAME: a scripted headless check ("throw")
} Game;

void game_init(Game *g);
bool game_init_gfx(Game *g, Platform *pf);
void game_tick(Game *g, const Input *in, double dt);
// Once per RENDERED FRAME, before the ticks: applies this frame's mouse delta to the view and
// advances everything that hangs off the head at the frame rate (bob, step-up ease, landing dip).
void game_view_look(Game *g, Platform *pf, float dt);
void game_render(Game *g, Platform *pf, float alpha);
// Render-rate helpers for anything drawn between two simulation ticks (a held item, the viewmodel).
// The alpha is the same one game_render interpolates the players with, so everything drawn against
// it moves together; the frame dt is the wall time of the frame being drawn, which is what a
// frame-rate spring or bob has to integrate against.
float game_render_alpha(const Game *g);
float game_frame_dt(const Game *g);
void game_shutdown(Game *g);
// Loads the hero character model into player_models[slot] if not already loaded.
void game_ensure_player_model(Game *g, int slot);
// Resolve one character's footing against the terrain, the level's blocks and any prop deck:
// step up onto what is within LEVEL_STEP_UP of the feet, fall with gravity off anything else.
// dt <= 0 snaps straight to the ground (spawns, teleports, level loads).
void game_ground_character(Game *g, Character *c, float dt);
// Puts players[slot] at the level spawn, spread out.
void game_spawn_player(Game *g, int slot);
// --- loadouts --- The weapon this slot's character file asks for with `spawn ITEM`: the item is
// created (with the slot's fixed id, so every side has it) and, on the host, put in the weapon
// hand. A no-op for a character with no `spawn` line, a hand that is already full, or a slot whose
// item exists already. Called wherever a slot is seated: solo start, host start, a client joining.
void game_give_loadout(Game *g, int slot);
// Re-seat the camera on the local player for the level's view mode (spawns, restarts, joining).
void game_snap_camera(Game *g);
// settings.txt: replace or add one `key value` line, keeping the rest (comments included).
void game_settings_set(Game *g, const char *key, const char *value);
void game_screenshot(Game *g, const char *path);
void game_tool_screenshot(Game *g, const char *path);
// Test harness: jump to a state ("explore", "fight", "end").
void game_start_at(Game *g, const char *where);
// Harness: true once when the named battle moment is on screen (ring closing, a judgement burst, a card in flight).
bool game_shot_moment(Game *g, const char *when);
// Open the sprite editor on a character (creates it if missing).
// Switch the tool window: 0 closes, 1 debugger, 2 world editor (terrain, place, look), 4 character builder.
void game_set_tool(Game *g, int mode);

// --- traversal --- src/travbot.c: a bot that runs the parkour course so a headless capture and the
// log show sprinting, mantling and sliding with nobody at the keyboard. HOLLOW_BOT=traverse.
bool traverse_bot_input(struct Game *g, struct Input *in);
void traverse_bot_log(struct Game *g, float dt);
