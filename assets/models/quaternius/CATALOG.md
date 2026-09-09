# Quaternius humanoid cast (CC0)

Realistic/anime-proportion rigged humans, to compare against the KayKit chibi cast. Everything
here is CC0 1.0 (`LICENSE.txt`); see ASSETS.md for the source URLs.

| File | What |
|------|------|
| `ranger_male.glb` | hooded fantasy ranger, 13 meshes, 53 joints, 84 clips. `characters/hero.txt`, `q_hero.txt` |
| `peasant_male.glb` | plain villager, 8 meshes, 53 joints, 84 clips. `characters/q_villager.txt` |
| `elder_male.glb` | peasant, long hair and beard bleached white, linen desaturated cold. `characters/elder.txt` |
| `smith_male.glb` | peasant burnt down to dark leather, buzz cut and black beard. `characters/smith.txt` |
| `warden_male.glb` | ranger in dark iron and blood red, 88 clips (84 + 4 aliases). `characters/warden.txt` |
| `sword_bronze.glb` | one static blade for the right hand (the outfit packs ship no weapons) |

The free "Standard" tier of Modular Character Outfits - Fantasy ships **only** peasant and ranger,
male and female -- no apron, no armour, no knight. So the villagers are all the one peasant outfit
repainted, and the ranger (pauldron, bracers, hood, belts) is the most armoured body there is; the
warden is that ranger, scaled up and recoloured. The free Fantasy Props MegaKit has no two-handed
weapon either (`Sword_Bronze`, `Axe_Bronze`, `Pickaxe_Bronze`), so the warden swings the bronze
sword at 1.8x. What keeps the cast apart is hair, build, scale and colour.

## How they were built

Quaternius ships the mesh and the animation in different downloads, so `merge.py` fuses them:

    blender -b --python merge.py -- OUT.glb BODY.gltf OUTFIT.gltf UAL1.glb UAL2.glb \
        [--add HAIR.gltf ...] [--tex N] [--alias NEW=CLIP ...] [--tint PREFIX R G B [SAT] ...]

It re-binds the base body to the outfit's armature, trims that body down to the head (the outfit
already supplies torso, arms with hands, legs and feet, and the bare mannequin otherwise pushes
straight through the clothes), moves all 84 Universal Animation Library actions onto the one rig as
NLA tracks, drops the normal/ORM maps the engine never reads, and folds the fingertip and toe-tip
"leaf" bones into their parents -- the stock rig is 65 bones and `MODEL_MAX_JOINTS` is 64, so the
skin has to lose one. The result is 53 joints.

`--add` puts another mesh rigged to the same universal skeleton on that rig: the Universal Base
Characters hairstyles (`Hair_Long`, `Hair_Beard`, `Hair_Buzzed`, `Hair_Buns`, ...) ship "Rigged to
Head Bone" and drop straight in.

`--tint` repaints a material (or a mesh) by name prefix: it pulls saturation to `SAT` and then
multiplies by `R G B`. The pack ships one atlas per outfit, so without it every peasant is the same
peasant. The game's lighting mostly normalises brightness away, so hue and saturation are the levers
that read on screen -- the elder is the peasant map at `sat 0.15` tinted cold, the smith the same map
at `sat 0.25` tinted dark brown. A material is repainted by the first rule that names it; a later
rule that wants an atlas an earlier one already touched gets its own copy of it, which is how the
warden's red cloth (`MI_Ranger.001`) and iron pauldrons (`MI_Ranger`) come out of the one ranger map.

`--alias NEW=CLIP` exports a clip a second time under another name. `assets/enemies/warden.txt` and
`warden_battle.txt` call the boss's moves by clip name and those names are KayKit's, so
`warden_male.glb` carries all four as second names for the closest Quaternius swings:

| name the enemy files ask for | Quaternius clip |
|---|---|
| `2H_Melee_Attack_Slice` | `Sword_Regular_A` |
| `2H_Melee_Attack_Stab` | `Sword_Regular_B` |
| `2H_Melee_Attack_Chop` | `Sword_Heavy_Combo` |
| `Unarmed_Melee_Attack_Punch_A` | `Melee_Hook` |

Neither enemy file needed changing.

## Rebuilding the cast

Unzip the four free packs (see ASSETS.md for the URLs); `BODY`/`OF`/`HAIR` below are
`Base Characters/Godot - UE`, `Exports/glTF (Godot-Unreal)/Outfits` and
`Hairstyles/Rigged to Head Bone/glTF (Godot -Unreal)`, `A1`/`A2` the two `UAL*_Standard.glb`.

    blender -b --python merge.py -- elder_male.glb $BODY/Superhero_Male_FullBody.gltf \
        $OF/Male_Peasant.gltf $A1 $A2 --add $HAIR/Hair_Long.gltf --add $HAIR/Hair_Beard.gltf \
        --tex 512 --tint MI_Hair_1 2.4 2.4 2.3 0.15 --tint MI_Hair_2 2.4 2.4 2.3 0.15 \
        --tint MI_Peasant 0.95 1.00 1.25 0.15

    blender -b --python merge.py -- smith_male.glb $BODY/Superhero_Male_FullBody.gltf \
        $OF/Male_Peasant.gltf $A1 $A2 --add $HAIR/Hair_Buzzed.gltf --add $HAIR/Hair_Beard.gltf \
        --tex 512 --tint MI_Hair_1 0.30 0.26 0.24 0.30 --tint MI_Hair_2 0.30 0.26 0.24 0.30 \
        --tint MI_Peasant 0.50 0.42 0.36 0.25

        $OF/Female_Peasant.gltf $A1 $A2 --add $HAIR/Hair_Buns.gltf --tex 512 \
        --tint MI_Hair_1 1.60 0.80 0.40 1.0 --tint MI_Hair_2 1.60 0.80 0.40 1.0 \
        --tint MI_Peasant 1.35 1.05 0.35 1.0

    blender -b --python merge.py -- warden_male.glb $BODY/Superhero_Male_FullBody.gltf \
        $OF/Male_Ranger.gltf $A1 $A2 --tex 512 \
        --tint MI_Ranger.001 1.05 0.34 0.30 0.12 --tint MI_Ranger 0.72 0.80 1.00 0.06 \
        --alias 2H_Melee_Attack_Slice=Sword_Regular_A --alias 2H_Melee_Attack_Stab=Sword_Regular_B \
        --alias 2H_Melee_Attack_Chop=Sword_Heavy_Combo --alias Unarmed_Melee_Attack_Punch_A=Melee_Hook

## Clip list

The rig is the Unreal-style universal humanoid (`root`, `pelvis`, `spine_01..03`, `neck_01`,
`Head`, `clavicle/upperarm/lowerarm/hand_l|r` with full fingers, `thigh/calf/foot/ball_l|r`), so any
other Quaternius pack on the same rig drops straight in. The 84 clips, grouped:

**locomotion** (15): `Walk_Loop`, `Walk_Formal_Loop`, `Walk_Carry_Loop`, `Jog_Fwd_Loop`, `Sprint_Loop`, `Crouch_Fwd_Loop`, `Crouch_Idle_Loop`, `Push_Loop`, `Swim_Fwd_Loop`, `Swim_Idle_Loop`, `Slide_Start`, `Slide_Loop`, `Slide_Exit`, `ClimbUp_1m`, `Roll`

**jump** (6): `Jump_Start`, `Jump_Loop`, `Jump_Land`, `NinjaJump_Start`, `NinjaJump_Idle_Loop`, `NinjaJump_Land`

**idle** (10): `Idle_Loop`, `Idle_Talking_Loop`, `Idle_FoldArms_Loop`, `Idle_No_Loop`, `Idle_Torch_Loop`, `Idle_Lantern_Loop`, `Idle_Rail_Loop`, `Idle_Rail_Call`, `Idle_TalkingPhone_Loop`, `Yes`

**sword** (11): `Sword_Idle`, `Sword_Attack`, `Sword_Regular_A`, `Sword_Regular_A_Rec`, `Sword_Regular_B`, `Sword_Regular_B_Rec`, `Sword_Regular_C`, `Sword_Regular_Combo`, `Sword_Heavy_Combo`, `Sword_Block`, `Sword_Dash`

**shield** (4): `Idle_Shield_Loop`, `Idle_Shield_Break`, `Shield_Dash`, `Shield_OneShot`

**unarmed** (5): `Punch_Jab`, `Punch_Cross`, `Melee_Hook`, `Melee_Hook_Rec`, `OverhandThrow`

**damage** (5): `Hit_Chest`, `Hit_Head`, `Hit_Knockback`, `Death01`, `LayToIdle`

**spell/gun** (10): `Spell_Simple_Enter`, `Spell_Simple_Idle_Loop`, `Spell_Simple_Shoot`, `Spell_Simple_Exit`, `Pistol_Idle_Loop`, `Pistol_Aim_Up`, `Pistol_Aim_Neutral`, `Pistol_Aim_Down`, `Pistol_Shoot`, `Pistol_Reload`

**sit/interact** (11): `Sitting_Enter`, `Sitting_Idle_Loop`, `Sitting_Talking_Loop`, `Sitting_Exit`, `Driving_Loop`, `Interact`, `PickUp_Table`, `Chest_Open`, `Consume`, `Fixing_Kneeling`, `Dance_Loop`

**work** (4): `Farm_PlantSeed`, `Farm_Watering`, `Farm_Harvest`, `TreeChopping_Loop`

**zombie** (3): `Zombie_Idle_Loop`, `Zombie_Walk_Fwd_Loop`, `Zombie_Scratch`
