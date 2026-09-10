// Weapons: what a goon can carry in the other hand, and what happens to whatever it points at.
//
// A weapon is an ordinary item (assets/items/NAME.txt) with a `weapon melee|gun` line. Picking one
// up with E puts it in the weapon hand instead of the loot spring: no physics body, no swinging on
// a string, just a model on the end of your arm. The loot hand keeps working exactly as it did, so
// a pistol in one hand and a painting in the other is a legal and very stupid way to travel.
//
// Nobody dies. A goon who runs out of wind falls over, lies there swearing, and is back up in six
// seconds -- or in one and a half if a mate stands over them and holds E. Friendly fire is always
// on because that is the joke.
//
// Authority: the host owns every shot. A client predicts its own kick, sound and tracer and sends
// a reliable FIRE with the eye and aim it fired from; the host re-runs the hitscan from its own
// copy of the world, validates range and rate, and tells everyone what happened with a small
// unreliable event. Knockdown lives in the player snapshot. See README.md, "Weapons".
#pragma once
#include "hmath.h"
#include "items.h"
#include "netgame.h"
#include <stdint.h>

struct Game;
struct Input;

typedef enum WeaponKind { WK_NONE = 0, WK_MELEE = 1, WK_GUN = 2 } WeaponKind;

// Tuning. Everything per-weapon lives in the item file (damage, rate, range, ammo, knock); these
// are the rules of the game rather than of any one gun.
#define WEAP_RELOAD_TIME   1.2f    // seconds, every gun
#define WEAP_SWING_TIME    0.35f   // seconds of melee swing
#define WEAP_SWING_ARC     1.6f    // metres a swing reaches
#define WEAP_WIND          100.0f  // how much punishment a goon absorbs before falling over
#define WEAP_WIND_REGEN    12.0f   // per second, once nothing has hit them for a moment
#define WEAP_WIND_DELAY    2.5f    // seconds of quiet before wind comes back
#define WEAP_DOWN_TIME     6.0f    // seconds flat on your back
#define WEAP_REVIVE_HOLD   1.5f    // seconds of a mate holding E to cut that short
#define WEAP_REVIVE_REACH  2.2f    // metres they have to stand within
#define WEAP_DOWN_EYE      0.30f   // eye height while down, metres
#define WEAP_DOWN_ROLL     70.0f   // degrees the camera lies over
#define WEAP_MAX_RANGE     60.0f   // hitscan ceiling, whatever the item file says

// One player's weapon hand.
typedef struct Weapon {
    int   item;          // item index, -1 = empty handed
    bool  drawn;         // in the hands (viewmodel, fires) vs holstered on the back
    int   ammo;          // rounds in the gun
    float cool;          // seconds until the next shot is allowed
    float reload;        // seconds left reloading, 0 = not
    float swing;         // seconds left in a melee swing, 0 = not swinging
    bool  swing_hit;     // this swing has already connected with something
    float kick;          // recoil 0..1, decays: drives the viewmodel and a little aim rise
    float flash;         // muzzle flash 0..1, decays over ~0.06 s: a light and a puff
    float wind;          // 0..WEAP_WIND; empty means down
    float wind_quiet;    // seconds since the last hit, for the regen delay
} Weapon;

// Knocked down. Not dead, never called death, and there is no gore anywhere near it.
typedef struct Downed {
    bool  down;
    float t;             // seconds spent down
    float getup;         // seconds left in the get-up animation, 0 = not getting up
    float revive;        // seconds a mate has held E on this one
    int   reviver;       // the slot doing it, -1
} Downed;

// One thing everyone has to see. Host -> clients as an unreliable payload; also applied locally by
// whoever caused it so a client sees its own shot on the frame it pressed the button.
enum { FE_SWING = 0, FE_SHOT = 1, FE_CLICK = 2, FE_RELOAD = 3, FE_DOWN = 4, FE_UP = 5 };
enum { FH_NONE = 0, FH_ITEM = 1, FH_PLAYER = 2, FH_WORLD = 3 };
typedef struct FireEvent {
    uint8_t slot, kind, hit, pellets;   // pellets: 1 for a pistol, several for a shotgun
    Vec3    from, to;                   // tracer ends; from == to for anything that is not a shot
} FireEvent;

#define WEAP_EVENTS  24
#define WEAP_TRACERS 24
#define WEAP_FLASHES 8

typedef struct Weapons {
    Weapon w[NET_MAX_PLAYERS];
    Downed dn[NET_MAX_PLAYERS];
    // local view state (not replicated)
    float  bob;                     // viewmodel bob phase
    float  sway_x, sway_y;          // viewmodel lag behind the mouse
    float  swap_t;                  // seconds into a draw/holster
    float  prompt_t;
    char   prompt[64];              // "E   pick up the bat" / "HOLD E   PICK UP DEZ"
    // effects, local only
    struct { Vec3 a, b; float life, max_life; } tracer[WEAP_TRACERS]; int ntracer;
    struct { Vec3 at; float life; } flash[WEAP_FLASHES]; int nflash;
    // events queued by the host this tick, drained into the outgoing packet
    FireEvent ev[WEAP_EVENTS]; int nev;
    // counters for the log line and the closing report
    unsigned shots, hits, misses, knockdowns, revives, reloads, swings;
} Weapons;

// ---- lifetime and the tick -----------------------------------------------------------------
void weapons_reset(struct Game *g);
// One tick: local input (fire, reload, swap, revive), host authority, timers, effects. Called from
// game.c's explore tick after items_tick, so the item the player just grabbed is already in hand.
void weapons_tick(struct Game *g, const struct Input *in, float dt);
// World pass: the local viewmodel, every remote goon's held or holstered weapon, tracers, flashes.
void weapons_draw(struct Game *g);
// Lights the muzzle flash contributes this frame. Returns how many were written (0..2).
int  weapons_lights(const struct Game *g, PointLight *out, int max);
// Screen: ammo, the DOWN line, the pick-up prompt.
void weapons_draw_hud(struct Game *g);
// Effects bookkeeping: tracers and muzzle flashes fading out. Called by weapons_tick.
void weapons_fx_tick(struct Game *g, float dt);

// ---- what the rest of the game needs to ask -------------------------------------------------
WeaponKind weapon_kind_of(const struct Game *g, int item);       // WK_NONE for ordinary loot
bool weapons_is_down(const struct Game *g, int slot);
// A weapon in the hands rather than on the back: it fires, it is drawn as a viewmodel, and it
// takes the left mouse button off the loot hand's throw charge.
bool weapons_drawn(const struct Game *g, int slot);
// The grip of the weapon hand in world space, off the character's hand_r bone when there is one.
Vec3 weapons_hand_point(const struct Game *g, int slot);
// The eye a slot shoots from and the direction it is aiming.
Vec3 weapons_eye(const struct Game *g, int slot);
Vec3 weapons_aim(const struct Game *g, int slot);
// While down you cannot move, look, fire or grab. The camera still works; it is just on its side.
bool weapons_frozen(const struct Game *g, int slot);
// Camera override for a downed local player: roll in degrees and the eye height to use.
// Returns false when the player is upright and the caller should do what it always did.
bool weapons_camera(const struct Game *g, float *roll_deg, float *eye_height);
// items.c asks this before running the carry spring: a weapon is not on a string.
bool weapons_holds_item(const struct Game *g, int slot, int item);

// ---- equipping ------------------------------------------------------------------------------
// E on a weapon. Host (and single player) call it directly; a client predicts it and lets the
// snapshot correct it. Returns false when the hand is full or the item is not a weapon.
bool weapons_equip(struct Game *g, int slot, int item);
void weapons_drop(struct Game *g, int slot);                // put it back on the floor with a body
void weapons_swap(struct Game *g, int slot);                // Q / scroll: draw or holster

// ---- network --------------------------------------------------------------------------------
// Host side, from a client's reliable message.
void weapons_net_fire(struct Game *g, int slot, Vec3 origin, Vec3 dir);
void weapons_net_reload(struct Game *g, int slot);
void weapons_net_swap(struct Game *g, int slot);
void weapons_net_revive(struct Game *g, int slot, int target, bool holding);
// The same, called directly by a host that is also a player rather than arriving over the wire.
void weapons_net_revive_local(struct Game *g, int slot, int target, bool holding);
// Client: a snapshot says this slot is holding this item as a weapon. Idempotent; it is how a
// remote goon's bat gets into weapons.w[slot] without a message of its own.
void weapons_client_hold(struct Game *g, int slot, int item);
// Snapshot: three bytes per player (flags, ammo, wind). Bits, low to high:
// 0 down, 1 getting up, 2 weapon drawn, 3..4 kind (0 none, 1 melee, 2 gun), 5 reloading, 6 swinging.
uint8_t weapons_pack_flags(const struct Game *g, int slot);
void    weapons_apply_flags(struct Game *g, int slot, uint8_t flags, uint8_t ammo, uint8_t wind);
// Events. The host queues; netgame drains into the packet; everyone applies what arrives.
void weapons_event(struct Game *g, const FireEvent *e);      // queue (host) and play locally
void weapons_event_apply(struct Game *g, const FireEvent *e);// play only (a client receiving one)
int  weapons_events_take(struct Game *g, FireEvent *out, int max);   // host: drain the queue
// Client -> host senders live in netgame.c: netgame_send_weapon_fire / _reload / _swap / _revive.

// ---- bot (weaponbot.c) ----------------------------------------------------------------------
// HOLLOW_BOT=shoot: walk to a weapon, pick it up, and shoot the nearest other player or item every
// few seconds. Writes into `in`; returns true when it is driving.
bool weapons_bot_input(struct Game *g, struct Input *in);
