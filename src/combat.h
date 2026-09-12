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
    ANIM_DOWN,                       // lying where you fell: a held frame, and never called death
    ANIM_GETUP,                      // pushing back up off the floor
    ANIM_KNOCKED,                    // the moment of going over
    ANIM_GUN_IDLE,                   // a pistol held at the hip
    ANIM_GUN_FIRE,
    ANIM_GUN_RELOAD,
    ANIM_MELEE_IDLE,                 // a bat over the shoulder
    ANIM_MELEE_SWING,
    // --- traversal --- the parkour set. Character files may name these like any other clip; when
    // they do not, charmodel binds them by the names every Quaternius body ships with, and falls
    // back to an ordinary clip when the body has none (see charmodel.c's TRAVERSE_CLIPS).
    ANIM_JUMP,                       // the push-off
    ANIM_FALL,                       // airborne, looping
    ANIM_ROLL,                       // a hard landing carried forward instead of stopped
    ANIM_SLIDE,                      // crouch at a sprint
    ANIM_MANTLE,                     // pulling up onto a ledge
    ANIM_VAULT,                      // over a low obstacle without losing speed
    ANIM_WALLRUN,
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
    float step_dy;                  // metres the feet were snapped this tick by a step up or down; the
                                    // render interpolation and the eye's ease both have to know about it
    int   ground_block;             // which level block is holding them up, -1 = the terrain itself
    Vec3  hvel;                     // ground velocity in XZ, m/s (y unused): Quake-style accel/friction now owns this instead of stepping position directly
    // --- traversal --- The fall the feet have just taken, handed from game.c's grounding to the
    // next player_update: a landing is decided by the movement code (keep the momentum, or roll)
    // but only the grounding code knows it happened. land_impact is consumed and cleared there.
    float fall_from;                // highest y reached since the feet last left the ground
    float land_impact;              // m/s downward at the moment of the last landing, 0 once used
    float land_drop;                // metres fallen to get there
    bool  traversing;               // a scripted traversal owns pos this tick: no gravity, no grounding
    float body_height;              // collision height right now: c->height, or less while sliding
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
    // Ground movement (Half-Life / Quake shaped): accel/air_accel/friction are the coefficients from
    // sv_accelerate/sv_friction, not m/s^2 -- see the accel/friction math in player_update.
    float accel, air_accel, friction, stop_speed;
    float jump_height;               // metres, converted to a launch vy against PLAYER_JUMP_GRAVITY
    float crouch_mult;                // speed multiplier while crouch is held
    // --- traversal --- Mirror's Edge on top of the Quake base: momentum that builds, a jump that
    // forgives, and three scripted moves the world decides for you.
    float sprint_ramp;               // seconds of sprinting to reach the top speed
    float speed_cap;                 // hard ceiling on horizontal speed: bunny hopping stays capped
    float coyote;                    // seconds past the edge a jump still counts
    float jump_buffer;               // seconds a jump press stays queued before the feet land
    float air_wish;                  // m/s the mid-air accel step chases: how much steering a jump has
    float slide_time, slide_decel, slide_enter, slide_exit;   // seconds, m/s^2, and the speeds that open and close it
    float mantle_min, mantle_max;    // the band of ledge heights that can be climbed
    float vault_max;                 // below this, at speed, it is vaulted instead of climbed
    float vault_speed;               // the run needed to go OVER an obstacle under vault_max instead of climbing it
    float roll_fall;                 // metres of drop that turns a landing into a roll
    float wallrun_time;              // seconds a wall run can hold, 0 = no wall running
    float wallrun_speed;             // the run needed to start a wall run
    // A long sprint keeps paying out: momentum (above) gets you to sprint_mult in sprint_ramp
    // seconds, surge is a second, slower ramp on top of that -- surge_ramp seconds of unbroken
    // running for sprint_surge more m/s (Mirror's Edge). sprint_grace is how long the sprint
    // survives a gap in the input (a stick reading zero for a tick, a turn, Shift lifting between
    // presses) before it actually lets go; sprint_decay is how long the built-up momentum/surge
    // then takes to bleed back to zero.
    float sprint_surge, surge_ramp, sprint_grace, sprint_decay;
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

// --- traversal --- What a FREE player is doing with the ground. This is deliberately not a PState:
// the fight's state machine is untouched by it, and a slide or a mantle is something that happens
// *inside* PS_FREE, decided by the world rather than by a button of its own.
typedef enum TravMode { TM_NONE, TM_SLIDE, TM_MANTLE, TM_VAULT, TM_ROLL, TM_WALLRUN } TravMode;
const char *trav_name(TravMode m);

// The world a player moves through: the level's blocks and the terrain under them. A ledge is
// either, so the traversal probe needs both, and passing them together stops player_update growing
// an argument every time it learns to climb something new.
struct Terrain;
typedef struct World { const Level *lv; const struct Terrain *tr; } World;
// Highest walkable surface in this column at or below `ceiling`, terrain and blocks alike; -1e9f
// for a column with nothing in it (outside the terrain grid, no block).
float world_top(const World *w, float x, float z, float ceiling);
// Room for a body of `radius` standing at (x, z) with its feet at y0 and its head at y1.
bool  world_clear(const World *w, float x, float z, float radius, float y0, float y1);
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
    float sprint_hold_t;             // seconds since sprint was last actually engaged (shift + moving);
                                      // inside sprint_grace the latch holds through the gap, past it the
                                      // latch drops and momentum/surge start bleeding off
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
    // --- traversal --- All of this is a function of the inputs and the world, so a client predicts
    // it and the host replays it to the same answer; see the traversal section of combat.c.
    TravMode trav; float trav_t, trav_dur;
    Vec3  trav_from, trav_to;        // the scripted path of a mantle or a vault
    Vec3  trav_dir;                  // the heading the move was entered on
    float trav_speed;                // horizontal speed carried in, handed back on the way out
    float trav_arc;                  // metres the path bulges above the straight line: a vault goes OVER the thing
    Vec3  wall_normal; float wall_side;   // wall run: the face and which shoulder it is on (-1 left, +1 right)
    float momentum;                  // 0..1 sprint build-up, sprint_ramp seconds to the top
    float surge;                     // 0..1 second, slower build on top of momentum: surge_ramp seconds
                                      // of unbroken running at full momentum earns sprint_surge more m/s
    float run_speed;                 // the speed you were doing a moment ago: hvel collapses the tick you
                                      // hit a wall, and "how fast were you going when you hit it" is the
                                      // question a vault and a mantle both have to answer
    float air_t;                     // seconds since the feet last left the ground (coyote time)
    float buf_jump;                  // a jump press still queued (jump_buffer)
    float slide_cd;                  // seconds before crouch can open another slide
    float last_vy;                   // vy at the end of the last tick, for the landing that follows
    unsigned n_mantle, n_vault, n_slide, n_jump, n_roll, n_wallrun;   // what the traversal log counts
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
    // --- traversal --- what the feet and the hands just did, for sound and the viewmodel
    bool  mantle, vault, slide_start, roll, wall_jump;
    float landed;                    // m/s of impact on the tick the feet touched down, 0 otherwise
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
void player_update(Player *p, const Input *in, Vec3 move_dir, const World *w, Boss *boss, float dt, CombatEvents *ev);
// --- traversal --- A correction big enough to be a real disagreement landed mid-climb: move the
// scripted path with it, not the body, so the climb still ends standing on a ledge.
void player_traverse_shift(Player *p, Vec3 delta);
void boss_update(Boss *b, Player *p, const Level *lv, float dt, CombatEvents *ev);

// Scripted motion (cutscenes): walk to a point over dur seconds, then idle.
void character_script_move(Character *c, Vec3 to, float dur);
void character_script_update(Character *c, float dt);
void character_set_anim(Character *c, Anim a);
// Keep two characters from overlapping. The heavier one (by radius) moves less.
void character_separate(Character *a, Character *b, const Level *lv);
// Frontal test used by both sides: is `target` within range and a 100 degree arc of `from`?
bool character_in_arc(const Character *from, Vec3 target, float range);
