# hollow

Working title. Third-person PS2-style horror, fixed cameras in exploration, parry and posture
combat in set-piece encounters. C11 on SDL3. Targets macOS, Linux (Steam Deck), Windows.

## Build

    cmake -B build -G Ninja
    cmake --build build
    ./build/bin/hollow

First configure fetches and builds SDL3 from source (a couple of minutes). Incremental builds
are sub-second.

## Controls (placeholder)

WASD / left stick move, mouse / right stick look, J / X attack, K / Y parry, Space / B dodge,
E / A interact, F1 debug overlay, Esc quit.

## Layout

    src/        game and platform code
    shaders/    HLSL source, compiled per backend at build time
    assets/     content: textures, audio, models, levels, scenes (cutscene timelines)
    tools/      build and content tooling
    ASSETS.md   licence manifest for every third-party asset
