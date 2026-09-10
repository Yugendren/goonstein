// Items: the loot. A physics body with a model, a name, a price and a habit of breaking.
//
// An item is described by a text file under assets/items/NAME.txt and placed by a level line
// `item NAME x y z [yaw]`. The host owns every item; clients see them replicated and predict only
// the one they are carrying. See README.md, "Items and carrying".
#pragma once
#include "hmath.h"
#include "phys.h"
#include <stdint.h>

struct Game;
struct Input;

#define ITEMDEF_MAX 32     // distinct assets/items/*.txt files loaded at once
#define ITEMS_MAX   128    // live items in a level (LEVEL_MAX_ITEMS)
#define DEBRIS_MAX  192    // pieces flying about after breakages, local-only
#define ITEM_HIST   8      // snapshots kept per item for client interpolation

// ---------------------------------------------------------------- the file
typedef struct ItemDef {
    char  name[32];        // the file's base name, what a level line says
    char  model[128];      // model or .part file, relative to assets/
    char  display[48];     // what the HUD calls it
    float mass;            // kg; drives the carry spring and the throw speed
    Vec3  half;            // box half-size in metres
    float radius;          // > 0: a sphere of this radius instead of a box
    float fragile;         // impact speed in m/s that breaks it; 0 = unbreakable
    int   value;           // dollars it is worth in the boat
    bool  two_handed;      // caps the carrier at 60% speed and disables sprint
    float scale;           // model scale (default 1)
    Vec4  tint;            // model tint, and the colour of its debris
    int   sound;           // SoundId played when it breaks, -1 = none
    // --- weapons --- (see weapons.h). `weapon` absent leaves all of this zero: ordinary loot.
    int   weapon;          // WeaponKind: 0 none, 1 melee, 2 gun
    float damage;          // of a goon's 100-point wind pool; 100 puts them on the floor
    float rate;            // shots or swings per second
    float wrange;          // metres a shot carries; a melee swing uses WEAP_SWING_ARC instead
    int   ammo;            // rounds in a full gun
    float knock;           // metres per second of shove given to whatever it hits
    int   pellets;         // hitscan rays per shot: 1 for a pistol, a handful for a shotgun
    int   fire_sound;      // SoundId played on firing, -1 = none
    bool  ok;
} ItemDef;

// ---------------------------------------------------------------- one item
typedef struct Item {
    bool     used;
    int      def;              // index into Items.defs
    int      body;             // phys body index, -1 once broken
    uint16_t id;               // stable network id, never reused within a level
    int      held_by;          // net slot carrying it, -1 = nobody
    bool     broken;
    bool     in_hold;          // inside the boat's cargo volume
    bool     dirty;            // host: changed since the last snapshot it went out in
    Vec3     pos; Quat rot;    // what the renderer draws: the body, or the interpolated replica
    float    drop_lock;        // seconds before this item can be grabbed again
    // client interpolation, oldest first
    struct { double t; Vec3 pos; Quat rot; } hist[ITEM_HIST]; int nhist;
    bool     seen_broken;      // client: the break has already been turned into debris
    Vec3     net_pos; Quat net_rot;   // host: what the last snapshot said, so a body that is awake
                                      // but not actually going anywhere costs nothing to replicate
    bool     sunk;             // resting under the sea: still an item, but not worth walking into the water for
    bool     weapon_hand;      // --- weapons --- held as a weapon rather than on the carry spring:
                               // no physics body, drawn by weapons.c off the holder's hand or spine
    bool     leash_armed;      // the spring has reeled it in at least once, so the leash may bite
    float    leash_t;          // seconds the spring has been over-stretched; a swing is not a drop
} Item;

// ---------------------------------------------------------------- debris (local only)
typedef struct Debris {
    bool used; Vec3 pos, vel; Quat rot; Vec3 spin; Vec4 tint; float size, life, max_life;
} Debris;

// ---------------------------------------------------------------- per-player carry state
typedef struct Carry {
    int   item;        // item index in hand, -1
    float charge;      // seconds the throw button has been held, 0..ITEM_CHARGE_MAX
    bool  charging;
    float pending;     // client: seconds a predicted grab or release is still waiting on the host,
                       // during which a snapshot that has not seen it yet must not undo it
} Carry;

#define ITEM_REACH        2.5f    // metres you can grab from
#define ITEM_HOLD_DIST    1.2f    // where the hold point sits in front of the eye
#define ITEM_BREAK_LEASH  1.5f    // stretch past this and the item is knocked out of your hands
#define ITEM_CHARGE_MAX   0.8f    // seconds of throw charge
#define ITEM_SLOW_SPEED   0.6f    // two-handed speed multiplier

typedef struct Items {
    ItemDef  defs[ITEMDEF_MAX]; int ndefs;
    Item     it[ITEMS_MAX];     int n;      // high-water mark; iterate [0, n) and skip !used
    Debris   debris[DEBRIS_MAX];
    Carry    carry[4];                      // per net slot
    uint16_t next_id;
    int      look_at;                       // item the local player is looking at, -1 = none
    int      hold_count, hold_value;        // what is in the boat right now
    int      lost_value; float lost_t;      // HUD "-VALUE" after a break
    bool     hold_valid; Vec3 hold_min, hold_max;   // the level's `hold` trigger volume
    float    last_ms;                       // physics + items cost of the last tick
    unsigned breaks;                        // how many items have been broken this run
} Items;

// ---- assets/items/NAME.txt (itemdef.c) ----------------------------------------------------
// Loads (and caches) one item file. Returns an index into `its->defs`, or -1 if it failed.
int  itemdef_get(Items *its, const char *name);

// ---- debris, local only (debris.c) --------------------------------------------------------
void debris_burst(Items *its, Vec3 at, Vec4 tint, float size, int count, float speed);
void debris_update(struct Game *g, float dt);
void debris_draw(struct Game *g);

// ---- hud (itemhud.c) ----------------------------------------------------------------------
// The look-at prompt, the carry line and charge bar, "-VALUE" and the boat's hold readout.
void items_draw_hud(struct Game *g);

// ---- bot (itembot.c) ----------------------------------------------------------------------
// Explore bot: walk to the nearest free item, grab it, walk to the boat, drop it, repeat.
// Writes movement and buttons into `in`; returns true when it is driving (an item run is on).
bool items_bot_input(struct Game *g, struct Input *in);
// Where the bot is trying to get to right now, for the log line. (0,0,0) when idle.
Vec3 items_bot_target(const struct Game *g);

// ---- core (items.c) -----------------------------------------------------------------------
void items_reset(struct Game *g);                 // drop everything, free the bodies
void items_load_level(struct Game *g);            // spawn from the level's `item` lines
// One tick. The host (and single player) simulates; a client predicts the item in its own hands
// and pins everything else where the interpolation puts it.
void items_tick(struct Game *g, const struct Input *in, float dt);
void items_draw(struct Game *g);                  // item models and debris

int  items_find_id(const Items *its, uint16_t id);
// The item `slot` is looking at within ITEM_REACH, or -1.
int  items_look_target(const struct Game *g, int slot);
// Where an item held by `slot` is pulled toward, and the direction that player is looking.
Vec3 items_hold_point(const struct Game *g, int slot);
Vec3 items_look_dir(const struct Game *g, int slot);
// Host-side commands. items_grab validates reach and returns false when it is refused.
bool items_grab(struct Game *g, int slot, int item);
void items_release(struct Game *g, int slot, bool thrown, Vec3 vel);
// --- weapons --- The physics body of an item that is being taken into (or out of) a hand. A
// weapon has no body while it is held: it is a model on the end of an arm, not a thing on a
// string. Detach is safe to call twice; attach rebuilds the body at the item's current pos/rot.
void items_body_detach(struct Game *g, int item);
bool items_body_attach(struct Game *g, int item);
// Break it now (an impact past the threshold, or the host being told to): debris, sound, value lost.
void items_break(struct Game *g, int item);
// 1, or ITEM_SLOW_SPEED while carrying something two-handed. Sprint is blocked at the same time.
float items_speed_scale(const struct Game *g, int slot);
bool  items_two_handed(const struct Game *g, int slot);
// ---- replication, called by netgame.c ------------------------------------------------------
// Client: one item's state out of a snapshot, pushed into its interpolation history.
void items_net_sample(struct Game *g, uint16_t id, Vec3 pos, Quat rot, int held_by, bool broken, bool in_hold, double t);
// Client: the hold totals ride in the snapshot header rather than being recounted locally.
void items_set_hold_totals(struct Game *g, int count, int value);
// Host: a client asked to grab / to let go. Both validate; grab returns false when it is refused.
bool items_net_grab(struct Game *g, int slot, uint16_t id);
void items_net_release(struct Game *g, int slot, uint16_t id, bool thrown, Vec3 vel);

// Count and value of everything sitting in the boat's hold.
void  items_hold_totals(const struct Game *g, int *count, int *value);
// Centre of the boat's hold volume, and whether the level has one.
bool  items_hold_center(const struct Game *g, Vec3 *out);
// Is this world point inside the hold volume? What the bot uses to decide it is standing in the
// right place: it must let go with the item over the boat, not with itself over the boat.
bool  items_in_hold_volume(const struct Game *g, Vec3 p);
static inline const ItemDef *item_def(const Items *its, const Item *i) { return &its->defs[i->def]; }
