# hollow

Working title. An HD-2D JRPG: pixel-art sprites living in a lit low-poly 3D world, an isometric
overworld, in-engine cutscenes, and card battles where the enemy's attack frames are rhythm beats
you deflect osu-style. C11 on SDL3, targeting macOS, Linux (Steam Deck) and Windows.

This is the skeleton build: one corridor, one cutscene going in, one boss, one cutscene coming out.

## Build and run

    cmake -B build -G Ninja
    cmake --build build
    ./build/bin/hollow

The first configure fetches and builds SDL3 from source (a couple of minutes). Incremental
rebuilds are sub-second. Shaders are precompiled and committed; after editing anything in
`shaders/` run `tools/shaders.sh` (needs `brew install shaderc spirv-cross`).

## Controls

| Action        | Keyboard / mouse          | Gamepad          |
|---------------|---------------------------|------------------|
| Move          | WASD                      | Left stick       |
| Camera        | Mouse                     | Right stick      |
| Attack        | Left mouse (or J)         | RB               |
| Deflect       | Right mouse (or K)        | LB               |
| Step dodge / sprint | Shift tap / hold (or Space) | B          |
| Lock-on       | Middle mouse, Q or Tab    | R3               |
| Interact      | E                         | A                |
| Skip cutscene | Enter                     | Start            |
| Debug overlay | F1                        | Back / Select    |
| Pause / step  | F2 / F3                   |                  |
| Reload data   | F5                        |                  |
| Quit          | Esc                       |                  |

The layout follows Sekiro on PC. Level files also hot-reload on save while the game is running.

## Everything is a text file

The game is meant to be edited without touching C:

| What                  | Where                        | Format doc                 |
|-----------------------|------------------------------|----------------------------|
| Level geometry, cameras, triggers | `assets/levels/*.txt` | `assets/levels/README.md`  |
| Cutscenes             | `assets/scenes/*.txt`        | `assets/scenes/README.md`  |
| Dialogue portraits    | `assets/portraits.txt`       | speaker name and image     |
| Boss move sets        | `assets/enemies/*.txt`       | comments in `warden.txt`   |
| Character bindings (sprite or model) | `assets/characters/*.txt` | comments in `src/charmodel.h` |
| Sprite sheets and frame animations | `assets/sprites/*.txt` | comments in `src/sprite.h` |
| Player tuning         | `assets/player.txt`          | comments in the file       |
| Look (fog, light)     | `fog` / `light` lines in the level |                      |
| Post-processing       | `shaders/post.frag`          |                            |

## Card battle

Reaching the boss starts a card battle. Cards are in `assets/cards/cards.txt`, the starting deck
in `assets/decks/knight.txt`, the boss's attacks and pattern in `assets/enemies/warden_battle.txt`.
Attack cards are played Slay the Spire style: press one, a curved arrow follows the cursor,
release over the Warden. Guard, heal and other self cards are dragged up onto yourself. A plain
click still plays a card straight away. End Turn (or Enter) hands over to the enemy. Its attacks are rhythm
reads: a numbered hit circle appears on you and an approach ring closes onto it. Click, right-click
or press Space when the ring meets the circle. Judgement tiers are PERFECT (45 ms), GREAT (95 ms)
and GOOD (150 ms); anything else is a miss and the hit lands. Perfects counter and bank two energy,
greats bank one, every fourth combo hit banks a bonus, and a miss resets the combo. Shift dodges
unblockable attacks. A timing bar shows whether you were early or late.

## Testing and debugging

`assets/settings.txt` holds personal defaults (volume, debug overlay, hero); flags override it.
`--quiet` sets volume to 0.15 and `--volume 0` mutes. `\` (or the backtick) opens the debugger:
a side panel with the state summary and a live stream of raw inputs (every key, mouse and pad
press with position) interleaved with the actions the game took. F1 toggles the wireframe overlay: state
machines, timers, mouse position, card hover/drag/target, parry press and judgement offsets,
and a live event log. Every event also goes to `hollow.log` in the working directory. F8 writes
`hollow_snapshot.txt` with the full state plus recent events and copies it to the clipboard,
so a bug report is: press F8 when it happens, paste.

## Tools (second window, live in the game)

Three tools, one key each: `[` the world editor (terrain, placing, look), `]` the character
builder, `\` the debugger. `Esc` closes the open tool (with nothing selected in it), and Esc quits
only when no tool is open. On a Mac, Cmd works in place of Ctrl. Also `Ctrl+D` wireframes,
`Ctrl+R` reload data, `Ctrl+G` snapshot. Command line: `--tool 2` (world editor) / `--tool 4` (builder).

Tool windows open tiled beside the game window and the game window goes back where it was when
the tool closes (`HOLLOW_NO_TILE=1` leaves placement alone). Closing a tool window ends the tool;
nothing ever draws into the game window. Panels flow to the window width and scroll with the wheel.
Keys typed into a tool window go only to that tool; keys typed into the game window go only to
the game. `--tool-shot PATH` saves a PNG of the tool window; `HOLLOW_TOOL_SIZE="w h"` fixes its
size for headless layout checks.

### World editor (`[`)

The game window becomes a fly camera (WASD and Q/E, hold the right mouse button to look, wheel
changes speed). Three tabs:

**TERRAIN: generate, then paint over it.** GENERATE WORLD turns a seed and six sliders
(mountains, hills, roughness, forest, rocks, snow line, water level) into a landmass: noise
heights, biome colours from height, slope and moisture, a flat pad at the spawn and the arena, a
path between them, lakes below the water level, and trees and rocks scattered by biome rules
(clumped forests on gentle grass, rocks on slopes and high ground, nothing on the path or pads).
NEW SEED rolls another world; sliders and a new GENERATE change it. IMPORT HEIGHTMAP loads any
PNG from `assets/heightmaps` (real-world elevation tiles, or one you drew) as the heights,
stretched to the range slider. Then the brushes, held on the ground in the game window: RAISE
(Shift lowers), LOWER, SMOOTH, FLATTEN, PAINT biome colours, SCATTER pieces of a kit category,
CLEAR them, and PATH, which levels and paints a road along your drag. Ctrl+wheel resizes the
brush. APPLY AUTO BIOME repaints from height and slope; MOUNTAIN FOREST LOOK sets a daylight
alpine look. Ctrl+Z undoes sculpting. Saving writes `assets/levels/NAME_terrain_h.png`, `_c.png`
and a sidecar with the water level next to the level, referenced by its `terrain` line.

**PLACE: assets and where they go.** The palette lists every model file under `assets/models`
by folder: trees, rocks, ground, walls, props, lights (from `assets/kit.txt`), `shapes` (box,
cylinder, sphere, wedge, plate), `import` (your OBJ exports), `own` (your saved parts). Left click
places, `1` select/move (drag pieces, lights and emitters), `2` piece, `3` light, `4` emitter, `R`
rotate, `[` `]` scale, `X` delete, `G` duplicate, `F` fly to. A selected piece has stretch x/y/z,
colour and glow, so a grey box becomes a red beam. Pieces that collide flatten a pad under
themselves on terrain. **Meld pieces into a part:** shift-click several pieces, type a name, SAVE
AS PART. They become one `assets/models/own/NAME.part` (a list of `piece FILE pos size rotation
colour` lines), replaced in the level by a single piece and added to the palette under `own`;
UNGROUP takes it apart again. A part works as a prop and as a character attachment.

**LOOK.** Every lighting, fog, sky, grading and pixel-look value on a slider.

### Character builder (`]`)

Makes a character from a rigged model without drawing anything. BODY & PARTS lists every rigged
`.glb` under `assets/models/kaykit`, `characters` and `import` (KayKit knight, barbarian, mage,
rogues and four skeletons ship, all CC0) and every part of the chosen one as toggles. ATTACH puts
any file from `import`, `parts` or `own` on a bone: a helmet on `head`, a weapon on `handslot.r`,
a saved part on `chest`; nudge it with the sliders and it follows every animation. COLOURS lists
the model's paint colours, most used first; pick one and move the sliders to repaint it, shading
and edges follow. ANIMATION picks a fighting style, AUTO BIND fills every game action from the
clip names, and any clip previews on the hero. SAVE writes `assets/characters/NAME.txt`
(`model`, `hide`, `recolor`, `attach`, `anim` lines); SAVE + USE AS HERO also sets it as the hero
in `assets/settings.txt`. `--hero NAME` plays it.

### Lighting

Cheap classic stack, no ray tracing: a sun shadow map (one depth pass from the sun, fitted around
what the camera looks at, hard-edged to match the toon bands; `shadow` look line 0..1), banded sun
plus hemisphere ambient, up to sixteen point lights without shadows, height fog, bloom, tonemap and
grading. Time of day: a `daytime` look line (hours, `-1` off) or the TIME OF DAY slider on the
LOOK tab moves the sun along the author's noon bearing and blends sky, ambient, fog colour and
stars through night, dawn, noon, golden hour and dusk (`src/daylight.c`). Everything runs at full
speed on the Steam Deck class of hardware. `assets/levels/showcase.txt` demonstrates it with the
Poly Haven scans: `--start level:showcase`, `HOLLOW_NOSHADOW=1` to compare.

### The art pipeline in one paragraph

Characters and props are 3D, drawn through the pixel-art pass (small layer, grid-snapped camera,
Endesga 32 palette, one-pixel outline, depth-composited), so nothing is drawn by hand and
everything matches. Models come from CC0 packs (KayKit, Quaternius, Kenney, and photoscanned Poly Haven models with
textures in `assets/models/polyhaven`) or from your CAD tool: export OBJ with materials
into `assets/models/import` and it is in the palette (millimetre files scale themselves). Dialogue
portraits render live from the speaker's model with an emotion-driven clip (`assets/portraits.txt`
maps a speaker to `model:hero`, `model:boss` or an image). Tuning: the `pixel` look line, the LOOK
tab, `HOLLOW_PIX="3 8 1 1 0.6"`, `HOLLOW_NOPIX=1`.

## Headless test harness

    ./build/bin/hollow --start fight --bot --frames 3600 --screenshot out.png

`--frames N` exits after N rendered frames, `--screenshot P` saves the internal-resolution
frame, `--start` jumps to `battle`, `fight`, `boss_intro`, `victory` or `end`, `--volume 0.1` keeps test runs quiet, and `--bot` lets a
simple frame-perfect bot play the fight and logs every swing, parry and hit. This is how
the fight is checked without a controller in hand.

## Layout

    src/            game and platform code
      platform.*    SDL window, GPU device, input
      gfx.*         renderer: low-res target, world / ui / post pipelines, screenshot
      level.*       level file parser, collision, camera volumes, triggers
      camera.*      fixed / follow / cutscene camera and camera-relative input
      scene.*       cutscene timeline player
      combat.*      player and boss state machines, data file loaders
      render_world.* procedural textures, level drawing, box-figure characters
      model.*       glTF loader, skinning, animation clips and crossfades
      charmodel.*   maps gameplay animation states to model clips via a text config
      sprite.*      sprite sheets, frame animation, contact frames, upright billboards
      audio.*       procedural synth plus WAV/OGG samples and crossfading music
      game.*        state flow, HUD, debug overlay, scene hosting
    shaders/        Vulkan GLSL source, compiled by tools/shaders.sh
    assets/         all content (see above); assets/shaders holds compiled shaders
    ASSETS.md       licence manifest (KayKit CC0 props, Ninja Adventure CC0 sprites, music and sounds, stb, cgltf)

## Platform notes

Metal (macOS) and Vulkan (Linux, Steam Deck) shaders are produced by `tools/shaders.sh`.
Windows needs DXIL, which requires `dxc` on a Windows machine; until that step is added the
Windows build compiles but the renderer will fail to load shaders at startup.
