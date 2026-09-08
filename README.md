# hollow

Working title. A PS2-style third-person horror game: fixed authored cameras in exploration,
in-engine cutscenes, and set-piece boss fights with parry and posture combat. C11 on SDL3,
targeting macOS, Linux (Steam Deck) and Windows.

This is the skeleton build: one corridor, one cutscene going in, one boss, one cutscene coming out.

## Build and run

    cmake -B build -G Ninja
    cmake --build build
    ./build/bin/hollow

The first configure fetches and builds SDL3 from source (a couple of minutes). Incremental
rebuilds are sub-second. Shaders are precompiled and committed; after editing anything in
`shaders/` run `tools/shaders.sh` (needs `brew install shaderc spirv-cross`).

## Controls

| Action        | Keyboard        | Gamepad          |
|---------------|-----------------|------------------|
| Move          | WASD            | Left stick       |
| Attack        | J               | X / Square       |
| Parry         | K               | Y / Triangle     |
| Dodge         | Space           | B / Circle       |
| Interact      | E               | A / Cross        |
| Skip cutscene | Enter           | Start            |
| Debug overlay | F1              | Back / Select    |
| Pause / step  | F2 / F3         |                  |
| Reload data   | F5              |                  |
| Quit          | Esc             |                  |

Level files also hot-reload on save while the game is running.

## Everything is a text file

The game is meant to be edited without touching C:

| What                  | Where                        | Format doc                 |
|-----------------------|------------------------------|----------------------------|
| Level geometry, cameras, triggers | `assets/levels/*.txt` | `assets/levels/README.md`  |
| Cutscenes             | `assets/scenes/*.txt`        | `assets/scenes/README.md`  |
| Boss move sets        | `assets/enemies/*.txt`       | comments in `warden.txt`   |
| Player tuning         | `assets/player.txt`          | comments in the file       |
| Look (fog, light)     | `fog` / `light` lines in the level |                      |
| Post-processing       | `shaders/post.frag`          |                            |

## Headless test harness

    ./build/bin/hollow --start fight --bot --frames 3600 --screenshot out.png

`--frames N` exits after N rendered frames, `--screenshot P` saves the internal-resolution
frame, `--start` jumps to `fight`, `boss_intro`, `victory` or `end`, and `--bot` lets a
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
      audio.*       procedural sound effects and drone
      game.*        state flow, HUD, debug overlay, scene hosting
    shaders/        Vulkan GLSL source, compiled by tools/shaders.sh
    assets/         all content (see above); assets/shaders holds compiled shaders
    ASSETS.md       licence manifest for third-party assets (none yet; textures and
                    sounds are generated in code)

## Platform notes

Metal (macOS) and Vulkan (Linux, Steam Deck) shaders are produced by `tools/shaders.sh`.
Windows needs DXIL, which requires `dxc` on a Windows machine; until that step is added the
Windows build compiles but the renderer will fail to load shaders at startup.
