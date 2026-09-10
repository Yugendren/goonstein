# Building Goonstein Island

How the M3 island is put together, what it is made of, and what it needed from the engine.
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
(8.3 ms / 120 fps) with `HOLLOW_NOVSYNC=1`. The far plane decides how much of the island is in the
frame at all, so the island's draw count now depends on where you stand: a few hundred at the pier,
a couple of thousand looking down the spine.

---

## 4. What the engine grew for this

Every item content ran into has been built. What each one turned into, and where it lives:

1. **The far plane is a level value.** `look far METRES` (default 80, so no other level changed);
   the island asks for 600. The sun shadow map is refitted rather than stretched — the box follows
   what the camera looks at, its ceiling grows with the far plane, and past its edge the world is
   simply unshadowed, faded out over the outer 15% so there is no line across the ground. The sea
   was the other thing hiding behind 80 m: it now reaches far past the grid so the far plane cuts
   it, and the island's fog is thicker and falls off faster with height to suit.
2. **Characters stand on things.** `resolve_ground` (src/game.c) replaces the old terrain snap: the
   ground is the terrain, or the top of any solid block or `deck` collider within 0.5 m of the feet,
   whichever is higher. Walking onto something that low steps up; walking off it falls at 1 g. No
   jump yet. It applies to the player, every net slot, the boss, NPCs and cutscene actors.
3. **A prop can be a scene actor.** `prop ... name boat` plus `actor prop:boat move|teleport|face`
   in a scene; the prop's collider travels with it and anyone standing on that collider is carried.
   The intro is rewritten around it: the four of them ride the boat in from 66 m out and climb onto
   the quay.
4. **Cutscene actors are grounded**, and a scene's `move`/`teleport` y is a floor rather than an
   answer: `move x 0 z dur` hugs the terrain, a larger y lifts them onto a deck or a quay.
5. **A `prop` scale that was meant as a pitch** now says so and draws at 1 instead of putting a
   45 m mirrored slab on a hillside.
6. **`block ... pass` survives a save.** The solidity word is read from the end of the line at
   either length.
7. **`pixel 0`** is a whole line: the trailing fields keep their defaults.
8. **The sea is shaded.** Two crossing ripple trains (analytic normals, four sines), the sky at
   grazing angles, a sun glint, and a coastline read from a small depth texture: shallow water lifts
   toward the sand and foam sits on the waterline. See `terrain_draw_water` and lit.frag's `water`
   material.
9. **Prop instancing / part cost** is still open, and now matters: with the far plane at 600 a view
   down the island is ~2600 draw calls instead of ~120. It still sits on the display cap, so this is
   a headroom problem rather than a frame-rate one.

Two smaller things worth knowing:

- The vegetation carries its own colliders: a `.part` can declare `collide R [H] [deck]` and every
  prop placed from it inherits it, scaled. That is why the level file has no collider lines for its
  409 plants. The orbit camera ignores any collider under 1.2 m across so it does not shorten
  against every palm.
- `--bot` on a `view first` level wanders instead of walking the spine (`bot_input` hands over to
  `netgame_bot_wander` in first person), so the trigger-by-trigger regression in section 3 needs
  `--third`.

## 5. Deliberately not done yet

- First person. `view first` does not exist; the level is `view third` until it does.
- The descent. The Culvert stops at a steel door and the Music Room's hatch is sealed: both are
  placeholders for M4's first underground level, with triggers (`tunnel`, `temple`) already
  named.
- Loot, enemies, the clock, the boat as an extraction point, and co-op spawns for goons b-d.
  They currently stand on the pier forever, which is exactly as useful as it sounds.
