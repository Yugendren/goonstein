# Quaternius humanoid cast (CC0)

Realistic/anime-proportion rigged humans, to compare against the KayKit chibi cast. Everything
here is CC0 1.0 (`LICENSE.txt`); see ASSETS.md for the source URLs.

| File | What |
|------|------|
| `ranger_male.glb` | hooded fantasy ranger, 13 meshes, 53 joints, 84 clips. `assets/characters/q_hero.txt` |
| `peasant_male.glb` | plain villager, 8 meshes, 53 joints, 84 clips. `assets/characters/q_villager.txt` |
| `sword_bronze.glb` | one static blade for the ranger's right hand (the outfits ship no weapons) |

## How they were built

Quaternius ships the mesh and the animation in different downloads, so `merge.py` fuses them:

    blender -b --python merge.py -- OUT.glb BODY.gltf OUTFIT.gltf UAL1.glb UAL2.glb

It re-binds the base body to the outfit's armature, trims that body down to the head (the outfit
already supplies torso, arms with hands, legs and feet, and the bare mannequin otherwise pushes
straight through the clothes), moves all 84 Universal Animation Library actions onto the one rig as
NLA tracks, drops the normal/ORM maps the engine never reads, and folds the fingertip and toe-tip
"leaf" bones into their parents -- the stock rig is 65 bones and `MODEL_MAX_JOINTS` is 64, so the
skin has to lose one. The result is 53 joints.

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
