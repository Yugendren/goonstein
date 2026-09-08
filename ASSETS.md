# Third-party asset manifest

Every asset not authored in this repo is listed here with its source and licence.
Allowed licences: CC0, OFL, MIT, Mixamo. Not allowed: any CC-BY-NC, any ShareAlike, anything
requiring attribution we cannot satisfy in-game.

| Path | Source | Licence | Notes |
|------|--------|---------|-------|
| src/vendor/stb_image.h | https://github.com/nothings/stb | MIT / public domain | image loading |
| src/vendor/stb_image_write.h | https://github.com/nothings/stb | MIT / public domain | screenshot PNG |
| src/vendor/stb_easy_font.h | https://github.com/nothings/stb | MIT / public domain | debug and subtitle font |
| assets/models/kaykit/Knight.glb, Barbarian.glb | https://github.com/KayKit-Game-Assets/KayKit-Character-Pack-Adventures-1.0 | CC0 | rigged, 76 animations |
| assets/models/kaykit/Skeleton_Warrior.glb | https://github.com/KayKit-Game-Assets/KayKit-Character-Pack-Skeletons-1.0 | CC0 | rigged, 95 animations, same rig family |
| src/vendor/cgltf.h | https://github.com/jkuhlmann/cgltf | MIT | glTF parser |
| assets/models/kaykit/dungeon/*.glb | https://github.com/KayKit-Game-Assets/KayKit-Dungeon-Remastered-1.0 | CC0 | 40 static props (floors, walls, pillars, stairs, torches/candles, banners, barrels, crates, chest, rubble, arch/gate); embedded textures; see assets/models/kaykit/CATALOG.md |
| assets/models/kaykit/halloween/*.gltf, *.bin | https://github.com/KayKit-Game-Assets/KayKit-Halloween-Bits-1.0 | CC0 | 29 static props (dead trees, lanterns, fences, gravestones, pumpkins, crypt, paths, arch/gate, skull/bone/coffin decor); shared external texture halloweenbits_texture.png; see assets/models/kaykit/CATALOG.md |
| assets/models/kaykit/hex/*.gltf, *.bin | https://github.com/KayKit-Game-Assets/KayKit-Medieval-Hexagon-Pack-1.0 | CC0 | 25 static props (trees, rocks, hills, mountains, hex grass/water/river tiles); shared external texture hexagons_medieval.png; see assets/models/kaykit/CATALOG.md |
| assets/sprites/ninja/Actor/CharacterAnimated/NinjaGreen/*.png | https://pixel-boy.itch.io/ninja-adventure-asset-pack (Ninja Adventure - Asset Pack, by Pixel-boy and AAA) | CC0 | hero sprite, 32x32 frames, Separate/ + combined SpriteSheet.png; see assets/sprites/ninja/CATALOG.md |
| assets/sprites/ninja/Actor/Character/{Knight,Samurai,Princess,OldMan,Villager,Monk,Hunter}/*.png | https://pixel-boy.itch.io/ninja-adventure-asset-pack | CC0 | party/NPC candidates, 16x16 frames, SeparateAnim/ + Faceset.png portraits; see CATALOG.md |
| assets/sprites/ninja/Actor/Boss/{GiantRedSamurai,GiantBlueSamurai,TenguRed,TenguBlue,GiantFrog,DemonCyclop,GiantSlime}/*.png | https://pixel-boy.itch.io/ninja-adventure-asset-pack | CC0 | boss animation strips, square frames sized to sheet height; charge-then-attack inventory in CATALOG.md |
| assets/sprites/ninja/Actor/Monster/{Slime,Mushroom,Bear,Owl,Skull,Snake,Mole,Larva}/*.png | https://pixel-boy.itch.io/ninja-adventure-asset-pack | CC0 | regular enemies, 64x64 sheets = 4 frames x 4 direction rows of 16x16 |
| assets/sprites/ninja/FX/{Slash,Attack,Magic,Smoke,Particle}/**/*.png | https://pixel-boy.itch.io/ninja-adventure-asset-pack | CC0 | slash/attack/magic/smoke/particle effect strips; see CATALOG.md for per-file frame counts |
| assets/sprites/ninja/Ui/{Arrow.png,Dialog/*.png,Receptacle/*.png} | https://pixel-boy.itch.io/ninja-adventure-asset-pack | CC0 | curated UI subset: menu cursor arrow, dialog box panels/frames, heart/HP icons |
| assets/sprites/ninja/Audio/Sounds/{Whoosh & Slash,Hit & Impact,Menu,Magic & Skill,Alert,Bonus}/*.wav | https://pixel-boy.itch.io/ninja-adventure-asset-pack | CC0 | 74 sound effects, 44.1kHz stereo 16-bit; see assets/sprites/ninja/SOUNDS.md |
| assets/sprites/ninja/Audio/Musics/*.ogg | https://pixel-boy.itch.io/ninja-adventure-asset-pack | CC0 | 4 of 41 tracks (Adventure Begin, Clearing, Mystical, Fight), kept under 12MB budget; see SOUNDS.md |
