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

## Editors (second window, live in the game)

`F6` opens the environment editor, `F7` the sprite editor, `\` the debugger; each takes over
the tool window, and pressing the same key closes it. Command line: `--tool 2` / `--tool 3`.

**Environment editor.** The game window becomes a fly camera (WASD and Q/E, hold the right
mouse button to look, wheel changes speed). The tool window has the kit palette (trees, rocks,
ground, walls, props, lights from `assets/kit.txt`), the placement settings, and the LOOK tab
with every lighting, fog, sky and grading value on a slider. In the game window: left click
places the ghost, `1` select/move (drag pieces, lights and emitters), `2` piece, `3` light,
`4` emitter, `R` rotate, `[` `]` scale, `X` delete, `G` duplicate, `F` fly to the selection,
`Ctrl+Z` undo, `Ctrl+S` save. Saving rewrites the level file (comments are dropped) and the
game hot-reloads it, so play, edit, play.

**Sprite editor.** Draws in the tool window while the game keeps running with your character as
the hero: every stroke updates the sprite in the world within a tenth of a second, so walk
around or fight with it between edits. `--edit NAME` starts straight into it.

## Sprite editor

    ./build/bin/hollow --edit NAME [--size 32]

Opens the in-engine pixel editor on `assets/sprites/own/NAME`, creating it with idle, walk and
attack if it does not exist. Mouse paints (right-drag erases); B pencil, E eraser, G fill, I pick,
L line; `[` `]` change frame, 1-4 change direction, arrows nudge, O onion skin, M mirror, K marks
the frame as the attack's contact (the parry beat), C copies a frame to every direction, F mirrors
left into right, A adds the next preset animation, Space plays, Ctrl+S saves. Saving writes the
PNG sheets, the sprite definition and a character binding, so `--hero NAME` plays it immediately.

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
