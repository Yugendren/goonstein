# Building Goonstein Island

How the M3 island is put together, what it is made of, and what it still needs from the engine.
Everything below is content: `assets/**` plus two scripts in `tools/`. No C was written for it.

Research the map is drawn from: `ISLAND.md`. Tone and hard rules: `DESIGN.md`. The island name and
every location name on it are invented; the outline and the scale are the only things taken from
the public record.

---

## 1. What is there

`assets/levels/island.txt` — one level, `view third`, dusk (`daytime 18.5`), plain polygon look
(`pixel 0`, `style 0 0 0 1`), sun shadow on, no `combat` line.

**Axes, once and for all.** The island's long axis runs along world Z:

| in fiction | in the world |
|---|---|
| east  | +Z |
| west  | -Z |
| north | -X |
| south | +X |

So the boat lands at the north-west, the compound sits on the western shelf, and the striped
pavilion stands on the eastern high point 300 m away. This orientation is not arbitrary: the
headless bot walks toward +Z and steers back toward x = 0, so a `--bot` run drives the length of
the island through the compound, over the saddle, up the hill and onto the temple court.

**Scale.** 302 m along the spine, up to 114 m across, 32 m at the summit, about 3.5 ha of land.
That is roughly a third of the real thing's linear size (ISLAND.md derives ~1 km x ~400 m from the
acreage) and it walks end to end in about 95 s at the player's 3.2 m/s, 55 s sprinting — the
"60-90 seconds end to end" ISLAND.md asks for.

| # | Place (all names fictional) | Where | Trigger |
|---|---|---|---|
| 1 | **Slack Tide Cove**, the only landable beach, a burnt-out fire and somebody's abandoned skiff | -14, -140 | `cove` |
| 2 | **Pelican Pier**, a stone mole with a timber dock house, bollards, fender piles and the goons' motor boat | -68, -112 | `dock`, `intro` |
| 3 | **Pad One**, the helipad on the bluff, two blue-roofed sheds and a wind sock | -48, -92 | `helipad` |
| 4 | **Villa Ambergris**, colonnaded, turquoise-roofed, two wings closing a courtyard round a lit basin | -16, -68 | `compound` |
| 5 | **The Oval**, an oval pool, a three-arched pool house, four cabanas, loungers and parasols | 16, -48 | `pool` |
| 6 | **The Bunkhouse**, staff quarters: four identical doors, air conditioners, a washing line | -32, -32 | `bunkhouse` |
| 7 | **The Cistern**, a galvanised water tower on the saddle | -2, -4 | `cistern` |
| 8 | **Windward Point**, a parapet on the north cliff, 20 m of air below it, a telescope and a bench | -22, 46 | `lookout` |
| 9 | **Utility Two** and **the Culvert**: a half-buried concrete shed whose doorway is the mouth of a 17 m cutting into the hill that dead-ends at a door nobody has opened | 30, 89 | `culvert`, `tunnel` |
| 10 | **The Court**, a bermed rectangle with nothing in it and no explanation | 32, 113 | `court` |
| 11 | **The Music Room**, the blue-and-white striped pavilion on the eastern high point, roof open where the dome used to be, two gold birds still bolted on, a sealed hatch inside | 0, 126 | `temple` |
| 12 | **Great Goonstein**, the empty neighbour across the cut — scenery only | -178, -168 | — |

Only `intro` has a scene wired to it. The other twelve are named landing points: add
`scene NAME FILE` to the level and a `NAME.txt` under `assets/scenes/` and the trigger fires it.

### The four goons

`assets/characters/goon_a.txt` .. `goon_d.txt`, all from the Quaternius ranger with the sword
attachment dropped and every action bound to unarmed clips (`Idle_Loop`, `Walk_Loop`,
`Jog_Fwd_Loop`, `Sprint_Loop`, `Punch_Jab` / `Punch_Cross` / `Melee_Hook`, `Roll`, `Hit_*`,
`Death01`).

| file | name | scale | outfit |
|---|---|---|---|
| goon_a | **Dez** | 1.05 | rust orange, maroon trousers — the player (`hero goon_a` in settings.txt) |
| goon_b | **Marko** | 1.15 | olive-yellow — the big one, brought the extension lead |
| goon_c | **Pip** | 0.90 | teal-cyan — the small one, the only one who read anything |
| goon_d | **Bunny** | 1.00 | purple-magenta — wants it on record that he said to call somebody |

b, c and d stand on the pier as `npc` lines with a talk scene each
(`island_marko.txt`, `island_pip.txt`, `island_bunny.txt`). The intro is `island_intro.txt`.

### Parts

New assemblies in `assets/models/own/`, all built from `models/shapes/*.obj` with flat tints:

`boat`, `dock_house`, `shed_blue`, `villa`, `villa_wing`, `cabana`, `pool_house`, `bunkhouse`,
`sun_lounger`, `water_tower`, `service_shed`, `temple`, `golden_bird`, `palm_tall`, `palm_bent`,
`scrub`, `cactus`, `sea_rock`.

The temple's stripes are ten full-plan boxes stacked with alternating tints, which is why a
9.5 m striped cube costs ten pieces.

---

## 2. The two scripts

### `tools/island_terrain.py`

Writes `assets/levels/island_terrain_h.png` (16-bit height packed as `R<<8|G`, range
`[-64, 192)`), `_c.png` (biome colours) and `island_terrain.txt` (cell 3.5, origin -224 0 -224,
water 0). The engine's grid is a fixed 129 x 129, so 3.5 m per cell buys 448 m of world with the
island in the middle and open sea to the horizon.

The island is a spine: a centreline `SPINE_X(z)`, a half-width `HALF_W(z)`, a crest height
`CREST(z)` and an inland slope `SLOPE(z)`, plus fbm coastline wobble that is stronger on the
windward (east and south) side. Height inland from the waterline is `min(crest, beach apron then
slope)`; offshore it falls away exponentially to about -26 m. `BEACHES` widens the apron at the
cove, the north beach and the dock shelf; `HARD_SHORE` doubles the slope where cliffs are wanted.
`PADS` levels the built ground (circles and rectangles, each with its own blend); `PATHS` samples
the ground along each golf-cart road, smooths the profile and cuts the corridor in. Then
`--scatter` rejection-samples 104 palms, 255 scrub, 38 cactus and 70 boulders by height, slope,
moisture noise and distance from roads and pads.

    python3 tools/island_terrain.py --report --preview /tmp/island.png \
        --scatter /tmp/scatter.txt

`--report` prints the ground height at every named site; `--preview` writes a hill-shaded
top-down map, which is currently the **only** way to see the whole island (see "Needs code").
The scatter block is pasted at the bottom of `island.txt` under its own comment banner.

### `tools/island_snap.py`

An authoring aid. Placing 570 props on a heightmap by hand means knowing the ground height at
every x/z, so the level may be written with the ground as a tilde:

    prop models/own/villa.part  -16 ~ -70  0 1.0
    prop models/own/lamp.part     4 ~+2.6 -12  0 1.0
    collider  -16 ~+4 -70  20 8 12
    trigger dock  -84 ~-4 -120  -54 ~+8 -100

`python3 tools/island_snap.py assets/levels/island.txt` rewrites those in place with real
numbers, so the result is an ordinary level file the world editor can load, edit and save. It
knows which token is y for `prop`, `npc`, `spawn`, `boss`, `block`, `collider`, `light`,
`emitter` and `trigger` (both corners). Run it once after writing tildes; after that the file is
just numbers, so **regenerating the terrain does not move anything that is already placed** —
that is the one sharp edge, and it is why the terrain was frozen before the compound went in.

### Rebuilding from scratch

    python3 tools/island_terrain.py --scatter /tmp/scatter.txt
    # paste /tmp/scatter.txt under the "vegetation" banner at the end of island.txt
    python3 tools/island_snap.py assets/levels/island.txt      # only if there are tildes left

---

## 3. Verifying

The binary is `build/bin/goonstein`. Always mute it.

    HOLLOW_FPS=60 ./build/bin/goonstein --volume 0 --bot --frames 1800 --shot-every 150 DIR
    HOLLOW_FPS=60 ./build/bin/goonstein --volume 0 --start scene:island_intro.txt \
        --frames 1800 --shot-every 120 DIR
    HOLLOW_FPS=60 ./build/bin/goonstein --volume 0 --start level:island --spawn 0 110 \
        --frames 60 --screenshot shot.png

A `--bot` run walks the spine of the island in about 81 s of sim time and logs, in order:
`intro`, `dock`, two NPC conversations on the pier, `helipad`, `compound`, `bunkhouse`,
`cistern`, `temple`. That is the regression test: if any of those stop firing, something has
moved or something is blocking the road.

Frame cost is the same as the shipped Lantern Count slice: both sit on the display cap
(8.3 ms / 120 fps) with `HOLLOW_NOVSYNC=1`: 8.3 ms and 519 draw calls over 574 props on the
island against 8.3 ms and 360 draws over 642 props on lantern. The island is not the expensive thing; the 80 m far plane is doing a lot of
culling for free.

---

## 4. Needs code

Content-only work ran into these. None of them are cosmetic.

1. **The camera's far plane is a hard-coded 80 m.** `src/camera.c`,
   `m4_perspective(c->fov * DEG2RAD, aspect, 0.1f, 80.0f)`. Nothing beyond 80 m from the eye is
   ever drawn, so *a 300 m island can never read as an island in game* — not from the hill, not
   from the boat, not from a cutscene camera. This was fine for a 90 m village and is the single
   biggest blocker on M3. **Recommendation:** make the far plane a `Look` value (a `camera` line
   field or its own `viewdist` line, default 80 so nothing else changes), set it to ~600 on the
   island, and refit the sun shadow map's ortho extent to the visible range so shadow resolution
   does not collapse. Until then the island's only overview is
   `tools/island_terrain.py --preview`.
2. **Characters are snapped to the terrain every tick** (`src/game.c`: `c->pos.y =
   terrain_height(...)` for the player, the boss and every NPC), so nobody can stand on a block,
   a prop or a boat deck, and there is no vertical collision at all. Consequences here: the dock
   had to be built as a stone mole (ground) rather than a pier on piles; the goons stand *beside*
   the boat in the intro rather than in it; no building can have a first floor or a raised
   walkway. **Recommendation:** ground height = max(terrain, top of any block whose xz contains
   the character and whose top is within a step of their feet), plus gravity.
3. **Scene actors cannot be props.** `src/scene.c` resolves `actor NAME` to the player, the boss
   or an NPC only. The intro wants a motor boat driving in to the dock with four men in it; it
   gets a moored boat and a camera move. **Recommendation:** let `actor` name a level prop by an
   optional `name` field on the `prop` line, and let characters parent to a moving prop.
4. **The player is not terrain-snapped inside a cutscene.** `tick_explore` snaps the player's y
   to the terrain every tick, but `tick_scene` only runs `character_script_update`, while NPCs are
   snapped unconditionally. So `actor player teleport x 0 z yaw` — which is exactly how every
   existing scene is written, because on the flat lantern level y is always 0 — buries the hero
   inside the ground and he silently vanishes from the shot. Cost about half an hour to find.
   The island's intro therefore carries the real quay height (1.60) in every player command.
   **Recommendation:** snap the player in `tick_scene` too, or make `teleport`'s y optional.
5. **Props have a yaw and nothing else.** `prop FILE x y z yaw scale` — there is no pitch or roll,
   and the field after `yaw` is `scale`, so a line written as if it took a pitch (`... 0 -25
   stretch ...`) silently multiplies the prop by -25 and puts a 45 m red slab on the hillside.
   Anything tilted has to become a `.part` (which does have pitch and roll). **Recommendation:**
   accept `pitch`/`roll` as optional prop keywords, and reject a non-positive `scale` with a log
   line instead of drawing it.
6. **`block` solidity is off by one.** `src/level.c` writes `block ... tile solid|pass` (13
   tokens, which is also what `level_save` emits) but the loader requires 13 or 14 tokens and
   reads the solidity from `tok[13]`, so a 13-token line is *always solid* and a saved `pass`
   block silently comes back solid. The two non-solid quay kerbs here are props instead. One-line
   fix: read `tok[12]` when `n == 13`.
7. **No `pixel 0` shorthand.** `pixel` needs all five numbers or the line is rejected, so
   "pixel pass off" is written `pixel 0 8 1 0 0.6`. Cosmetic, but it costs a confused minute.
8. **No water shading.** The sea is one flat emissive box the size of the terrain
   (`src/game.c`). It reads acceptably at dusk, but there is no shoreline foam, no depth tint and
   no movement, and its edge is the edge of the terrain grid — currently hidden by fog, which is
   the only reason the fog is as thick as it is.
9. **Prop instancing / part cost.** A part is drawn piece by piece, so 104 palms is ~1700 draws
   before culling. Fine at 80 m; it will matter the moment (1) is fixed.

## 5. Deliberately not done yet

- First person. `view first` does not exist; the level is `view third` until it does.
- The descent. The Culvert stops at a steel door and the Music Room's hatch is sealed: both are
  placeholders for M4's first underground level, with triggers (`tunnel`, `temple`) already
  named.
- Loot, enemies, the clock, the boat as an extraction point, and co-op spawns for goons b-d.
  They currently stand on the pier forever, which is exactly as useful as it sounds.
