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
    float pending;       // client: seconds a predicted equip, drop or shot is still in flight, during
                         // which a snapshot older than the round trip must not walk it back
    int   pose; float pose_t;   // the animation the weapon hand is asking for, and its own clock:
                                // see weapons_pose. ANIM_COUNT means "leave the locomotion alone"
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
enum { FE_SWING = 0, FE_SHOT = 1, FE_CLICK = 2, FE_RELOAD = 3, FE_DOWN = 4, FE_UP = 5,
       // --- projectiles --- a detonation: `from` is the centre and `pellets` is the blast radius
       // in tenths of a metre, which is all a client needs to draw the same bang in the same place.
       FE_BOOM = 6,
       // --- projectiles --- something that was fired a moment ago has just arrived. A hitscan
       // shot knows what it hit on the frame the trigger goes down; a bullet with travel time does
       // not, so the host sends this after the fact and the shooter gets a hit marker out of it.
       // It carries feedback only -- the damage was applied on the host, and the dust and the thud
       // were played by each machine's own copy of the projectile when it died.
       FE_IMPACT = 7 };
enum { FH_NONE = 0, FH_ITEM = 1, FH_PLAYER = 2, FH_WORLD = 3 };
typedef struct FireEvent {
    uint8_t slot, kind, hit, pellets;   // pellets: 1 for a pistol, several for a shotgun
    Vec3    from, to;                   // tracer ends; from == to for anything that is not a shot
} FireEvent;

#define WEAP_AMMO_POOLS 6   // distinct kinds of round one goon can be carrying at once
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
    // --- viewmodel --- Everything the first-person gun does that nobody else can see. Recoil is
    // a spring rather than a ramp: a shot hands the gun a velocity and the spring brings it home,
    // so a second shot fired before the first has settled stacks on top of it instead of
    // restarting the animation. See weaponview.c and docs/weapons_feel.md.
    struct {
        float back, up, side;              // metres of position kick, and their velocities
        float back_v, up_v, side_v;
        float pitch, yaw, roll;            // degrees of rotation kick, and their velocities
        float pitch_v, yaw_v, roll_v;
        float flash;                       // seconds left on the first-person muzzle flash
        float flash_seed;                  // so two flashes running are not the same shape
        float sprint;                      // 0..1, how far the gun is lowered and turned for a run
        float bloom;                       // crosshair bloom, 0..1, decays with the kick
        float hitmark; bool hitmark_solid; // hit marker timer, and whether it was a goon
        unsigned shots;                    // so the sideways kick can alternate rather than drift
        // Spent cases, in camera-local metres (right, up, forward). They live under a second and
        // never leave the frame, so there is nothing to gain by putting them in the world -- and
        // a lot to lose, because the world is drawn through a different lens than this is.
        struct { Vec3 pos, vel; float spin, roll, life; } shell[12]; int nshell;
    } vm;
    float  prompt_t;
    char   prompt[64];              // "E   pick up the bat" / "HOLD E   PICK UP DEZ"
    // effects, local only
    struct { Vec3 a, b; float life, max_life; } tracer[WEAP_TRACERS]; int ntracer;
    // A muzzle flash is two things: a billboard out in the world and a point light. `world_vis`
    // turns the billboard off for the local player's own shot in first person, because that one is
    // drawn at the model's real muzzle inside the viewmodel pass instead -- the light still counts.
    struct { Vec3 at; float life; bool world_vis; } flash[WEAP_FLASHES]; int nflash;
    // --- ammo --- What each goon is carrying beyond what is in the gun, one entry per kind of
    // round. Keyed by the same one-byte hash of the `ammo_type` name that names a projectile on the
    // wire, so a pool is the same pool in every process and the name itself is never sent. A `type`
    // of 0 is an empty slot; an entry that exists with n == 0 means "carried this, spent it all",
    // which is a different thing from "never picked one up" and is why the slot is not freed.
    struct { uint8_t type; uint16_t n; } pool[NET_MAX_PLAYERS][WEAP_AMMO_POOLS];
    // events queued by the host this tick, drained into the outgoing packet
    FireEvent ev[WEAP_EVENTS]; int nev;
    // counters for the log line and the closing report
    unsigned shots, hits, misses, knockdowns, revives, reloads, swings;
} Weapons;

// ---- lifetime and the tick -----------------------------------------------------------------
void weapons_reset(struct Game *g);
// --- projectiles --- Two lines at shutdown: what the guns did, and what left them. Called from
// the same place the net and physics reports are.
void weapons_report(const struct Game *g);
// One tick: local input (fire, reload, swap, revive), host authority, timers, effects. Called from
// game.c's explore tick after items_tick, so the item the player just grabbed is already in hand.
void weapons_tick(struct Game *g, const struct Input *in, float dt);
// World pass: the local viewmodel, every remote goon's held or holstered weapon, tracers, flashes.
void weapons_draw(struct Game *g);
// --- viewmodel --- The first-person gun, its hands, its flash and its spent cases, drawn LAST in
// the world pass through their own narrow lens and their own slice of the depth buffer. Separate
// from weapons_draw because it has to happen after everything else the world draws: see
// docs/weapons_feel.md, "The gun is not in the world".
void weapons_draw_viewmodel(struct Game *g);
// --- viewmodel --- Load the first-person arms during the tick rather than during a frame, so the
// buffer upload and the texture decode do not land in the middle of one. Idempotent and cheap.
void weapons_warm_viewmodel(struct Game *g);
// Lights the muzzle flash contributes this frame. Returns how many were written (0..2).
int  weapons_lights(const struct Game *g, PointLight *out, int max);
// Screen: ammo, the DOWN line, the pick-up prompt.
void weapons_draw_hud(struct Game *g);
// Effects bookkeeping: tracers and muzzle flashes fading out. Called by weapons_tick.
void weapons_fx_tick(struct Game *g, float dt);
// Write the weapon hand's pose onto every character it owns. player_update decides idle / walk /
// run from the movement every tick, which is the right answer for empty hands and the wrong one
// for a goon lying on the floor or holding a pistol at the hip, so this runs after it -- both from
// weapons_tick for the local player and from netgame.c for the remote ones the host simulates
// later in the same tick. Idempotent: calling it twice in a tick costs nothing.
void weapons_pose(struct Game *g);

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

// ---- ammo ------------------------------------------------------------------------------------
// Rounds this goon is carrying for `type` ("pistol", "rifle", ...), outside the gun. 0 for a type
// they have never picked up, which is the same number as one they have run dry.
int  weapons_reserve(const struct Game *g, int slot, const char *type);
// Put rounds into a pool, creating it if this is the first box of that kind. Returns how many
// actually went in (a pool is capped, so walking over the tenth box of pistol rounds is allowed to
// be worth nothing). Host and single player; a client waits for the snapshot.
int  weapons_reserve_add(struct Game *g, int slot, const char *type, int n);
// What the HUD shows next to the magazine: the reserve for whatever is in this slot's hands, or
// -1 when that is a bat, a fist, or a gun whose item file names no ammo_type.
int  weapons_reserve_held(const struct Game *g, int slot);

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
// --- ammo --- A fourth byte: the reserve for whatever that slot is holding, clamped to 255. It is
// one byte a player a snapshot -- 120 B/s for a full game -- to save a client guessing at a number
// the host has already decided, and it is what makes the "12 / 96" on the HUD true rather than hopeful.
uint8_t weapons_pack_reserve(const struct Game *g, int slot);
void    weapons_apply_reserve(struct Game *g, int slot, uint8_t reserve);
// Events. The host queues; netgame drains into the packet; everyone applies what arrives.
void weapons_event(struct Game *g, const FireEvent *e);      // queue (host) and play locally
void weapons_event_apply(struct Game *g, const FireEvent *e);// play only (a client receiving one)
// The host's answer to a shot this client already played for itself. Everything in it has been
// seen once already except what only the host could know -- what it hit -- so this plays that and
// nothing else, and is why a client gets a hit marker at all.
void weapons_event_own_echo(struct Game *g, const FireEvent *e);
int  weapons_events_take(struct Game *g, FireEvent *out, int max);   // host: drain the queue
// Client -> host senders live in netgame.c: netgame_send_weapon_fire / _reload / _swap / _revive.

// ---- tracing --------------------------------------------------------------------------------
// --- projectiles --- The first thing a ray meets: the level's blocks, the terrain, the other
// goons (fat cylinders) and the loose items (spheres), in that order, nearest wins. `shooter` is
// skipped, and so is whatever the shooter is carrying. This is the hitscan the pistol uses; a
// projectile sweeps its own segment through the same function every tick, so a nail and a bullet
// agree about what a wall is.
typedef struct WeapHit { int kind, idx; Vec3 point, normal; float dist; } WeapHit;
WeapHit weapons_trace(struct Game *g, int shooter, Vec3 from, Vec3 dir, float range);

// ---- damage, for anything that is not a shot ------------------------------------------------
// --- projectiles --- The host applying a hit that a hitscan ray did not make: a nail arriving, a
// grenade going off nearby. Same wind pool, same knockdown, same no-gore rules as a bullet, so
// there is exactly one place that decides what being hit means.
void weapons_hurt_player(struct Game *g, int slot, float damage, Vec3 dir, float knock);
void weapons_hurt_item(struct Game *g, int item, Vec3 dir, float knock, Vec3 at);

// ---- bot (weaponbot.c) ----------------------------------------------------------------------
// HOLLOW_BOT=shoot: walk to a weapon, pick it up, and shoot the nearest other player or item every
// few seconds. Writes into `in`; returns true when it is driving.
bool weapons_bot_input(struct Game *g, struct Input *in);
