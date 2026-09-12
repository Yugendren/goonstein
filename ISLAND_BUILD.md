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

`assets/characters/goon_a.txt` .. `goon_d.txt`. Four MakeHuman bodies -- four builds, four faces,
real clothes -- carrying the same Quaternius clips they always did, put there by
`tools/blender/makegoon.py` and `tools/blender/retarget.py` from the recipes in
`assets/characters/mh/`; see README, "Characters". Every action is bound to an unarmed clip
(`Idle_Loop`, `Walk_Loop`, `Jog_Fwd_Loop`, `Sprint_Loop`, `Punch_Jab` / `Punch_Cross` /
`Melee_Hook`, `Roll`, `Hit_*`, `Death01`). The old stylised cast is still in the tree as
`goon_a_toon.txt` .. `goon_d_toon.txt`.

| file | name | scale | build and outfit |
|---|---|---|---|
| goon_a | **Dez** | 1.06 | heavy, middle-aged, rust-red shirt and jeans — the player (`hero goon_a` in settings.txt) |
| goon_b | **Marko** | 1.18 | tall and long-limbed, olive-yellow work suit — the big one, brought the extension lead |
| goon_c | **Pip** | 0.87 | small and slight, teal-cyan jacket — the only one who read anything |
| goon_d | **Bunny** | 1.00 | average everything, purple-magenta jacket — wants it on record that he said to call somebody |

b, c and d stand on the pier as `npc` lines with a talk scene each
(`island_marko.txt`, `island_pip.txt`, `island_bunny.txt`). The intro is `island_intro.txt`.

### Parts

New assemblies in `assets/models/own/`. The small ones -- `sun_lounger`, `golden_bird`,
`palm_tall`, `palm_bent`, `scrub`, `cactus`, `sea_rock`, `boat` -- are still built from
`models/shapes/*.obj` with flat tints, which is all a lounger needs.

**Every building is not.** `villa`, `villa_wing`, `cabana`, `pool_house`, `bunkhouse`,
`shed_blue`, `service_shed`, `water_tower`, `dock_house` and `temple` are generated as CadQuery
solids by `tools/cad/build_shells.py` -- walls with a real thickness and openings cut through
them, roofs that oversail their walls, plinths, verandas, an arcade of true arches -- and their
`.part` files, and the `arch/*_facade.part` files that seat the kit's frames in their reveals,
are written by that same script. **Do not hand-edit those .part files**; edit
`tools/cad/shells/NAME.py` (or `tools/cad/shells/extras/NAME.part` for hand-placed clutter) and
rebuild. See `tools/cad/README.md`.

One thing to know before moving a building: a shell is several OBJs, one per material group,
because `tex NAME TILE` is per model file. The `y` on each generated `piece` line is the height
that group was drawn at, put back, because `src/model.c` rebases every OBJ's lowest vertex to
zero. Nothing in the level file needs to know that -- but if you ever see a roof lying on the
ground, that is why.

The temple's stripes used to be ten full-plan boxes stacked with alternating tints. They are now
courses modelled on the outside of a real 0.45 m wall, the cobalt ones standing proud with
chamfered edges so each throws its own shadow line, returned in their own group so the level can
still retint them independently.

---

## 2. The two scripts

### `tools/island_terrain.py`

Writes `assets/levels/island_terrain_h.png` (16-bit height packed as `R<<8|G`, range
`[-64, 192)`), `_c.png` (biome colours) and `island_terrain.txt` (cell 1.75, origin -224 0 -224,
water 0). The grid is 257 x 257 and the engine takes its size from the PNG (65, 129 or 257 --
see `src/terrain.h`), so 1.75 m per cell buys the same 448 m of world at four times the detail,
with the island in the middle and open sea to the horizon. See section 6 for what that detail is
spent on.

The island is a spine: a centreline `SPINE_X(z)`, a half-width `HALF_W(z)`, a crest height
`CREST(z)` and an inland slope `SLOPE(z)`, plus fbm coastline wobble that is stronger on the
windward (east and south) side. Height inland from the waterline is `min(crest, beach apron then
slope)`; offshore it falls away exponentially to about -26 m. `BEACHES` widens the apron at the
cove, the north beach and the dock shelf; `HARD_SHORE` doubles the slope where cliffs are wanted.
`PADS` levels the built ground (circles and rectangles, each with its own blend); `PATHS` samples
the ground along each golf-cart road, smooths the profile and cuts the corridor in. Then
`--scatter` rejection-samples 104 palms (three variants by height and slope), 480 bushes, 38
agaves, 70 boulders and 150 tufts of ground cover by height, slope, moisture noise and distance
from roads and pads, plus 46 palm imposters on the empty neighbour island.

    python3 tools/island_terrain.py --report --preview /tmp/island.png \
        --scatter-into assets/levels/island.txt

`--report` prints the ground height at every named site; `--preview` writes a hill-shaded
top-down map. `--scatter-into` replaces the block below the `vegetation` banner in a level file
in place (`--scatter FILE` still just writes it out); `--out-dir` puts the three terrain files
somewhere else, for A/B runs. After any run that changes the heights, follow it with
`tools/island_resnap.py` (section 6).

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

    git show HEAD:assets/levels/island_terrain_h.png > /tmp/old_h.png   # before you regenerate
    python3 tools/island_terrain.py --scatter-into assets/levels/island.txt
    python3 tools/island_resnap.py assets/levels/island.txt --old /tmp/old_h.png --old-cell 1.75 --write
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

## 5. The realistic pass

The 467-prop stylised scatter (`scrub.part`, nine spheres and three cylinders; `cactus.part`;
`sea_rock.part`, seven boxes) became 842 photoscanned instances: 104 palms (still ours), 480
bushes (`shrub_02` and `pachira_aquatica_01` variants), 38 agaves (`cheiridopsis_succulent`,
standing in for the saguaro that had no business on a limestone cay), 70 shore boulders (the
`rock_moss` sets and `boulder_01`, tinted back toward bleached limestone), 150 ground-cover tufts.
The bushes now clump: a moisture fbm in `scrub_w` gives thickets and clearings where the old
scatter laid down an even lawn.

About 100 more scanned props dress the compound, dock, helipad, staff yard, culvert yard, court,
temple court and cove: crates, drums, jerrycans, buckets, a sack truck, a generator, planters with
real plants in them, garden furniture, street lamps, a marble bust, two bronzes, a stone fire pit,
conch shells, a timber jetty at the cove.

**Surfaces.** `assets/textures/NAME.png|jpg` is now a texture name anywhere a texture name is
accepted: on a `block` line, as `tex NAME [TILE]` on a `prop` line, and as `tex NAME [TILE]` on a
`.part` piece (see `assets/levels/README.md`). 281 pieces across the island's own 14 assemblies and
47 block/shape-prop lines in `island.txt` carry one. `TILE` is repeats per metre in world space
(planar mapping), so a stretched box does not stretch its texture.

**Tint compensation**, the non-obvious bit: `shaders/lit.frag` multiplies texture * vertex colour *
tint in sRGB before the 2.2, so dropping a photoscan onto a flat-tinted piece darkens it by the
map's mean. Every tint was divided back out — per channel where the map should be neutral (plaster,
concrete, roof tile: the piece's tint is the paint), by luminance only where the map's own colour
is the point (weathered planks, palm bark, foliage, terracotta). That is why the `.part` files are
full of tints above 1.0, and why the island's palette is unchanged.

The terrain takes a detail map: `assets/textures/ground_detail.*` is multiplied into the biome
colours with world planar UVs at 0.45 repeats per metre and a 1/mean gain, so the ground has
photoscan grain without a new palette (`src/terrain.c`, wired in `src/game.c`).

The palms were rebuilt: each frond is now an inner plank leaving the crown almost flat plus an
outer one hinged at its tip and drooping, so the crown arcs instead of spoking, and the trunk
carries a real bark map. That is the surface you stand next to.

Look retuned for dusk with scans: `toon 0.30 0.34 3.4` (softer bands, shallower floor, so a scan's
own shading shows through), `grade 1.22 1.02 1.02 0.16 1.22` (more exposure, less saturation and
bloom, because the albedo now carries the colour), `gain 1.04 1.00 0.94`, `fogv` density 0.0085 ->
0.0072 and start 34 -> 42. `pixel` and `style` were not touched — they are off.

Cost, draw calls at 300 frames, `HOLLOW_NOVSYNC=1`, before (commit 2c95ce1) vs after:

| where | before | after |
|---|---|---|
| first person on the quay | 5641 | 4490 |
| the pool | 1341 | 1434 |
| summit overview | 985 | 371 |

Props went 574 -> 1055 but draws went *down*: the old scatter's stylised pieces were assemblies
(`scrub.part` nine spheres and three cylinders, `sea_rock.part` seven boxes) and a scan is one
mesh. Free-running frame times after: 8.7 ms at the quay, 3.6 ms at the summit, 10.8 ms on the
intro cutscene frame.

The tools: `tools/polyhaven_get.py` fetches, md5-checks and keeps only the base-colour map,
shrinking model textures to 512 px; `tools/gltf_split.py` splits a set into per-variant files
sharing the `.bin`; `tools/blender/decimate.py` took four scans over 50k triangles down
(`dead_tree_trunk` 101802 -> 17999, `modular_wooden_pier` 84780 -> 23994, `boulder_01` 66122 ->
14000, `concrete_road_barrier` 60928 -> 9999).

## 6. The vegetation pass

Two things fill every frame on this island: the ground and the palms. Both were cheap in the
specific sense that you could see what they were made of.

### The palms

Twenty-nine boxes and cylinders per tree, with each frond a pair of flat green planks. They now
come out of `tools/palm/make_palm.py` in headless Blender: three variants (`palm_a` the standard
9.5 m tree, `palm_b` leaning over the water at 7 m, `palm_c` the tall one at 12 m), about 1700
triangles each, and each is ONE glTF with exactly two materials, which is what lets the prop
instancer collapse a hundred and four of them into two batches.

A frond is a five-segment card strip folded into a shallow drooping V -- rib, two mid-wing, two
edge -- carrying a cut-out texture drawn by `tools/palm/make_frond.py`: two hundred leaflets a
side off a tapering rib, seven per cent of them missing, one in ten torn short, green at the root
and dry yellow at the tip, each leaflet multiplied by its own brightness so neighbours differ.

Three decisions in there are worth keeping in mind if you touch any of it:

- **The card's outline is the frond's outline.** Its half width follows the same `env(t)` envelope
  the texture is drawn with, and the UVs map to the matching band. This is not tidiness: the
  shadow pass has no alpha test (`shadow.frag` writes depth and nothing else), so a rectangular
  card would throw a rectangular shadow. Because the card is frond-shaped, the palm lays a
  palm-shaped shadow across the sand.
- **Both faces of a frond carry the same normal**, pointing out of the middle of the crown and
  tilted up rather than off the face of the card. That is two-sided foliage lighting done in the
  asset: the underside of a frond lights like the top of a dome, and back-face culling never has
  to be turned off.
- **The wind weight is the vertex colour's ALPHA, inverted.** 1 is rigid -- which is what every
  other mesh in the game says, and what glTF says when a mesh has no COLOR_0 at all -- and 0 sways
  the most. `world.vert` and `world_inst.vert` read it and bend the vertex by a gust field keyed
  to world position and `flags.y` (the frame's time); `lit.frag` no longer takes opacity from the
  vertex colour, so the channel is free. It had to be a vertex channel and not a material flag or
  a level keyword, because the instancer batches by mesh and texture and a per-batch flag would
  have had to join that key.

`assets/models/own/palm_far.glb` is the same tree rendered to a texture by Blender on transparent
film and mapped onto two crossed cards: eight triangles. It is not wired to a distance -- 1700
triangles is already a stand-in, and `props.c` picks its own LODs by file name -- it is used
directly for the forty-six palms on Great Goonstein, which nobody ever lands on.

### The bushes

The 480 scanned bushes were the wrong plant and the wrong cost: `shrub_02` is 27254 triangles and
`pachira_aquatica` 76914, for something a metre across and forty pixels tall from the road, and
scanned temperate scrub reads as a handful of red twigs on a limestone cay. `tools/palm/make_bush.py`
builds three card-cluster bushes -- six to eleven quads on a squashed hemisphere, each carrying one
patch of the four-patch leaf atlas from `tools/palm/make_leafpatch.py`, every card written twice so
it is visible from both sides, every vertex normal pointing out of the middle of the clump. Thirty
to eighty-eight triangles. The photoscans are kept where you walk past them: a bush within about
four metres of a road or a built pad is a scan with 45% probability, everything else is a card.

### The ground

`tools/island_terrain.py` now writes a **257 x 257** grid at 1.75 m cells -- the same 448 m of
world at four times the detail (`src/terrain.h`: the grid size is a per-terrain number now, and the
mesh is split into four chunks because 66049 vertices do not fit 16-bit indices). What that detail
is spent on:

- **Drainage.** A ridged noise cut into the flanks, stretched along the downhill direction and
  squeezed across it. The trick is the choice of coordinates: taking a local frame from the
  gradient does not work, because the frame rotates and the effective frequency wanders into mush.
  Instead the two axes are fields that are continuous everywhere -- `z`, the island's long axis,
  which its contours run along on both flanks, and the height itself, which is by definition the
  downhill coordinate. `noise(z / 10.5, h / 16)` is a comb of gullies down the hillside, and a
  ridged transform turns its zero crossings into V-shaped cuts with rounded spurs between them.
- **Outcrops** of limestone where the windward faces are steep, and one octave of roughness
  everywhere, both fading out on the beaches and below the waterline where smooth sand is correct.
  Everything is applied BEFORE the pads and the roads, so the built ground and the cart tracks are
  untouched.
- **A surface, not a contour map.** Every material boundary is noisy: the sand-to-scrub line is a
  height plus a few metres of fbm, so it wanders like a real one. Cliffs take limestone strata
  keyed to elevation with a warped phase. The Laplacian of the height darkens the hollows and
  lifts the spurs by about a tenth, which is the only ambient occlusion the island gets and is what
  makes the new gullies read from the far side of the water. The rock thresholds are read off the
  finished field rather than guessed -- the median land slope is 0.53 and the 90th percentile 1.5,
  so rock starts around 1.0; the old numbers were set against a 3.5 m grid that averaged every
  slope down, and at 1.75 m they turned two thirds of the island into bare limestone.
- **De-tiling in the shader.** `terrain_draw` sets `material.water = 2` -- the sea is 1, one bit
  does not deserve a new uniform -- and `lit.frag` answers by multiplying in a second, much slower
  sample of the same detail map, normalised by its own mean (which `push_material` already uploads
  as `flatc.rgb`). One extra fetch, on the one surface that fills the bottom of every frame, and
  the half-metre tiling stops repeating.

### The sharp edge, blunted

ISLAND_BUILD used to warn that regenerating the terrain silently leaves five hundred props hanging
in the air, because `island_snap.py` turns `~` into a number once and then the file is just
numbers. `tools/island_resnap.py` is the other half: given the old heightmap and the new one, it
re-applies each placement's offset above the ground to the new ground, and only for things that
were resting on it (`--above` / `--below`). It moves `prop` and `npc` lines and deliberately not
`block` or `collider` -- those are pieces of a structure the author levelled by hand, and moving
each by its own local ground delta turns a straight wall into a staircase. Forty-nine placements
moved when the 257 grid went in; every one is printed, and it writes nothing without `--write`.

    git show HEAD:assets/levels/island_terrain_h.png > /tmp/old_h.png
    python3 tools/island_terrain.py --scatter-into assets/levels/island.txt
    python3 tools/island_resnap.py assets/levels/island.txt --old /tmp/old_h.png --old-cell 3.5 --write

`--scatter-into` replaces everything below the `vegetation` banner in the level file in place, so
regenerating the scatter is no longer a copy and paste.

## 7. Deliberately not done yet

- First person. `view first` does not exist; the level is `view third` until it does.
- The descent. The Culvert stops at a steel door and the Music Room's hatch is sealed: both are
  placeholders for M4's first underground level, with triggers (`tunnel`, `temple`) already
  named.
- Loot, enemies, the clock, the boat as an extraction point, and co-op spawns for goons b-d.
  They currently stand on the pier forever, which is exactly as useful as it sounds.
