// --- map --- The paper map, and the objective it points at.
//
// The map is a hand-held object, not a screen. It is drawn through the viewmodel's own lens, last,
// exactly the way a weapon is (see weaponview.c and docs/weapons_feel.md): a folded paper card in
// the off hand, at its own field of view, in its own slice of the depth buffer. M raises it, M
// lowers it, and while it is up the weapon goes down and does not fire. That is the whole
// interaction. There is no map screen, no pause, no zoom.
//
// WHAT IS ON THE CARD. Two layers.
//
//   1. The PRINT. On the island that is `assets/textures/map_island.png`, drawn once by
//      `tools/island_terrain.py --map` out of the same heightmap the terrain is built from: ink
//      coastline, hatched hills, the roads, the place names, on the game's own paper texture. It
//      is a picture of the ground, and it never changes. `assets/textures/map_island.txt` is the
//      sidecar that says which world rectangle the image covers, so the C and the Python agree
//      about where a metre lands without either of them holding a copy of the other's numbers.
//      A level with no printed map and no terrain (the cave) gets a SCHEMATIC instead: its own
//      solid blocks, drawn as an outline from above. A level with terrain and no printed map (the
//      lantern test level) has no map at all, and pressing M says so.
//
//   2. The OVERLAY, redrawn every frame on top of the print: where you are and which way you are
//      facing, where the other goons are, the boat, the objective, and -- when the level has
//      written the way down as a `route` -- a dotted line along that route with the dashes
//      crawling toward the objective. Overlay marks are small unlit quads laid in the card's own
//      plane a millimetre proud of it, in the viewmodel pass; nothing is composited into a texture
//      and no texture is uploaded per frame.
//
// THE COMPASS STRIP is the map's always-on half: a thin bearing tape across the top of the HUD
// with the objective's name and its distance in metres. It exists so that the map is optional --
// a player who never presses M is still never lost.
#pragma once
#include "hmath.h"
#include <stdbool.h>
#include <stddef.h>

struct Game;
struct Input;

// --- objectives ------------------------------------------------------------------------------
// The whole objective system, which is deliberately three states and a position. Anything more
// than this is a quest log, and a quest log is not what "I cannot find the cave" needed.
typedef enum ObjectiveKind {
    OBJ_NONE = 0,
    OBJ_CAVE,     // on the island, before the boss is dead: reach the cave
    OBJ_BOSS,     // in the cave, boss alive: beat the boss
    OBJ_BOAT,     // the boss is dead: return to the boat with the relic
    OBJ_OUT,      // in the cave, boss dead: get back out of the cave
} ObjectiveKind;

typedef struct Objective {
    ObjectiveKind kind;
    char  text[48];       // "REACH THE CAVE"
    Vec3  pos;            // where it is, world space
    bool  has_pos;        // false = nothing to point at (the compass strip hides)
    char  route[24];      // the level `route` that leads there, or "" -- see level.h Route
} Objective;

// What the local player is supposed to be doing, right now, on this level. Cheap: a trigger scan
// and a couple of comparisons. game.c owns it because it is the only thing that knows about both
// the level and the fight.
Objective game_objective(const struct Game *g);

// --- the card --------------------------------------------------------------------------------
// One tick of the map: the M key and the pad's Back button, and the raise/lower ease. Called from
// tick_explore, before weapons_tick, so the weapon knows this frame whether it is being lowered.
void map_tick(struct Game *g, const struct Input *in, float dt);
// 0 = down and out of sight, 1 = fully up. Eased. weaponview.c drops the weapon by this much.
float map_raised(const struct Game *g);
// Is there a map of this level at all? False on a level with terrain and no printed map, which is
// what makes pressing M there a message instead of a blank card.
bool map_exists(const struct Game *g);
// The card, its hand and every live mark on it, drawn through the viewmodel lens. Called from
// game_render_at immediately before weapons_draw_viewmodel, so the weapon ends up in front of the
// paper rather than through it.
void map_draw_viewmodel(struct Game *g);
// The compass strip at the top of the HUD. Called from draw_hud while exploring.
void map_draw_hud(struct Game *g);
// Drop the loaded print (level change, shutdown). Safe to call with nothing loaded.
void map_unload(struct Game *g);
