# Third-party asset manifest

Every asset not authored in this repo is listed here with its source and licence.
Allowed licences: CC0, OFL, MIT, Mixamo. Not allowed: any CC-BY-NC, any ShareAlike, anything
requiring attribution we cannot satisfy in-game.

| Path | Source | Licence | Notes |
|------|--------|---------|-------|
| src/vendor/stb_image.h | https://github.com/nothings/stb | MIT / public domain | image loading |
| src/vendor/stb_image_write.h | https://github.com/nothings/stb | MIT / public domain | screenshot PNG |
| src/vendor/stb_easy_font.h | https://github.com/nothings/stb | MIT / public domain | debug and subtitle font |
| src/vendor/stb_truetype.h | https://github.com/nothings/stb | MIT / public domain | tool UI font rasteriser |
| assets/fonts/VT323-Regular.ttf | https://github.com/google/fonts/tree/main/ofl/vt323 (Peter Hull) | OFL 1.1 (assets/fonts/VT323-OFL.txt) | tool window UI font |
| assets/models/kaykit/Knight.glb, Barbarian.glb, Mage.glb, Rogue.glb, Rogue_Hooded.glb | https://github.com/KayKit-Game-Assets/KayKit-Character-Pack-Adventures-1.0 | CC0 | rigged |
| assets/models/kaykit/Skeleton_Warrior.glb, Skeleton_Mage.glb, Skeleton_Minion.glb, Skeleton_Rogue.glb | https://github.com/KayKit-Game-Assets/KayKit-Character-Pack-Skeletons-1.0 | CC0 | rigged, same rig family |
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
| assets/models/polyhaven/barrel_03/*.gltf, *.bin, textures/*.jpg | https://polyhaven.com/a/barrel_03 | CC0 | photoscanned model, 1k textures |
| assets/models/polyhaven/rock_moss_set_01/*.gltf, *.bin, textures/*.jpg | https://polyhaven.com/a/rock_moss_set_01 | CC0 | photoscanned model, 1k textures |
| assets/models/polyhaven/dead_tree_trunk/*.gltf, *.bin, textures/*.jpg | https://polyhaven.com/a/dead_tree_trunk | CC0 | photoscanned model, 1k textures |
| assets/models/polyhaven/old_military_crate/*.gltf, *.bin, textures/*.jpg | https://polyhaven.com/a/old_military_crate | CC0 | photoscanned model, 1k textures |
| assets/models/polyhaven/brass_diya_lantern/*.gltf, *.bin, textures/*.jpg | https://polyhaven.com/a/brass_diya_lantern | CC0 | photoscanned model, 1k textures |
| assets/models/polyhaven/tree_stump_01/*.gltf, *.bin, textures/*.jpg | https://polyhaven.com/a/tree_stump_01 | CC0 | photoscanned model, 1k textures |
| assets/models/quaternius/ranger_male.glb, peasant_male.glb | https://quaternius.com/packs/modularcharacteroutfitsfantasy.html + universalbasecharacters.html + universalanimationlibrary.html + universalanimationlibrary2.html (all free "Standard" tiers) | CC0 (assets/models/quaternius/LICENSE.txt) | rigged realistic-proportion humans, ~1.8m: outfit + base-body head fused with all 84 UAL1+UAL2 clips by assets/models/quaternius/merge.py; 53 joints, see CATALOG.md |
| assets/models/quaternius/sword_bronze.glb | https://quaternius.com/packs/fantasypropsmegakit.html | CC0 | one static blade, attached to q_hero's hand_r (the outfit packs ship no weapons) |
