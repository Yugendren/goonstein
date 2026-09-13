// --- boss --- The cave boss: one big thing, four goons, and the host deciding all of it.
//
// This is not the Warden. The Warden (src/combat.c) is a duel: one player, one boss, posture,
// parries, and a camera locked on the pair of them. This one is a Doom fight. There are up to four
// goons with guns spread over a cavern full of platforms, the boss picks which of them it hates
// most, and everything it throws is aimed at a place rather than at a person -- a line down the
// floor, a ring coming out of the ground, five slow fireballs. You do not parry any of it. You get
// out of the way, and the way you get out of the way is the movement: a jump clears the shockwave,
// a platform breaks the charge's line, a pillar eats the volley.
//
// WHAT IS REUSED. BossDef, BossMove, Boss and BState come straight out of combat.h, and the shape
// of the state machine below (think -> approach -> windup -> active -> recover) is the one
// boss_update already had. That is deliberate: it means charmodel_drive_boss animates this boss
// with no changes at all, so a move's windup clip is fitted to the windup the text file asks for
// and the telegraph is the animation rather than something drawn over it. What is new is the move
// KINDS (BossMoveKind in combat.h), multiple targets, damage-driven stagger instead of posture,
// and the fact that every bit of it is host-authoritative.
//
// AUTHORITY. The host runs the whole thing. Clients run nothing: they receive the boss's position,
// yaw, health, animation, move id, state, phase, telegraph fill and shockwave radius in a section
// of the ordinary entity snapshot, and they render telegraphs out of that. They never decide that
// anything was hit. The one thing a client does simulate is the look of it -- the white flash, the
// damage numbers, the ring expanding between two snapshots -- because those are pictures of
// decisions the host has already made and sent.
//
// The events (FE_BOSS_ROAR, FE_BOSS_HIT, FE_BOSS_STAGGER, FE_BOSS_DIE, FE_HURT) ride the weapons
// event channel that already exists, because they are exactly what that channel is for: one frame
// of feedback, no state, and a lost one is simply not seen.
#pragma once
#include "hmath.h"
#include "combat.h"
#include "charmodel.h"
#include "gfx.h"
#include <stdint.h>

struct Game;
struct FireEvent;

// The ring a slam throws out. Half a metre high on purpose: the jump in assets/player.txt clears
// 1.1 m, so jumping it is a real answer and standing still is not.
typedef struct BossShock {
    bool  live;
    Vec3  at;                          // where it came out of the ground
    float r, r_max, speed, height, damage, knock;
    bool  caught[4];                   // this player has already been swept this ring (NET_MAX_PLAYERS)
} BossShock;

// How much of the fight is worth writing down at the end.
typedef struct BossLog {
    float dealt, taken;                // damage the goons did, and took from the boss
    unsigned downs, staggers, hits, moves[BMK_KIND_COUNT];
    double  started, ended;            // game time
} BossLog;

typedef struct CaveBoss {
    bool  active;                      // this level named a boss and it has been set up
    Boss  b;                           // body, def and animation state; a Boss so charmodel_drive_boss works
    CharModel model; bool model_ok;
    int   fireball_def;                // items.defs index of the thing a volley throws, -1 if missing

    // who it is angry at
    int   target; float retarget_t;
    float threat[4];                   // damage each slot has done lately; decays, so it follows the fight

    // stagger: this boss has no guard to break, so 120 damage inside two seconds stops it instead
    float stag_acc, stag_win, stag_t;

    // the move in progress
    Vec3  charge_dir; float charge_left, charge_speed;
    BossShock shock;
    int   volley_left; float volley_t;
    float cd[16];                      // per-move cooldowns, indexed like def.moves

    // death
    float death_t; bool loot_dropped, death_fx;

    // solo: nobody to revive you, so you wake up at the door and the boss keeps its health
    float solo_down_t;

    // local look, not replicated: the white flash and the telegraph the client draws
    float tell_k;                      // 0..1 fill of whatever windup is running
    int   tell_move;                   // which move that is, -1 for none
    BossLog log;
} CaveBoss;

// ---- lifetime --------------------------------------------------------------------------------
// Called from setup_level_content on every machine. Tears down whatever the last level had and,
// if this level's `boss_def NAME` line names one, loads assets/enemies/NAME.txt and its character.
void boss_level_changed(struct Game *g);
void boss_shutdown(struct Game *g);
bool boss_alive(const struct Game *g);     // present, set up, and not dead
bool boss_present(const struct Game *g);   // present and set up, dead or not

// ---- the tick --------------------------------------------------------------------------------
// Host: the whole state machine, the shockwave, the volley, the damage. Client: the flash, the
// ring's growth between snapshots, and the animation. Called from tick_explore after weapons_tick.
void boss_tick(struct Game *g, float dt);

// ---- taking damage ---------------------------------------------------------------------------
// Host only. `by_slot` is who did it (for the threat table and the hit marker), `at` where.
// Applies the stagger multiplier, opens the stagger window, and queues the FE_BOSS_HIT everyone
// needs to see it. Anything that can hurt the boss goes through here.
void boss_hurt(struct Game *g, int by_slot, float damage, Vec3 dir, Vec3 at);
// The boss's hit box for a ray: a fat cylinder, the same approximation weapons_trace uses on a
// goon. False when there is no boss, it is dead, or the ray misses inside `max_dist`.
bool boss_ray(const struct Game *g, Vec3 from, Vec3 dir, float max_dist, float *out_dist, Vec3 *out_point);

// ---- drawing ---------------------------------------------------------------------------------
void boss_draw(struct Game *g);              // the model, its telegraph decals and the shockwave
void boss_draw_shadow(struct Game *g);       // the model only, into the sun's shadow pass
void boss_draw_hud(struct Game *g);          // name, health bar, PHASE II and STAGGER
int  boss_lights(const struct Game *g, PointLight *out, int max);   // the telegraph's own glow

// ---- network ---------------------------------------------------------------------------------
#define BOSS_SNAP_BYTES 18   // one flag byte, six of position, two of yaw, two of health, and seven more
// Host: append the boss section to a snapshot. Writes exactly one byte (0) when there is no boss.
void boss_net_write(const struct Game *g, void *netbuf);
// Client: read it back. `netbuf` is the same NetBuf the rest of the snapshot is being read from.
void boss_net_read(struct Game *g, void *netbuf);
// Everyone: one frame of feedback out of the event channel. Returns false for an event that is
// not one of the boss's, so weapons_event_apply can fall through to its own kinds.
bool boss_event_apply(struct Game *g, const struct FireEvent *e);
// Feedback for a goon who has just been hurt, by anything at all: the red directional vignette,
// the shake, the FOV pull, the sound. Queued by the host from weapons_hurt_player so a bullet in
// the back reads the same as a shockwave.
void boss_hurt_fx(struct Game *g, int slot, float damage, Vec3 from);

// ---- the fight summary -----------------------------------------------------------------------
void boss_report(const struct Game *g);   // one block of lines at shutdown, and after a win

// --- boss --- Harness: is the named moment of the fight on screen right now? Used by
// `--shot-when NAME --screenshot FILE`, so every frame this feature has to be able to show can be
// captured by a command instead of by scrubbing a thousand PNGs. Names:
//   tell:charge tell:slam tell:volley tell:sweep   a windup, two thirds of the way through
//   shock                                          the shockwave crossing the floor
//   hurt                                           the local goon has just been hit
//   stagger                                        the stagger window, with the numbers still up
//   death                                          it going over
//   arena                                          the boss alive, upright and on screen
bool boss_shot_moment(const struct Game *g, const char *when);

// ---- bot (src/bossbot.c) ---------------------------------------------------------------------
// HOLLOW_BOT=boss: walks out of the island's culvert, through the door, and fights. Returns true
// when it is driving.
bool boss_bot_input(struct Game *g, struct Input *in);
