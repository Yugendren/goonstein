// Card battle: turn-based, mouse-driven hand, and real-time parries on the enemy's attack clips.
// The player's energy comes from parrying: defence fuels offence.
#pragma once
#include "hmath.h"
#include "platform.h"
#include "gfx.h"
#include "camera.h"
#include "combat.h"
#include "charmodel.h"
#include "particles.h"
#include "uifx.h"

#define CARDS_MAX      32
#define DECK_MAX       40
#define HAND_MAX       10
#define ATTACKS_MAX    12
#define PATTERN_MAX    24
#define HITS_MAX       4
#define FX_MAX         32

typedef enum CardKind { CK_ATTACK, CK_GUARD, CK_DRAW, CK_HEAL, CK_BUFF } CardKind;

typedef struct CardDef {
    char name[32]; char desc[96];
    int cost; CardKind kind;
    int damage, hits, block, draw, heal;
    bool needs_parried; char effect[16];
    Anim anim; Vec3 color;
} CardDef;

typedef struct EnemyAttack {
    char name[32]; char clip[64]; char charge[64];   // clip: model clip or sprite attack anim; charge: sprite telegraph anim
    int damage, hits; float contact[HITS_MAX];
    bool parryable; Vec3 tell; float lead, tail;
} EnemyAttack;

typedef struct EnemyDef {
    char name[64]; int hp;
    EnemyAttack attacks[ATTACKS_MAX]; int nattacks;
    int pattern[PATTERN_MAX]; int npattern;
} EnemyDef;

typedef enum BattleState {
    BT_INTRO, BT_PLAYER, BT_CARD, BT_ENEMY_TELL, BT_ENEMY_ATTACK, BT_ENEMY_RECOVER, BT_WIN, BT_LOSE
} BattleState;

// A card in the hand has physical state: springs pull it toward its fan slot, the hover slot,
// the play point, or the discard pile.
typedef enum CardPhase { CP_DRAWING, CP_HAND, CP_DRAG, CP_PLAYING, CP_DISCARDING } CardPhase;
typedef struct HandCard {
    int def; CardPhase phase; float phase_t;
    float x, y, rot, sc, vx, vy, vrot, vsc;   // position, rotation, scale and their velocities
    float hover;                               // 0..1 smoothed
} HandCard;

typedef enum Judge { J_NONE, J_PERFECT, J_GREAT, J_GOOD, J_MISS } Judge;

typedef struct SpriteFx { bool alive; SpriteActor actor; Vec3 pos; float scale; Vec3 tint; bool flip; } SpriteFx;

typedef struct Battle {
    CardDef cards[CARDS_MAX]; int ncards;
    EnemyDef enemy;
    // piles hold indices into cards[]
    int draw_pile[DECK_MAX]; int ndraw;
    int discard[DECK_MAX]; int ndiscard;
    HandCard hand[HAND_MAX]; int nhand;
    // state
    BattleState state; float t; int round;
    int energy, energy_max, banked;      // banked energy from parries carries into the next turn
    int player_hp, player_hp_max, guard;
    int enemy_hp, enemy_hp_max;
    bool parried_this_round, wide_windows;
    int pattern_i, cur_attack, hit_i; bool hit_done[HITS_MAX]; float hit_t[HITS_MAX];   // game time of each hit
    float parry_pressed_t; bool press_used;   // last parry press and whether a hit consumed it
    // rhythm read
    int   combo, max_combo; Judge last_judge; float judge_t, last_offset;
    float burst_t[HITS_MAX]; Judge hit_judge[HITS_MAX];
    bool dodging; float dodge_t;
    int playing_card; int card_hit_i; bool card_hit_done[HITS_MAX];
    // presentation
    Vec3 player_pos, enemy_pos; float stage_yaw;
    Vec3 cam_eye, cam_target; float cam_fov;          // smoothed
    Vec3 shot_eye, shot_target; float shot_fov;       // current shot
    float timescale, hitstop;
    int hovered; bool end_hover;
    int dragging; float drag_t, drag_x0, drag_y0, drag_mx, drag_my;   // card being dragged, press time, start and current cursor
    float enemy_sx, enemy_sy, player_sx, player_sy; // projected target centres in UI pixels
    int drop_target;                                 // 0 none, 1 enemy, 2 self (while dragging)
    float intent_pulse;
    unsigned parries, perfects, hits_taken;
    char last_read[24]; float last_read_t;
    SpriteDef fxdef; bool fx_loaded; SpriteFx fx[FX_MAX];
} Battle;

bool battle_load(Battle *b, const char *cards_path, const char *deck_path, const char *enemy_path);
void battle_load_fx(Battle *b, Gfx *g, const char *fx_sprite_path);
// Draw world-space battle effects (slashes, sparks). Call inside the world pass after the characters.
void battle_draw_world(Battle *b, Gfx *g);
// Start a battle on the stage: characters face each other across `spacing` metres at `centre`.
void battle_start(Battle *b, Vec3 centre, float stage_yaw, float spacing, int player_hp, int player_hp_max);
// One fixed tick. Drives the character animations, camera, particles, and text effects.
void battle_tick(Battle *b, const Input *in, float mouse_ux, float mouse_uy, float dt,
                 Player *player, Boss *boss, CharModel *pm, CharModel *bm, Camera *cam,
                 Particles *ps, Uifx *fx, CombatEvents *ev);
void battle_draw_ui(const Battle *b, Gfx *g, Mat4 view_proj);
bool battle_over(const Battle *b, bool *won);
// Test harness: plays cards, ends turns, and parries on the beat. Writes into in / mouse coords.
void battle_bot(const Battle *b, const CharModel *bm, Input *in, float *mx, float *my, unsigned tick);
