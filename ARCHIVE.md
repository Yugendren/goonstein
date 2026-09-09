# hollow — archive snapshot, 2026-09-10

State of the project at the point it was archived, before being forked into a new game.
Working title "hollow": an HD-2D game in C11 on SDL3 — 3D low-poly worlds run through a pixel-art
pass, everything driven by text files under `assets/`. 61 commits, 8–10 September 2026, ~10.8k
lines of C across 25 `.c` files in `src/` (12.4k with headers), 16 shaders, 108 MB of assets.
macOS and Linux (Steam Deck)
build and run; Windows compiles but has no DXIL shader step yet.

Tag: `archive-2026-09-10`. No remote — this repository is the only copy.

## What the engine does

**Renderer** (`src/gfx.c`, `shaders/`). SDL3 GPU device, low-resolution render target, separate
world / UI / post pipelines, screenshot path. HDR with toon banding, up to sixteen point lights
(no shadows on them), height fog, sky, bloom, tonemap and colour grading.

**HD-2D pixel pass.** 3D characters and props are drawn into a small grid-snapped layer, crunched
to a palette and colour levels, given a one-pixel outline and inner crease lines, and
depth-composited back into the world (`pixel` look line, `HOLLOW_PIX`, `HOLLOW_NOPIX=1`). A
separate **world style layer** in post snaps the whole frame to the palette, inks depth edges,
posterises and pixelates the world (`style` look line). `assets/palette.txt` (up to 64 hex colours,
hot reloaded) drives both, so one file sets the game's colour identity.

**Lighting and time of day** (`src/daylight.c`). One sun shadow map: depth pass fitted around the
camera view, hard-edged PCF to match the toon bands, strength on the `shadow` look line. Banded sun
plus hemisphere ambient. A `daytime` hour (0–24) computes sun, ambient, sky, stars, lift and fog
colour for that time, overriding the authored look lines.

**Terrain** (`src/terrain.c`, `src/terrain_io.c`). Heightmap terrain with biome colours. The
generator turns a seed and six sliders (mountains, hills, roughness, forest, rocks, snow line,
water level) into a landmass: noise heights, biomes from height/slope/moisture, flat pads at spawn
and arena, a path between them, lakes below the water line, and trees and rocks scattered by biome
rule. Heightmap PNGs can be imported instead. Saved as `NAME_terrain_h.png`, `_c.png` and a sidecar.

**World editor** (`[`, `src/leveled.c`). Fly camera in the game window, tool UI in a second window.
TERRAIN: generate, then sculpt with raise/lower/smooth/flatten/paint/scatter/path brushes, undo.
PLACE: every model under `assets/models` in a palette by folder; place, move, rotate, scale,
delete, duplicate; per-piece stretch, colour and glow; lights and emitters; shift-click several
pieces and SAVE AS PART melds them into one `.part` file. LOOK: every lighting, fog, sky, grading
and pixel value on a slider. Saves the level file.

**Character builder** (`]`, `src/builder.c`). Builds a character from a rigged model with no
drawing: toggle body parts, ATTACH any part or import onto a bone, recolour the model's paint
colours, BORROW PARTS across the nine KayKit files (shared skeleton, so a mage hat or skeleton arm
fits any body), pick an animation set and AUTO BIND clip names to game actions. Saves
`assets/characters/NAME.txt`; SAVE + USE AS HERO writes the hero into settings.

**Parts / CAD OBJ import** (`src/part.c`). Export OBJ+MTL from a CAD tool into
`assets/models/import` and it appears in the editor palette (flat material colours, millimetres
auto-scaled to metres, stood on the ground). `.part` files are lists of `piece FILE pos size
rotation colour` lines and work both as world props and as character attachments on bones.
Per-model `.recolor` sidecars remap paint colours on load. Two headless Blender scripts in
`tools/blender`: `variant.py` (proportion variants keeping weights and all clips) and
`decimate.py` (scan/CAD export down to a triangle budget).

**Scenes, NPCs, dialogue** (`src/scene.c`). Cutscene timeline files: camera keyframes, narration
and spoken lines with emotion, actor move/face/anim/teleport, fade, letterbox, shake, sound,
`daytime` and `music`. `npc` lines in a level place a character that faces the player and talks on
`E`, triggering a scene. Dialogue draws a JRPG portrait box with typed text and blips; portraits
render live from the speaker's 3D model with an emotion-driven clip.

**Card battle** (`src/battle.c`). Slay the Spire style hand: attack cards drag a curved arrow onto
the target, self cards drag onto yourself, nearest valid target highlights. The enemy's attacks are
osu-style rhythm reads — a hit circle with a closing approach ring, PERFECT/GREAT/GOOD tiers
(45/95/150 ms), combo and banked energy, Shift dodges unblockables, a timing bar shows early/late.
Cards in `assets/cards/cards.txt`, deck in `assets/decks/`, enemy pattern in `assets/enemies/`.

**Third-person Sekiro-style fight** (`src/combat.c`). Posture, not health, is the real resource.
An A/B/C attack chain with root motion, 0.2 s input buffer and post-contact cancels; hits land on
the animation's marked contact frame (`anim attack CLIP contact 0.42`) rather than a fixed number.
Tap to deflect inside a 0.14 s window judged with sub-tick press timing, or hold to block and take
~85% of damage as posture. Damage-sized hit reactions, hitstop, speed-blended phase-synced
locomotion, and a boss that circles, closes, chains combos and shortens windups below half health.

**Bot test harness.** `--bot` runs a frame-perfect bot that walks the road, talks to NPCs,
triggers every scene, deflects on the boss's contact frame, dodges unblockables and punishes
recovery, logging every swing, parry and hit. This is the regression test.

**Hot reload.** While running: the level, its terrain, every loaded model or part, and the hero's
character file and model reload within a second of being saved. `F5` / `Ctrl+R` forces it.

**Frame-rate options.** Simulation is a fixed 60 ticks/s, so play is identical everywhere;
rendering runs at display rate with characters and camera interpolated between ticks. Parry presses
are dated to the press, not the tick, so rhythm judgement is exact at any rate. The debugger's
FRAME RATE row (display, 30, 60, 90, 120, 144, 240, vsync toggle) saves to settings;
`HOLLOW_FPS=N`, `HOLLOW_NOVSYNC=1`, `HOLLOW_NOINTERP=1` override.

**Debugging.** `\` opens the debugger window: state summary and a merged stream of raw inputs and
the actions taken. `F1` wireframe overlay. Every event goes to `hollow.log`; `F8` writes
`hollow_snapshot.txt` and copies it to the clipboard.

## The vertical slice: "The Lantern Count"

Eight minutes, all text under `assets/`, and the default start (`level lantern` in
`assets/settings.txt`). A mountain village at dawn, a crisis at the shrine, the Warden in the
ruined arena, a death cutscene.

| # | Beat | Files |
|---|------|-------|
| 1 | Dawn intro: narration, camera sweep | `scenes/lantern_intro.txt` |
| 2 | The plaza: the Elder notices the stranger | `scenes/lantern_town.txt` |
| 3 | Talk to the Elder, the Smith, the Child (`E`) | `characters/{elder,smith,child}.txt`, `scenes/lantern_{elder,smith,child}.txt` |
| 4 | Crisis at the shrine: dusk at midday, the music turns | `scenes/lantern_crisis.txt` |
| 5 | The arena gate: the Warden reveal | `scenes/lantern_boss.txt` |
| 6 | The fight | `enemies/warden_battle.txt`, `characters/warden.txt` |
| 7 | Death cutscene: the Warden kneels, night falls, fade | `scenes/lantern_victory.txt` |

Level `assets/levels/lantern.txt` (+ `lantern_terrain_*`), village parts in `assets/models/own/`
(`house_a/b/c`, `well`, `market_stall`, `lantern_post`, `shrine_lantern`, `shrine`). The slice
plays `view third` with `combat realtime`.

### How to run it

    ./build/bin/hollow                 # plays the slice (settings.txt: level lantern)
    ./build/bin/hollow --volume 0      # muted, for test runs
    HOLLOW_FPS=60 ./build/bin/hollow --volume 0 --bot --frames 9000 --shot-every 300 /tmp/slice

The bot reaches the death cutscene in about 65 seconds of sim time and `hollow.log` lists each
beat. Capture one beat with `--start scene:lantern_crisis.txt --spawn 0 42 --frames 900
--screenshot out.png`. Other flags: `--level NAME`, `--hero NAME`, `--start`, `--tool 2|4`,
`--tool-shot PATH`, `--debug`, `--quiet`.

## Assets and licences

Every third-party asset is listed with its source and licence in `ASSETS.md`. Allowed licences are
CC0, OFL, MIT and Mixamo only; nothing NC, nothing ShareAlike, nothing needing attribution the game
cannot show. Sources at archive time:

- **KayKit** (CC0) — nine rigged characters (knight, barbarian, mage, two rogues, four skeletons,
  one shared rig family) plus Dungeon Remastered, Halloween Bits and Medieval Hexagon prop packs
  (94 static props); see `assets/models/kaykit/CATALOG.md`.
- **Quaternius** (CC0) — the humanoid cast (`ranger_male`, `peasant_male`, `elder_male`,
  `smith_male`, `child_female`, `warden_male`), fused by `assets/models/quaternius/merge.py` from
  the free Standard tiers of the outfit, base-character and two Universal Animation Library packs:
  84 clips, 52–53 joints. Plus `sword_bronze.glb` from the props kit.
- **Poly Haven** (CC0) — six photoscanned models with 1k textures (barrel, mossy rocks, dead tree
  trunk, military crate, brass diya lantern, tree stump).
- **Ninja Adventure pack**, Pixel-boy and AAA (CC0) — pixel sprites (hero, NPCs, bosses, monsters,
  FX, UI subset), 74 sound effects and 4 music tracks.
- **VT323** (OFL 1.1) — the tool window UI font.
- **stb** (MIT/public domain) `stb_image`, `stb_image_write`, `stb_easy_font`, `stb_truetype`, and
  **cgltf** (MIT), all in `src/vendor`.
- Authored here: the user's own hero sprite sheet in `assets/sprites/own/`, the `.part` village
  models in `assets/models/own/`, all levels, scenes, characters, cards and tuning files.

## Decisions log

The direction changes visible in `git log --oneline`, in order:

1. **Skeleton game first** (`56b5367`…`da142bb`). SDL3 GPU scaffold, fixed-timestep loop, renderer
   and shader pipeline, then a whole small game — level, cameras, cutscenes, parry combat, boss,
   audio, HUD, test harness — before any art direction.
2. **Third person, then top-down** (`a60c308`, `7c8da8d`). An orbit camera with lock-on and wall
   collision and Sekiro controls came first; the isometric overworld camera arrived with the card
   battle.
3. **Card battle with rhythm parries** (`e73a5fa`, `9f048d5`). The boss fight became a turn-based
   card battle whose enemy attacks are osu-style timing reads — hit circles, approach rings,
   judgement tiers, combo.
4. **HD-2D pivot** (`01e15b6`). Hand-drawn pixel sprites (Ninja Adventure) as lit billboards in the
   3D world, sprite FX, WAV/OGG audio, isometric overworld, a giant samurai boss.
5. **Sprites out, 3D through a pixel pass** (`09eb8bc`). Hand-drawn sprites were replaced by 3D
   models rendered into a low-res palette-crunched layer with outlines. Nothing is drawn by hand and
   everything matches automatically. The sprite editor was later removed entirely (`36babca`).
6. **Tools consolidated to three** (`36babca`). Sprite editor and primitive part editor deleted;
   what remained is the world editor, the character builder and the debugger.
7. **CAD as the art pipeline** (`8692ebd`, `f130eb6`, `d2c46f3`). OBJ/MTL import, bone attachments,
   parts as assemblies saved from the world editor, borrowed parts across a shared skeleton.
8. **Chibi to humanoid cast** (`56facb7`, `dc286c0`). The stylised KayKit chibi characters were
   replaced for the slice by realistic-proportion ~1.8 m Quaternius humans merged from four packs.
9. **Back to third person, realtime fight** (`dc286c0`). The final commit makes third person the
   default view for the slice and replaces its card battle with the Sekiro-style duel. Both survive:
   `view top|third` and `combat cards|realtime` are per-level lines, and the card battle is intact.

## How to fork this for a new game

### Build

    cmake -B build -G Ninja
    cmake --build build -j8
    ./build/bin/hollow --volume 0

The first configure fetches and builds SDL3 from source (pinned to `release-3.4.16`, a couple of
minutes); incremental rebuilds are sub-second. Shaders are precompiled and committed — after
editing anything in `shaders/`, run `tools/shaders.sh` (needs `brew install shaderc spirv-cross`).
Note there is no `Makefile`: the build is Ninja through CMake.

### Engine vs slice content

**Engine — keep all of it.** `src/` (25 C files, no slice logic hardcoded), `shaders/`,
`tools/`, `CMakeLists.txt`, and the format docs `assets/levels/README.md`,
`assets/scenes/README.md`.

**Slice content — replace or delete for a new game.**

| Path | What it is |
|------|------------|
| `assets/levels/` | `lantern*` (the slice), `glade`, `corridor`, `showcase` (lighting demo) + terrain PNGs |
| `assets/scenes/` | `lantern_*` are the slice's eight beats; `intro/boss_intro/victory/glade_*` are older |
| `assets/characters/` | the cast: `elder`, `smith`, `child`, `warden`, `hero`, `q_hero`, `q_villager`, plus older `knight*`, `demo`, sprite variants |
| `assets/enemies/` | `warden*.txt` boss move sets and battle patterns |
| `assets/cards/`, `assets/decks/` | the card list and the starting deck |
| `assets/models/own/` | the village `.part` assemblies |
| `assets/sprites/own/` | the user's own hero sprite sheet |
| `assets/portraits.txt` | speaker → portrait mapping |
| `SLICE.md` | the slice's beat sheet and verification command |

**Shared but tunable:** `assets/player.txt` (player numbers), `assets/palette.txt` (colour
identity), `assets/kit.txt` (editor palette catalogue), `assets/models/{kaykit,quaternius,polyhaven,shapes,import}`
and `assets/sprites/ninja` (the CC0 libraries — keep what you use and prune `ASSETS.md` to match),
`assets/fonts`, `assets/audio`, `assets/heightmaps`, `assets/shaders` (compiled output).

Minimum viable fork: keep `src/`, `shaders/`, `assets/models`, `assets/fonts`, `assets/shaders`,
`assets/palette.txt`, `assets/player.txt`, `assets/kit.txt`; delete the tables above; write one new
level and point `settings.txt` at it.

### settings.txt keys

`assets/settings.txt` holds personal defaults, read at startup; command-line flags override them.

| Key | Meaning |
|-----|---------|
| `volume 0` | master volume 0..1 (`--volume`, `--quiet` = 0.15) |
| `debug 0` | 1 starts with the F1 overlay on (`--debug`) |
| `hero NAME` | character file to play as, `assets/characters/NAME.txt` (`--hero`) |
| `fps 0` | frame cap: 0 = display rate, or 30 / 60 / 90 / 120 / 144 / 240 (`HOLLOW_FPS`) |
| `vsync 1` | 0 lets the frame rate exceed the display, tearing possible (`HOLLOW_NOVSYNC`) |
| `level lantern` | the level to start in, `assets/levels/NAME.txt` (`--level`) |

`fps` and `vsync` are written back by the debugger's FRAME RATE row; `hero` is written back by the
character builder's SAVE + USE AS HERO.
