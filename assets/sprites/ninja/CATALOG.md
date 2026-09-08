# Ninja Adventure asset pack -- sprite catalogue

Every PNG copied into `assets/sprites/ninja/` (excluding audio, LICENSE.txt, README.md -- see SOUNDS.md for audio). Frame layouts were inferred from each file's PNG IHDR (width/height) using the rules in the header of `scratchpad/scripts/gen_catalog.py`; sizes were parsed with a small Python script, no third-party libraries.

## Actor/CharacterAnimated/NinjaGreen (hero, 32x32 frame)

| Path | Size | Frame layout |
|------|------|--------------|
| `Actor/CharacterAnimated/NinjaGreen/Separate/Attack.png` | 128x128 | 4 frame(s) x 4 direction rows of 32x32 (down, up, left, right, top to bottom) |
| `Actor/CharacterAnimated/NinjaGreen/Separate/Climb.png` | 32x128 | 1 frame(s) x 4 direction rows of 32x32 (down, up, left, right, top to bottom) |
| `Actor/CharacterAnimated/NinjaGreen/Separate/Dead.png` | 32x64 | 1 frame(s) x 2 row(s) of 32x32 |
| `Actor/CharacterAnimated/NinjaGreen/Separate/Hit.png` | 128x64 | 4 frame(s) x 2 row(s) of 32x32 |
| `Actor/CharacterAnimated/NinjaGreen/Separate/Idle.png` | 128x128 | 4 frame(s) x 4 direction rows of 32x32 (down, up, left, right, top to bottom) |
| `Actor/CharacterAnimated/NinjaGreen/Separate/Item.png` | 32x64 | 1 frame(s) x 2 row(s) of 32x32 |
| `Actor/CharacterAnimated/NinjaGreen/Separate/Jump.png` | 96x128 | 3 frame(s) x 4 direction rows of 32x32 (down, up, left, right, top to bottom) |
| `Actor/CharacterAnimated/NinjaGreen/Separate/Pickup.png` | 32x64 | 1 frame(s) x 2 row(s) of 32x32 |
| `Actor/CharacterAnimated/NinjaGreen/Separate/Push.png` | 128x128 | 4 frame(s) x 4 direction rows of 32x32 (down, up, left, right, top to bottom) |
| `Actor/CharacterAnimated/NinjaGreen/Separate/Roll.png` | 128x96 | 4 frame(s) x 3 row(s) of 32x32 |
| `Actor/CharacterAnimated/NinjaGreen/Separate/Swim.png` | 128x128 | 4 frame(s) x 4 direction rows of 32x32 (down, up, left, right, top to bottom) |
| `Actor/CharacterAnimated/NinjaGreen/Separate/Walk.png` | 128x128 | 4 frame(s) x 4 direction rows of 32x32 (down, up, left, right, top to bottom) |
| `Actor/CharacterAnimated/NinjaGreen/SpriteSheet.png` | 256x544 | combined atlas of all Separate/ animations packed into an 8x17 grid of 32x32 cells; not a single uniform animation strip -- use the Separate/ files to slice frames |

## Actor/Character (party/NPC candidates, 16x16 frame)

Knight, Samurai, Princess, OldMan, Villager, Monk, Hunter. Each has SeparateAnim/{Attack,Dead,Idle,Item,Jump,Special1,Special2,Walk}.png plus a Faceset.png portrait.

| Path | Size | Frame layout |
|------|------|--------------|
| `Actor/Character/Hunter/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Character/Hunter/SeparateAnim/Attack.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/Hunter/SeparateAnim/Dead.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Hunter/SeparateAnim/Idle.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/Hunter/SeparateAnim/Item.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Hunter/SeparateAnim/Jump.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/Hunter/SeparateAnim/Special1.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Hunter/SeparateAnim/Special2.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Hunter/SeparateAnim/Walk.png` | 64x64 | 4 frame(s) x 4 direction rows of 16x16 (down, up, left, right, top to bottom) |
| `Actor/Character/Knight/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Character/Knight/SeparateAnim/Attack.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/Knight/SeparateAnim/Dead.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Knight/SeparateAnim/Idle.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/Knight/SeparateAnim/Item.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Knight/SeparateAnim/Jump.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/Knight/SeparateAnim/Special1.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Knight/SeparateAnim/Special2.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Knight/SeparateAnim/Walk.png` | 64x64 | 4 frame(s) x 4 direction rows of 16x16 (down, up, left, right, top to bottom) |
| `Actor/Character/Monk/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Character/Monk/SeparateAnim/Attack.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/Monk/SeparateAnim/Dead.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Monk/SeparateAnim/Idle.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/Monk/SeparateAnim/Item.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Monk/SeparateAnim/Jump.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/Monk/SeparateAnim/Special1.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Monk/SeparateAnim/Special2.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Monk/SeparateAnim/Walk.png` | 64x64 | 4 frame(s) x 4 direction rows of 16x16 (down, up, left, right, top to bottom) |
| `Actor/Character/OldMan/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Character/OldMan/SeparateAnim/Attack.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/OldMan/SeparateAnim/Dead.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/OldMan/SeparateAnim/Idle.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/OldMan/SeparateAnim/Item.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/OldMan/SeparateAnim/Jump.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/OldMan/SeparateAnim/Special1.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/OldMan/SeparateAnim/Special2.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/OldMan/SeparateAnim/Walk.png` | 64x64 | 4 frame(s) x 4 direction rows of 16x16 (down, up, left, right, top to bottom) |
| `Actor/Character/Princess/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Character/Princess/SeparateAnim/Attack.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/Princess/SeparateAnim/Dead.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Princess/SeparateAnim/Idle.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/Princess/SeparateAnim/Item.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Princess/SeparateAnim/Jump.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/Princess/SeparateAnim/Special1.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Princess/SeparateAnim/Special2.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Princess/SeparateAnim/Walk.png` | 64x64 | 4 frame(s) x 4 direction rows of 16x16 (down, up, left, right, top to bottom) |
| `Actor/Character/Samurai/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Character/Samurai/SeparateAnim/Attack.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/Samurai/SeparateAnim/Dead.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Samurai/SeparateAnim/Idle.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/Samurai/SeparateAnim/Item.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Samurai/SeparateAnim/Jump.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/Samurai/SeparateAnim/Special1.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Samurai/SeparateAnim/Special2.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Samurai/SeparateAnim/Walk.png` | 64x64 | 4 frame(s) x 4 direction rows of 16x16 (down, up, left, right, top to bottom) |
| `Actor/Character/Villager/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Character/Villager/SeparateAnim/Attack.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/Villager/SeparateAnim/Dead.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Villager/SeparateAnim/Idle.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/Villager/SeparateAnim/Item.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Villager/SeparateAnim/Jump.png` | 64x16 | 1 frame x 4 directions of 16x16, laid out horizontally (down, up, left, right, left to right) |
| `Actor/Character/Villager/SeparateAnim/Special1.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Villager/SeparateAnim/Special2.png` | 16x16 | 1 frame, 16x16, single pose (no per-direction variants) |
| `Actor/Character/Villager/SeparateAnim/Walk.png` | 64x64 | 4 frame(s) x 4 direction rows of 16x16 (down, up, left, right, top to bottom) |

## Actor/Boss (square frame == sheet height)

| Path | Size | Frame layout |
|------|------|--------------|
| `Actor/Boss/DemonCyclop/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Boss/DemonCyclop/Hit.png` | 150x50 | 3 frame(s) of 50x50 |
| `Actor/Boss/DemonCyclop/Idle.png` | 250x50 | 5 frame(s) of 50x50 |
| `Actor/Boss/DemonCyclop/Sprite.png` | 37x33 | 37x33, not square and not a multiple of a boss frame height -- best guess: single static reference/preview image, not an animation strip |
| `Actor/Boss/DemonCyclop/Walk.png` | 300x50 | 6 frame(s) of 50x50 |
| `Actor/Boss/GiantBlueSamurai/AttackLeft.png` | 384x96 | 4 frame(s) of 96x96 |
| `Actor/Boss/GiantBlueSamurai/AttackRight.png` | 384x96 | 4 frame(s) of 96x96 |
| `Actor/Boss/GiantBlueSamurai/ChargeLeft.png` | 288x96 | 3 frame(s) of 96x96 |
| `Actor/Boss/GiantBlueSamurai/ChargeRight.png` | 288x96 | 3 frame(s) of 96x96 |
| `Actor/Boss/GiantBlueSamurai/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Boss/GiantBlueSamurai/Hit.png` | 384x48 | 8 frame(s) of 48x48 |
| `Actor/Boss/GiantBlueSamurai/Idle.png` | 576x48 | 12 frame(s) of 48x48 |
| `Actor/Boss/GiantBlueSamurai/Walk.png` | 576x48 | 12 frame(s) of 48x48 |
| `Actor/Boss/GiantFrog/Attack.png` | 120x40 | 3 frame(s) of 40x40 |
| `Actor/Boss/GiantFrog/Charge.png` | 280x40 | 7 frame(s) of 40x40 |
| `Actor/Boss/GiantFrog/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Boss/GiantFrog/Hit.png` | 120x40 | 3 frame(s) of 40x40 |
| `Actor/Boss/GiantFrog/Idle40x40.png` | 200x40 | 5 frame(s) of 40x40 |
| `Actor/Boss/GiantFrog/Jump.png` | 240x40 | 6 frame(s) of 40x40 |
| `Actor/Boss/GiantRedSamurai/AttackLeft.png` | 384x96 | 4 frame(s) of 96x96 |
| `Actor/Boss/GiantRedSamurai/AttackRight.png` | 384x96 | 4 frame(s) of 96x96 |
| `Actor/Boss/GiantRedSamurai/ChargeLeft.png` | 288x96 | 3 frame(s) of 96x96 |
| `Actor/Boss/GiantRedSamurai/ChargeRight.png` | 288x96 | 3 frame(s) of 96x96 |
| `Actor/Boss/GiantRedSamurai/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Boss/GiantRedSamurai/Hit.png` | 384x48 | 4 frame(s) of 96x48 (verified by eye) |
| `Actor/Boss/GiantRedSamurai/Idle.png` | 576x48 | 6 frame(s) of 96x48 (verified by eye; wide front view) |
| `Actor/Boss/GiantRedSamurai/Walk.png` | 576x48 | 6 frame(s) of 96x48 (verified by eye) |
| `Actor/Boss/GiantSlime/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Boss/GiantSlime/Hit.png` | 310x52 | non-uniform: width 310 is not a clean multiple of height 52; best guess ~6 frame(s) of ~52x52 (2px short of 6 full 52px-wide frames (312px), likely padding/trim or variable-width frames) |
| `Actor/Boss/GiantSlime/Idle.png` | 310x52 | non-uniform: width 310 is not a clean multiple of height 52; best guess ~6 frame(s) of ~52x52 (2px short of 6 full 52px-wide frames (312px), likely padding/trim or variable-width frames) |
| `Actor/Boss/GiantSlime/Jump.png` | 806x52 | non-uniform: width 806 is not a clean multiple of height 52; best guess ~16 frame(s) of ~52x52 (26px short of 16 full 52px-wide frames (832px), likely padding/trim or variable-width frames) |
| `Actor/Boss/TenguBlue/Attack.png` | 1230x82 | 15 frame(s) of 82x82 |
| `Actor/Boss/TenguBlue/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Boss/TenguBlue/Hit.png` | 544x68 | 8 frame(s) of 68x68 |
| `Actor/Boss/TenguBlue/Idle.png` | 408x68 | 6 frame(s) of 68x68 |
| `Actor/Boss/TenguBlue/Trans.png` | 748x68 | 11 frame(s) of 68x68 |
| `Actor/Boss/TenguBlue/Walk.png` | 820x82 | 10 frame(s) of 82x82 |
| `Actor/Boss/TenguRed/Attack.png` | 1230x82 | 15 frame(s) of 82x82 |
| `Actor/Boss/TenguRed/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Boss/TenguRed/Hit.png` | 656x82 | 8 frame(s) of 82x82 |
| `Actor/Boss/TenguRed/Idle.png` | 492x82 | 6 frame(s) of 82x82 |
| `Actor/Boss/TenguRed/Trans.png` | 902x82 | 11 frame(s) of 82x82 |
| `Actor/Boss/TenguRed/Walk.png` | 820x82 | 10 frame(s) of 82x82 |

### Boss file inventory (which animation states exist per boss)

Determines which bosses have a charge-then-attack sequence (both a `Charge*` and an `Attack*` file present).

| Boss | Charge | Attack | Hit | Idle | Walk | Trans | Jump | Charge-then-attack? | Notes |
|------|------|------|------|------|------|------|------|------|------|
| GiantRedSamurai | yes | yes | yes | yes | yes | - | - | YES | Charge{Left,Right} + Attack{Left,Right}, directional pair |
| GiantBlueSamurai | yes | yes | yes | yes | yes | - | - | YES | Charge{Left,Right} + Attack{Left,Right}, directional pair |
| TenguRed | - | yes | yes | yes | yes | yes | - | no | attacks directly (no wind-up); also has Trans (transformation) |
| TenguBlue | - | yes | yes | yes | yes | yes | - | no | attacks directly (no wind-up); also has Trans (transformation) |
| GiantFrog | yes | yes | yes | yes | - | - | yes | YES | Charge + Attack, single (non-directional) pair |
| DemonCyclop | - | - | yes | yes | yes | - | - | no | no Attack/Charge file; has an odd non-strip Sprite.png instead |
| GiantSlime | - | - | yes | yes | - | - | yes | no | no Attack/Charge file; likely attacks via Jump |

## Actor/Monster (64x64 sheet = 4 frames x 4 direction rows of 16x16)

| Path | Size | Frame layout |
|------|------|--------------|
| `Actor/Monster/Bear/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Monster/Bear/SpriteSheet.png` | 64x64 | 4 frames x 4 direction rows of 16x16 (down, up, left, right, top to bottom) |
| `Actor/Monster/Larva/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Monster/Larva/Larva.png` | 64x64 | 4 frames x 4 direction rows of 16x16 (down, up, left, right, top to bottom) |
| `Actor/Monster/Mole/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Monster/Mole/Mole.png` | 64x64 | 4 frames x 4 direction rows of 16x16 (down, up, left, right, top to bottom) |
| `Actor/Monster/Mushroom/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Monster/Mushroom/mushroom.png` | 64x64 | 4 frames x 4 direction rows of 16x16 (down, up, left, right, top to bottom) |
| `Actor/Monster/Owl/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Monster/Owl/Owl.png` | 64x64 | 4 frames x 4 direction rows of 16x16 (down, up, left, right, top to bottom) |
| `Actor/Monster/Skull/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Monster/Skull/SpriteSheet.png` | 64x64 | 4 frames x 4 direction rows of 16x16 (down, up, left, right, top to bottom) |
| `Actor/Monster/Slime/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Monster/Slime/Slime.png` | 64x64 | 4 frames x 4 direction rows of 16x16 (down, up, left, right, top to bottom) |
| `Actor/Monster/Snake/Faceset.png` | 38x38 | single 38x38 portrait (not a strip) |
| `Actor/Monster/Snake/Snake.png` | 64x64 | 4 frames x 4 direction rows of 16x16 (down, up, left, right, top to bottom) |

## FX (square frame == sheet height)

| Path | Size | Frame layout |
|------|------|--------------|
| `FX/Attack/CircularSlash/SpriteSheet.png` | 128x32 | 4 frame(s) of 32x32 |
| `FX/Attack/Claw/SpriteSheet.png` | 128x32 | 4 frame(s) of 32x32 |
| `FX/Attack/ClawDouble/SpriteSheet.png` | 128x32 | 4 frame(s) of 32x32 |
| `FX/Attack/Cut/SpriteSheet.png` | 128x32 | 4 frame(s) of 32x32 |
| `FX/Attack/CutDouble/SpriteSheet.png` | 160x32 | 5 frame(s) of 32x32 |
| `FX/Attack/CutX/SpriteSheet.png` | 128x32 | 4 frame(s) of 32x32 |
| `FX/Attack/SlashCurved/SpriteSheet.png` | 128x32 | 4 frame(s) of 32x32 |
| `FX/Attack/SlashDoubleCurved/SpriteSheet.png` | 128x32 | 4 frame(s) of 32x32 |
| `FX/Magic/Aura/SpriteSheet.png` | 125x24 | non-uniform: width 125 is not a clean multiple of height 24; best guess ~5 frame(s) of ~24x24 (5px more than 5 full 24px-wide frames (120px), likely padding/trim or variable-width frames) |
| `FX/Magic/Circle/SpriteSheetOrange.png` | 128x32 | 4 frame(s) of 32x32 |
| `FX/Magic/Circle/SpriteSheetSpark.png` | 192x32 | 6 frame(s) of 32x32 |
| `FX/Magic/Circle/SpriteSheetSpark2.png` | 160x32 | 5 frame(s) of 32x32 |
| `FX/Magic/Circle/SpriteSheetWhite.png` | 128x32 | 4 frame(s) of 32x32 |
| `FX/Magic/Shield/SpriteSheetBlue.png` | 144x26 | non-uniform: width 144 is not a clean multiple of height 26; best guess ~6 frame(s) of ~26x26 (12px short of 6 full 26px-wide frames (156px), likely padding/trim or variable-width frames) |
| `FX/Magic/Shield/SpriteSheetYellow.png` | 144x26 | non-uniform: width 144 is not a clean multiple of height 26; best guess ~6 frame(s) of ~26x26 (12px short of 6 full 26px-wide frames (156px), likely padding/trim or variable-width frames) |
| `FX/Magic/Spark/SpriteSheet.png` | 270x35 | non-uniform: width 270 is not a clean multiple of height 35; best guess ~8 frame(s) of ~35x35 (10px short of 8 full 35px-wide frames (280px), likely padding/trim or variable-width frames) |
| `FX/Particle/Bamboo.png` | 96x15 | non-uniform: width 96 is not a clean multiple of height 15; best guess ~6 frame(s) of ~15x15 (6px more than 6 full 15px-wide frames (90px), likely padding/trim or variable-width frames) |
| `FX/Particle/Clouds.png` | 80x36 | non-uniform: width 80 is not a clean multiple of height 36; best guess ~2 frame(s) of ~36x36 (8px more than 2 full 36px-wide frames (72px), likely padding/trim or variable-width frames) |
| `FX/Particle/Fire.png` | 96x12 | 8 frame(s) of 12x12 |
| `FX/Particle/Grass.png` | 72x13 | non-uniform: width 72 is not a clean multiple of height 13; best guess ~6 frame(s) of ~13x13 (6px short of 6 full 13px-wide frames (78px), likely padding/trim or variable-width frames) |
| `FX/Particle/Leaf.png` | 72x7 | non-uniform: width 72 is not a clean multiple of height 7; best guess ~10 frame(s) of ~7x7 (2px more than 10 full 7px-wide frames (70px), likely padding/trim or variable-width frames) |
| `FX/Particle/LeafPink.png` | 72x7 | non-uniform: width 72 is not a clean multiple of height 7; best guess ~10 frame(s) of ~7x7 (2px more than 10 full 7px-wide frames (70px), likely padding/trim or variable-width frames) |
| `FX/Particle/Rain.png` | 24x8 | 3 frame(s) of 8x8 |
| `FX/Particle/RainOnFloor.png` | 24x8 | 3 frame(s) of 8x8 |
| `FX/Particle/Rock.png` | 80x16 | 5 frame(s) of 16x16 |
| `FX/Particle/RockGray.png` | 80x16 | 5 frame(s) of 16x16 |
| `FX/Particle/Snow.png` | 56x8 | 7 frame(s) of 8x8 |
| `FX/Particle/Spark.png` | 70x8 | non-uniform: width 70 is not a clean multiple of height 8; best guess ~9 frame(s) of ~8x8 (2px short of 9 full 8px-wide frames (72px), likely padding/trim or variable-width frames) |
| `FX/Particle/Vase.png` | 84x14 | 6 frame(s) of 14x14 |
| `FX/Particle/Wood.png` | 96x16 | 6 frame(s) of 16x16 |
| `FX/Slash/SpriteSheetArc.png` | 228x34 | non-uniform: width 228 is not a clean multiple of height 34; best guess ~7 frame(s) of ~34x34 (10px short of 7 full 34px-wide frames (238px), likely padding/trim or variable-width frames) |
| `FX/Slash/SpriteSheetCircular.png` | 378x55 | non-uniform: width 378 is not a clean multiple of height 55; best guess ~7 frame(s) of ~55x55 (7px short of 7 full 55px-wide frames (385px), likely padding/trim or variable-width frames) |
| `FX/Slash/SpriteSheetMulti.png` | 270x30 | 9 frame(s) of 30x30 |
| `FX/Slash/SpriteSheetSlash01.png` | 130x32 | non-uniform: width 130 is not a clean multiple of height 32; best guess ~4 frame(s) of ~32x32 (2px more than 4 full 32px-wide frames (128px), likely padding/trim or variable-width frames) |
| `FX/Slash/SpriteSheetSlash02.png` | 396x50 | non-uniform: width 396 is not a clean multiple of height 50; best guess ~8 frame(s) of ~50x50 (4px short of 8 full 50px-wide frames (400px), likely padding/trim or variable-width frames) |
| `FX/Slash/SpriteSheetSlash03.png` | 228x42 | non-uniform: width 228 is not a clean multiple of height 42; best guess ~5 frame(s) of ~42x42 (18px more than 5 full 42px-wide frames (210px), likely padding/trim or variable-width frames) |
| `FX/Smoke/Smoke/SpriteSheet.png` | 192x32 | 6 frame(s) of 32x32 |
| `FX/Smoke/SmokeCircular/SpriteSheet.png` | 240x14 | non-uniform: width 240 is not a clean multiple of height 14; best guess ~17 frame(s) of ~14x14 (2px more than 17 full 14px-wide frames (238px), likely padding/trim or variable-width frames) |

## Ui (single images: cursor, dialog panels/frames, heart/HP icons)

Curated subset only -- the source pack's Ui/ tree also has fonts, gamepad/keyboard icon sets, emotes, skill icons and full widget themes that were not copied (out of scope for this pass).

| Path | Size | Frame layout |
|------|------|--------------|
| `Ui/Arrow.png` | 13x13 | single image, menu-selection cursor arrow, not a strip |
| `Ui/Dialog/ChoiceBox.png` | 64x20 | single image, dialog panel/frame element, not a strip |
| `Ui/Dialog/DialogBox.png` | 300x58 | single image, dialog panel/frame element, not a strip |
| `Ui/Dialog/DialogBoxFaceset.png` | 300x58 | single image, dialog panel/frame element, not a strip |
| `Ui/Dialog/DialogInfo.png` | 80x16 | single image, dialog panel/frame element, not a strip |
| `Ui/Dialog/DialogueBoxSimple.png` | 316x60 | single image, dialog panel/frame element, not a strip |
| `Ui/Dialog/FacesetBox.png` | 48x48 | single image, dialog panel/frame element, not a strip |
| `Ui/Dialog/NoButton.png` | 26x16 | single image, dialog panel/frame element, not a strip |
| `Ui/Dialog/YesButton.png` | 26x16 | single image, dialog panel/frame element, not a strip |
| `Ui/Receptacle/Heart.png` | 80x16 | single image, heart/HP icon or bar segment, not a strip |
| `Ui/Receptacle/Heart2.png` | 64x16 | single image, heart/HP icon or bar segment, not a strip |
| `Ui/Receptacle/Heart3.png` | 64x16 | single image, heart/HP icon or bar segment, not a strip |
| `Ui/Receptacle/IconHeart.png` | 14x12 | single image, heart/HP icon or bar segment, not a strip |
| `Ui/Receptacle/LifeBarMiniProgress.png` | 18x4 | single image, heart/HP icon or bar segment, not a strip |
| `Ui/Receptacle/LifeBarMiniUnder.png` | 18x4 | single image, heart/HP icon or bar segment, not a strip |

