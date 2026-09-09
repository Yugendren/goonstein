// Combat: player and boss state machines. Everything tunable lives in text files under assets/.
// Timing is in seconds; the sim runs at a fixed 60 Hz so 1 frame = 1/60 s.
#pragma once
#include "hmath.h"
#include "platform.h"
#include "level.h"

// New entries go on the end: the character files and the box-character renderer key off these.
typedef enum Anim {
    ANIM_IDLE, ANIM_WALK, ANIM_ATTACK, ANIM_PARRY, ANIM_PARRY_HIT, ANIM_DODGE, ANIM_HURT,
    ANIM_KNEEL, ANIM_DEAD, ANIM_ROAR, ANIM_STAGGER, ANIM_WINDUP, ANIM_STRIKE, ANIM_RUN, ANIM_ATTACK2, ANIM_ATTACK3,
    ANIM_SPRINT,                     // fastest locomotion clip, blended above run
    ANIM_BLOCK,                      // guard held (looping / holding), as opposed to the deflect tap
    ANIM_HURT_HEAD, ANIM_HURT_HEAVY, // bigger hit reactions, picked by damage size
    ANIM_ATTACK_RUN,                 // sprint attack
    ANIM_COUNT
} Anim;
const char *anim_name(Anim a);
Anim anim_from_name(const char *s);   // ANIM_IDLE if unknown

// Shared body: position, facing, health, and what the renderer needs to pose it.
typedef struct Character {
    Vec3  pos;  float yaw;          // yaw 0 faces +Z, positive turns toward +X
    float radius, height;
    float hp, hp_max, posture, posture_max;
    Anim  anim; float anim_t;       // seconds into the current animation
    float walk_phase;               // leg swing accumulator
    float speed;                    // smoothed planar speed in m/s, what the locomotion blend runs on
    float flash;                    // white hit flash, decays
    float tell;  Vec3 tell_color;   // telegraph glow amount and colour (boss windups)
    int   move_id;                  // which boss move is posing (for windup/strike variants)
    // Vertical state: the ground under the feet is resolved once a tick (game.c's resolve_ground),
    // so a character can stand on a block, a pier or a boat deck and fall off the edge of it.
    float vy;                       // metres per second, negative is falling
    bool  grounded;                 // standing on something this tick
    int   ground_block;             // which level block is holding them up, -1 = the terrain itself
    // Scripted motion (cutscenes)
    bool  scripted_moving; Vec3 move_from, move_to; float move_t, move_dur;
} Character;

typedef struct BossMove {
    char  name[32];
    float windup, active, recovery;  // seconds
    float damage, range;             // range: metres from boss centre, frontal 100 degree arc
    bool  parryable;
    float posture_on_parry;          // posture damage dealt to the boss on a successful parry
    float step;                      // metres the boss lunges forward during active
    Vec3  tell;                      // telegraph colour
    float weight;                    // selection weight
    char  clip[64]; float contact;   // animation clip and the fraction of it where the hit lands
} BossMove;

typedef struct BossDef {
    char  name[64];
    float hp, posture, posture_regen, stagger_time, speed;
    float attack_range;              // approach until this close
    float think_min, think_max;      // pause between moves
    float phase2_hp;                 // fraction of hp; below it windups are faster
    float phase2_windup_mult;        // e.g. 0.7
    float stagger_damage_mult;       // player damage multiplier while staggered
    float combo_chance;              // chance to chain straight into another move after recovery
    Vec3  size, color;
    BossMove moves[16]; int nmoves;
} BossDef;

typedef struct PlayerDef {
    float hp, speed, sprint_mult, turn_speed;
    float attack_windup, attack_active, attack_recovery, attack_damage, attack_range, attack_posture;
    float parry_window, parry_recovery, parry_hitstop;
    float dodge_time, dodge_iframes, dodge_dist;
    float hurt_time;
    // Posture (Sekiro): deflecting costs almost nothing, blocking costs a lot, breaking staggers you.
    float posture, posture_regen, posture_delay;   // max, per second, seconds regen stays paused after damage
    float block_posture;                           // posture spent blocking a hit, as a fraction of its damage
    float deflect_posture;                         // posture spent on a successful deflect
    float stagger_time;                            // how long a posture break leaves you open
    float knockback;                               // metres a heavy hit shoves you back
    Vec3  size, color;
} PlayerDef;

// PS_PARRY covers the whole guard: the deflect window first, then a held block if the button is
// still down. PS_HURT covers both a hit reaction and a posture break (p->staggered).
typedef enum PState { PS_FREE, PS_ATTACK, PS_PARRY, PS_DODGE, PS_HURT, PS_DEAD, PS_SCRIPTED } PState;
typedef enum BState { BS_IDLE, BS_APPROACH, BS_WINDUP, BS_ACTIVE, BS_RECOVER, BS_STAGGER, BS_DEAD, BS_SCRIPTED } BState;

#define PLAYER_SWINGS 4              // three-hit chain plus the sprint attack
#define INPUT_BUFFER  0.2f           // seconds a press stays queued while you are busy

typedef struct Player {
    Character c; PlayerDef def;
    PState state; float t;           // seconds in state
    Vec3  dodge_dir;
    bool  hit_applied;               // attack has already connected this swing (also: whiff logged)
    float step_timer;                // footstep cadence
    int   combo;                     // which swing: 0..2 chain, 3 sprint attack
    float sprint_t;                  // seconds sprint has been held (dodge on tap, sprint on hold)
    // Swing timing read off the animation: swing_lead[i] is the seconds from the start of swing i's
    // clip to the frame the blade lands. Filled in by the game from the character model; 0 = unknown,
    // fall back to def.attack_windup.
    float swing_lead[PLAYER_SWINGS];
    float atk_lead, atk_active, atk_recovery, atk_damage, atk_step;   // the swing in progress
    float buf_attack, buf_parry, buf_dodge;   // input buffer, seconds left on each queued press
    bool  guarding;                  // the deflect window closed with the button still held: blocking
    bool  staggered;                 // this PS_HURT is a posture break
    float regen_delay;               // no posture regen while > 0
    float iframes;                   // invulnerable for this many more seconds (dodge)
    Vec3  knock;                     // knockback velocity, decays
} Player;

typedef struct Boss {
    Character c; BossDef def;
    BState state; float t;
    int   move, last_move;
    bool  hit_applied;
    float think;                     // remaining think time
    float regen_delay;               // no posture regen while > 0
    bool  phase2;
    float strafe_dir;                // -1 / +1: which way it circles between moves
} Boss;

// One-frame events for feedback (sound, shake, hitstop). Cleared by the caller each tick.
typedef struct CombatEvents {
    bool parried, player_hit, boss_hit, boss_staggered, boss_died, player_died;
    bool parry_early, parry_unblockable, parry_whiff;   // failure flavours for feedback
    bool player_blocked, player_staggered;              // guard held the hit / the player's posture broke
    Vec3 contact;                                        // where the last hit or parry happened
    bool player_swing, boss_swing, footstep, boss_footstep, phase2;
    float hitstop, shake;
} CombatEvents;

bool boss_def_load(BossDef *d, const char *path);
bool player_def_load(PlayerDef *d, const char *path);

void player_init(Player *p, const PlayerDef *d, Vec3 pos, float yaw);
void boss_init(Boss *b, const BossDef *d, Vec3 pos, float yaw);
void player_reset(Player *p, Vec3 pos, float yaw);
void boss_reset(Boss *b, Vec3 pos, float yaw);

// move_dir: desired world-space XZ movement (already camera-relative), length 0..1.
// boss may be NULL outside fights.
void player_update(Player *p, const Input *in, Vec3 move_dir, const Level *lv, Boss *boss, float dt, CombatEvents *ev);
void boss_update(Boss *b, Player *p, const Level *lv, float dt, CombatEvents *ev);

// Scripted motion (cutscenes): walk to a point over dur seconds, then idle.
void character_script_move(Character *c, Vec3 to, float dur);
void character_script_update(Character *c, float dt);
void character_set_anim(Character *c, Anim a);
// Keep two characters from overlapping. The heavier one (by radius) moves less.
void character_separate(Character *a, Character *b, const Level *lv);
// Frontal test used by both sides: is `target` within range and a 100 degree arc of `from`?
bool character_in_arc(const Character *from, Vec3 target, float range);
