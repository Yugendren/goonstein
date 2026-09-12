# Why the gun looked bad, and the rules that fixed it

It was almost never the mesh. A Poly Haven photoscan of a service pistol is a perfectly good gun;
it was being drawn through the world's 70-degree lens (78 at a sprint) at arm's length, with two
untextured coloured boxes standing in for arms, recoil that was a linear ramp and nothing else, a
flash pasted 35 cm down the aim vector instead of at the muzzle, no shell coming out of the gun, no
reload animation, and a crosshair that told the truth about none of it. Fix the picture, not the
model, and the same pistol reads as a gun instead of a prop. Everything below is `src/weaponview.c`
(the viewmodel, its own lens, its recoil springs, its HUD), `src/camera.c` (the two lenses that
share one eye) and `src/gfx.c`/`src/gfx.h` (the depth-range trick that lets the viewmodel sit in
front of the world without clearing anything), read symbol by symbol rather than described from
memory.

## The rules

### 1. A separate viewmodel field of view

`VM_FOV_DEFAULT` is 58 degrees, overridable live with `HOLLOW_VM_FOV` (clamped 20-110 by `vm_fov()`
in `weaponview.c`), and it is built by `camera_view_proj_lens()` through exactly the same eye,
target, roll and shake as the world lens — `camera_view_basis()` supplies all four, so the gun
never swims against the world the way it would if the two lenses disagreed about where the camera
actually is. `gfx_set_view_proj()` swaps the projection in for the viewmodel draw only, and
`gfx_reset_view_proj()` puts the world's back immediately after. The world itself stays at
`FP_FOV` 70 degrees in `camera.c` and opens by `FP_FOV_SPRINT` (8 degrees) at a flat-out sprint —
that widening is the oldest trick in first-person running for selling speed, and it is exactly why
the gun must not share the lens: a gun held at arm's length through a 78-degree lens stretches into
a plank the moment you start running. Half-Life 2 and Titanfall 2 expose a separate viewmodel FOV
as a setting; Doom 2016 bakes it in without exposing it. Quake's viewmodel is smaller and closer to
the camera for the same underlying reason — a wide lens is honest about the world and dishonest
about anything held a few centimetres from the eye.

### 2. Drawn last, into its own slice of the depth buffer

`weapons_draw_viewmodel()` is called from `game.c` after `particles_draw`, and the reason is
`gfx_depth_range(x, 0.0f, VM_DEPTH)` (`VM_DEPTH` is 0.12) at the top of its own-lens block, restored
to `0, 1` at the bottom. Squeezing everything the viewmodel draws into the near eighth of the depth
buffer puts the gun in front of every piece of world geometry without touching what is already
written there, so a barrel pressed against a wall stays a barrel instead of the wall winning the
depth test and swallowing it. SDL_GPU cannot clear the depth buffer in the middle of a render pass —
ending the world pass and reopening a new one to clear depth would throw away the depth the sky, the
particles and the composited character layer still need to draw correctly behind and in front of
each other. `gfx_depth_range` is the same trick as OpenGL's `glDepthRange`, which Quake-era and
Source-engine renderers used for exactly this: a depth-range squeeze costs one call and keeps the
rest of the frame's depth intact; a mid-frame clear costs a whole pass boundary and loses it.

### 3. Framing

`VM_REST_RIGHT` (0.145), `VM_REST_DOWN` (0.175) and `VM_REST_FWD` (0.500) put the gun's rest
position down and to the right of centre, with the muzzle angled back toward the middle of the
screen rather than pointed straight out of frame — `vm_rest()` reads them, and `HOLLOW_VM_REST="right
down fwd"` overrides all three live for tuning by eye. Doom and Quake both parked their viewmodel in
the lower-right third of the screen, and for the reason `weaponview.c`'s own comment gives: it is
the one part of the frame a player never has to read to play the game, so it is the one part of the
frame a gun is allowed to occupy without getting in the way of aiming, jumping or reading the HUD.

### 4. Real hands

The arms on screen are the local goon's own MakeHuman body, trimmed to the trigger arm alone by
`tools/blender/make_arms.py --side right`, keeping the full skeleton (the script's own comment says
52 bones survive; `assets/characters/*_fp.txt` is what `vm_arms()` in `weaponview.c` looks for,
derived from the goon's own model filename so a new goon needs no table entry) and the pistol clips
the third-person body already plays. They are posed by `charmodel_drive_simple()` against a
`Character` that `s_arms.body` fabricates purely so the clip player has state to read — it is never
replicated, never saved, never reset with the level, because it belongs to whoever is sitting at
this keyboard, not to the simulation. The arms are hung off the gun rather than the other way
round: the weapon sits in `frame` first, with the recoil springs, the sprint pose and the reload
curve already applied to it, and then the arms model is placed so `hand_r` (found once per pose
switch by `charmodel_bone_posed()`, and anchored from the settled aim pose after a quarter-second
cross-fade so that the clip's own later motion still shows in the hand) lands exactly on `frame`'s
origin. Doing it in that order is what keeps the barrel pointed down the middle of the screen — a
gun hung off a bone points wherever the animator aimed a body standing in a field, which is never
where a first-person crosshair sits.

It is one arm, not two, and honestly so: the free CC0 animation library this cast is rigged against
has one-handed pistol clips and no two-handed rifle clip, so a second arm would be holding air in
the middle of the picture. Doom and Quake drew one hand for the same reason — a viewmodel with
nothing to hold in one of its hands is worse than a viewmodel with only one hand.
`HOLLOW_VM_ARMS="yaw pitch roll"` (`vm_arms_turn()`) exists for the day a two-handed clip arrives
and the second arm needs nudging out of the middle of the picture; it does nothing today because the
comment in the code says plainly that a single trigger arm needs no correction.

### 5. Lighting

The viewmodel is lit by the same `FrameParams` as the rest of the frame — the same sun, ambient,
fog and point lights `gfx.h`'s `FrameParams` carries for the world pass — plus a rim: `mat.rim =
0.35f; mat.rim_color = v3(0.85f, 0.90f, 1.0f)` in `weapons_draw_viewmodel()`, a cool near-white-blue
that keeps the gun's silhouette readable against a dark wall without making it look like it is
standing in a different room from the one it is actually in.

### 6. Recoil is a spring, not a curve

`vm_recoil()` hands each shot's kick to `vm_spring()`, one axis of a critically damped spring
advanced every rendered frame by `vm_advance()`, with `VM_KICK_OMEGA` at 34 rad/s — critically
damped so the gun settles back to rest in about 120 ms without overshooting into a wobble, which is
what a spring that is merely damped, rather than critically damped, would do. Because it is a
spring rather than a fixed-length animation, a second shot fired before the first has settled adds
its own velocity on top of whatever is left, so a fast gun's recoil visibly stacks instead of
restarting an animation halfway through — which is what actually sells a high rate of fire. The kick
moves `ws->vm.back/up/side` (position: back along the barrel, up out of the rest pose, and
sideways) and `ws->vm.pitch/yaw/roll` (muzzle rise, and a sideways twitch that alternates sign every
shot via `(ws->vm.shots++ & 1u)` so it never drifts one way over a magazine), and rides alongside
`camera_add_shake()` and `camera_add_fov_punch()`. The FOV punch is added in
`camera_view_proj_offset()` as `(c->fov + c->fov_punch)` rather than folded into `c->fov` itself,
because `c->fov` is damped toward `FP_FOV` every frame by `camera_view_advance()` and would
otherwise absorb a 120 ms kick into nothing before it was ever seen. Every one of these is weighted
by `heft`, computed in `vm_recoil()` from the item file's own `damage` and `rate`
(`clampf((d->damage / 60.0f) * (1.4f - clampf(d->rate / 8.0f, 0, 0.9f)), 0.35f, 2.2f)`), so a slow
heavy gun shoves harder than a fast light one out of the box, with no per-gun recoil code anywhere.

### 7. Sprint puts the gun away

`VM_SPRINT_DROP` (0.085 m), `VM_SPRINT_ROLL` (26 degrees), `VM_SPRINT_YAW` (17 degrees) and
`VM_SPRINT_PITCH` (13 degrees) pull the gun down, roll it across the body and tip the muzzle up,
weighted by `ws->vm.sprint`, which `vm_advance()` computes from `hypotf(p->c.hvel.x, p->c.hvel.z)` —
the player body's own measured ground speed — rather than from whether the sprint key is held. A
goon shoved down a hill, thrown by an impact, or otherwise moving fast for a reason that has nothing
to do with pressing Shift still gets the gun put away, because the rule the game is trying to
communicate is "you are moving too fast to shoot", not "you are pressing a particular key".

### 8. The flash goes on the muzzle

An item file's `muzzle x y z` line (measured once per weapon by hand, in the model's own frame —
see `pistol.txt`'s and `rifle.txt`'s own comments on how those numbers were found) is read by
`vm_muzzle_local()`, which falls back to `v3(0, d->half.y * 0.55f, d->half.z * 1.55f)` — a point
near the front of the item's own bounding box — for anything that has not been measured yet. Two
additive billboards are drawn there inside the viewmodel pass (`gfx_billboard`, one tight and bright,
one larger and softer, both fading with `ws->vm.flash` over `VM_FLASH_VM` = 0.045 s), and
`weapons_lights()` adds a short-lived world-space point light through the same event so a shot in a
dark room lights the room regardless of who fired it. The world-space flash billboard
(`draw_flashes()`, driven by `ws->flash[i].world_vis`) is deliberately suppressed for your own
first-person shot — `push_flash(ws, muzzle, !first_person)` in `weapons_event_apply()` — because
that shot's flash is already being drawn on the model's real muzzle, through the viewmodel's own
lens; drawing both would put two flashes in two different pictures for the one shot you actually
fired.

### 9. Spent cases

The shells `vm_recoil()` spawns into `ws->vm.shell[]` are stored and drawn in camera-local metres
(offsets from `VM_REST_RIGHT/DOWN/FWD` plus the item's own `eject x y z` line, or a small default
offset if it has none), not in world space, and drawn inside the same viewmodel-lens block as the
gun itself. A world-space case would be positioned correctly for the world's lens and wrong for the
narrower one the gun is actually drawn through, so it would either swim relative to the gun it just
left or need its own duplicate lens math; camera-local is the only frame that stays put in the
picture the gun is standing in. `vm_advance()` ages them out over `VM_SHELL_LIFE` (0.85 s), applies
`VM_SHELL_G` (7.5 m/s², chosen light rather than a true 9.8 so the arc is slow enough to actually
read in the corner of the eye) and spins each one by its stored `spin`.

### 10. The reload is animated by code

`vm_reload_curve()` is not a clip — the clip belongs to the third-person body's rig, which is not
what is on screen in first person. It computes `r`, the reload's own progress from 0 to 1, and a
`down` value built from two smoothsteps: one rising over the first 22% of the reload as the gun
drops out of frame and rolls (`smoothstep(r / 0.22)`), held near its peak until 62%, and one falling
back to level over the final 30% (`1 - smoothstep((r - 0.62) / 0.30)`) as the magazine is dealt with
and the gun comes back up. `*roll = 34.0f * down` and `*pitch = 26.0f * down` are the numbers that
shape it. The comment beside the code states the intent plainly — "the snap back is deliberately
faster than the drop: a reload that ends slowly feels broken" — worth knowing before you retune
those two width constants, since as currently written the exit window (0.30) is wider than the entry
window (0.22), which is the opposite of what the comment says it should read as; see "What is still
wrong" below.

### 11. The crosshair tells the truth

The reticle's four ticks open with `ws->vm.bloom` (which rises 0.55 × `heft` per shot and decays at
2.6/s) and with `ws->vm.sprint` — a fixed cross would be a promise about where the next shot lands
that a gun with any spread or any recoil cannot keep. A hit marker (`weapons_draw_hud()`, four
diagonal ticks per corner) shows for 0.30 s, brighter and warmer (`v4(1.0, 0.95, 0.85, k)`) for a
goon than for a crate (`v4(0.75, 0.78, 0.75, k * 0.8)`), because at thirty metres a goon taking a
round looks exactly like a goon not taking one, and the marker is the only place the game can say
otherwise. `FE_IMPACT` in `weapons_event_apply()` is why that marker can arrive late on purpose: both
current guns fire a real projectile with travel time (`projectile ...` lines in `pistol.txt` and
`rifle.txt`), so the host cannot know what a shot hit at the instant the trigger is pulled — only
once the projectile itself has finished travelling and reports its own impact. Sending the marker
the moment the trigger is pressed would be a lie about a shot that has not landed yet; sending it
late, when the host actually knows, is the honest version of the same feedback.

### 12. No ADS

There is no aim-down-sights mode anywhere in `weaponview.c`, and none is planned. This is a physics
comedy about four idiots on an island, not a milsim, and a scope-in mode that asks the player to
line up a real sight picture belongs to a different kind of game than the one `DESIGN.md` describes.

### 13. Nothing in the effect path is ever blood

Every hit effect in `weapons_event_apply()` and `weapons_event_own_echo()` is grey dust
(`PT_SMOKE`, tinted somewhere between `v3(0.5, 0.45, 0.4)` and `v3(0.6, 0.6, 0.6)`), a spark burst
for an item, or a dull thud (`SND_HIT`, `SND_THUD`) — never red, never gore, and the code comments
say so directly ("No gore, no red, nothing that reads as injury"). `DESIGN.md`'s pillars are
"physics comedy over voice" and "fun and games, nothing serious", and the game's own README repeats
the same rule in plainer language when it describes guns: "There is no blood and no gore anywhere in
the effect path; that is a hard rule from `DESIGN.md`, not a preference." Worth being precise about
that citation: the current `DESIGN.md` does not contain the words "blood" or "gore" itself — the
rule lives in its pillars and in the game's own comedy premise (nobody in this game is meant to die;
a goon who is emptied of wind is "down", never "dead", never "killed") rather than in one explicit
sentence. See "What is still wrong" for the exact wording this document found, and none.

## Where the guns come from

The pistol and the rifle are both Poly Haven CC0 photoscans — `service_pistol` and
`bolt_action_rifle_7_62` — and the bat and the wrench (`baseball_bat`, `pipe_wrench`) are the same
author's photoscans put to melee use; all four went through `tools/blender/weapon_prep.py` into the
weapon frame `ASSETS.md` defines (+Z the muzzle, +Y up, origin at the grip) and now live under
`assets/models/polyhaven/`. The ammo pickups (`ammo_pistol.txt`, `ammo_rifle.txt`) both use the
same Poly Haven "Ammo Box" scan (`assets/models/polyhaven/ammo_box/ammo_box.glb`). Sourcing beats
modelling wherever a reachable CC0 scan exists, and the whole arsenal is now sourced.

There used to be a fifth weapon, a pump shotgun, and it was the only one this repo had modelled
itself: a CadQuery solid from `tools/cad/build_weapons.py`, built that way because no CC0 photoscan
of a pump gun is reachable without a login. The research below confirms that gap is real and
permanent. It was still the wrong trade. A hand-built gun costs a parametric script, a Blender
staging pass and a triangle budget, and it comes out reading as the odd one out next to three
photoreal scans — and that cost would have to be paid again for every weapon after it. So the
shotgun, its model, its STEP and the whole weapon CAD bench were deleted rather than improved, and
a nail gun and a grenade launcher were dropped from the design before either was ever built. The
same research says why they were never going to be free: nobody gives away a CC0 nail gun without
a login wall, and the closest free grenade launchers are flat-shaded lowpoly models that would sit
oddly beside the photoscans. If photoreal versions of any of the three are ever wanted, the paid
table further down is how to buy them in an afternoon.

(The architecture CAD bench — `tools/cad/build_kit.py` and `build_shells.py` — is untouched. A
building is a thing this game genuinely has to author; a gun is not.)

### Free, no login, verified

Every row below was actually fetched with `curl` in this research pass, not assumed from a listing
page. "Taken?" records what the game's own item files actually use today, separately from what was
merely available.

| What | Slot | Source | Licence | Style family | Format | Tris | Direct download | Verified curl result | Taken? |
|---|---|---|---|---|---|---|---|---|---|
| Bolt Action Rifle 7.62 | rifle | Poly Haven, Mateusz Sadek — polyhaven.com/a/bolt_action_rifle_7_62 | CC0 | photoreal | glTF+bin+jpg | 19985 | `dl.polyhaven.org/.../bolt_action_rifle_7_62_1k.gltf` (+ `.bin`, diff maps) | gltf 200/15,369 B; bin 200/736,616 B; diffs 200 | **Yes** — `assets/models/polyhaven/bolt_action_rifle/` |
| Service Pistol | pistol | Poly Haven, Mateusz Sadek — polyhaven.com/a/service_pistol | CC0 | photoreal | glTF+bin+jpg | — | (fetched via `tools/polyhaven_get.py`) | verified working (already in ASSETS.md) | **Yes** — already in the tree |
| Drill 01 | nail-gun stand-in | Poly Haven, Fernando Quinn — polyhaven.com/a/Drill_01 | CC0 | photoreal | glTF+bin+jpg | 2926 | `dl.polyhaven.org/.../Drill_01_1k.gltf` | 200/2,637 B; bin 200/90,100 B; diff 200/106,186 B | No — no nail-gun item exists |
| Stick Grenade | bonus prop | Poly Haven, singaii — polyhaven.com/a/stick_grenade | CC0 | photoreal | glTF+bin+jpg | 6250 | `dl.polyhaven.org/.../stick_grenade_1k.gltf` | 200/2,785 B | No |
| Quaternius Shotgun_1 (Ultimate Gun Pack) | shotgun candidate | Quaternius, mirrored on OpenGameArt | CC0 | stylised-flat | OBJ+MTL | 1270 | `opengameart.org/.../ultimate_gun_pack_by_quaternius.zip` (whole pack) or Google Drive single-file link | zip 200/7,375,704 B; single OBJ 200/49,702 B, MTL 200/779 B | No — the game has no shotgun any more |
| Grenade Launcher (Quaternius) | grenade launcher candidate | poly.pizza — poly.pizza/m/ZKvWhvu4tV | CC0 1.0 | stylised-flat | GLB | 1259 | `static.poly.pizza/bcbc44eb-....glb` | 200/68,944 B, model/gltf-binary | No — no grenade launcher item exists |
| Blaster Kit 2.1 | rejected on style | Kenney — kenney.nl/assets/blaster-kit | CC0 | cartoon-lowpoly | OBJ+FBX+GLB | ~1000 avg | `kenney.nl/media/pages/.../kenney_blaster-kit_2.1.zip` | 200/1,724,676 B | No — Nerf-toy proportions, wrong family |

Four sources were tried and failed outright, and are worth recording so nobody tries them again
expecting a different result. **Blend Swap** is behind a Cloudflare interstitial even on its root
page (HTTP 403 before any login wall is reached) — not curl-able at all. **itch.io**'s browse and
tag pages return the same Cloudflare 403; individual creator pages load, but the download flow needs
a page-JS-derived key that a bare cookie jar cannot reproduce, so nothing was actually retrieved from
it. **Sketchfab**'s download endpoint is a flat 401 without an account, and its own open metadata
search turns up zero CC0, downloadable results for "pump shotgun", "nail gun", "grenade launcher" or
"flare gun" regardless. **Smithsonian Open Access 3D**'s public API answers with no key needed but
returns no firearms or tools at all — its 3D collection is natural history and spacecraft, not
weapons.

One licence conflict is worth flagging by name rather than taking quietly: Pichuliru's low-poly gun
models (`Shotgun Pump West` and others) are labelled **CC0 1.0 on poly.pizza** and **CC-BY 4.0 on
OpenGameArt** — the same author, the same content, contradictory labels, with no way to tell which
one the author actually intended. Neither copy is used in this game; `ASSETS.md`'s policy is CC0 /
OFL / MIT / Mixamo only, and a model whose own author cannot be pinned to one licence does not clear
that bar. The two OpenGameArt "Silent Nail Gun" and "Nailgun" models found in the same research pass
are both explicitly **CC-BY**, not CC0, and were not taken for the same reason — the best-looking
nail gun found anywhere in this search is one of them, and it still is not in the game.

### Paid, licensed for any engine

| Name | Seller | Price | Weapons | Formats | Tris/LODs | Texture style | Art style | Licence | Verdict |
|---|---|---|---|---|---|---|---|---|---|
| Low Poly FPS Weapons Pack | JustCreate3D (itch.io) | $9.99+ | 210 weapons (35 types x 6 colours) + grenades/attachments | FBX + GLB + Blender source | not stated, low hundreds to low thousands | one 1024 flat colour-palette atlas | clean stylised low-poly | itch seller terms, no engine restriction | **Allowed** — buy the itch copy, not the thinner Fab listing |
| POLYGON Battle Royale / Construction / Military | Synty Studios | $49.99 / $49.99 / $299.99 | 20+ weapons (Military); nail gun lives in Construction | FBX source + Unity/Unreal/Godot projects | ~300-2000 tris typical, no LODs | single shared palette atlas | flat-shaded stylised cartoon-realistic | Synty one-time-purchase EULA, engine-agnostic by name | **Allowed** — cleanest licence on the list; wait for a Humble bundle |
| Shooter Weapons Pack — Sci-Fi & Scopes + Grenades | FaxLab3D (Fab) | $13.99 Personal / $30.99 Pro | 15 assets incl. shotgun, grenade launcher | FBX + GLB + Blender + UE project | LOD0 5k-50k, LOD1 1k-20k, LOD2 0.5k-5k | full PBR (BaseColor/Normal/ORM) | semi-realistic fictional sci-fi | Fab Standard Licence | **Allowed**; budget an art pass to bake BaseColor-only |
| Poly Weapons Pack 01 | Fab | $14.99 Personal / $29.99 Pro | 52 weapons + 150+ modular parts, incl. shotguns/launchers | glb + fbx | not stated, "optimized low poly" | flat colours | stylised | Fab Standard Licence | **Allowed**; thin documentation, check the gallery first |
| Weapons Pack Low Poly | RGS_Dev (itch.io) | name-your-price | 10 real-firearm likenesses incl. SPAS-12, M32 launcher, flamethrower | FBX + OBJ + BLEND | not stated, no rigging | not specified | low-poly real-firearm | itch seller terms, commercial OK | **Allowed**, but modelled on real trademarked guns — a clearance risk the seller carries, not you |
| Game Assets All-in-1 | Kenney (itch.io) | $19.95+ | 60,000+ assets incl. Blaster Kit | OBJ, FBX, GLTF | not stated | flat atlas | cartoon-toy | CC0, no restriction at all | **Allowed**, zero risk, wrong style for this game |

Three findings from the paid research matter more than any single price. **Fab's Standard Licence
has no engine restriction** — "you can privately use the Content however you want under a Standard
License" — but the real trap is the "Included formats" field on a specific listing: a listing whose
formats field reads only **"Unreal Engine"** (the verified example is FaxLab3D's competitor,
"RetroCore Shooter Pack") gives you a `.uasset` blob with no supported way to get a mesh out of it,
regardless of what the licence permits. Fab's own clause 6(a) is worth checking against this
engine's own dependency list before buying anything: it forbids combining Standard-Licence content
with "GNU General Public License (GPL), Lesser GPL (LGPL) (unless you are merely dynamically linking
a shared library), or Creative Commons Attribution-ShareAlike License" — this codebase's own
third-party list (`stb_image`, `cgltf`, `SDL3`, `libopus`) is MIT/zlib/BSD/BSD-3-clause throughout,
so it clears that bar today, but it is the kind of check that has to be redone the day a new
dependency is added.

**Editorial licences forbid commercial game use outright, on every store that offers them.**
Sketchfab's Editorial licence "cannot be used for any commercial or promotional use" and exists
specifically for models depicting a real branded product the seller never cleared trademark on.
CGTrader's Editorial tier restricts use "for legitimate, editorial purposes on some issue of
journalistic, editorial, cultural or otherwise newsworthy value" because "such Products may contain
material that is not released from its rights holders." TurboSquid's Editorial Uses Only tier is
"limited to news reporting in Creations of some cultural, editorial, journalistic, or otherwise
newsworthy value." A commercial game is never any of those three things, on any of those three
stores, at any price.

**The Sketchfab Store closed to new purchases on 22 October 2024.** Sketchfab's own migration notice
says "All products previously listed on the Sketchfab Store will remain viewable on Sketchfab but
will no longer be available for purchase" and that existing buyers keep access through their
purchase history; sellers were pointed at Fab instead. Anything found through a search engine that
still points at a Sketchfab Store product page is a dead link now, not a live listing.

If you only buy one thing: **JustCreate3D's "Low Poly FPS Weapons Pack" on itch.io, $9.99** — 35
weapon types in FBX and GLB off one flat-colour atlas, a plain "use it in your compiled game, don't
resell the files" licence with no engine clause at all, and modular parts that would let a pump or a
cylinder actually animate. The cheapest acceptable option is **RGS_Dev's "Weapons Pack Low Poly" on
itch.io at name-your-own-price** — pay $5-10, accept real-firearm silhouettes and undocumented
tris/textures, and get the SPAS-12, the M32 grenade launcher and a flamethrower that nothing free
comes close to.

## Tuning without a rebuild

| Variable | Takes | Overrides |
|---|---|---|
| `HOLLOW_VM_FOV` | one float, degrees (clamped 20-110) | `VM_FOV_DEFAULT` (58), the viewmodel's own lens — `vm_fov()` |
| `HOLLOW_VM_REST` | `"right down fwd"`, three floats in metres | `VM_REST_RIGHT/DOWN/FWD` — where the gun sits at rest — `vm_rest()` |
| `HOLLOW_VM_ARMS` | `"yaw pitch roll"`, three floats in degrees | the arms' own turn about the grip point, without moving the gun — `vm_arms_turn()` |
| `HOLLOW_VM_HAND` | `"yaw pitch roll"`, three floats in degrees | the correction between the hand bone's rest orientation and the gun model's own frame — `vm_hand_fix()` |
| `HOLLOW_GRIP` | `"x y z yaw pitch roll scale"`, seven floats | every weapon's `grip` line at once, for finding a number by eye — `grip_xform()` |
| `HOLLOW_GRIP_<NAME>` | same seven floats, `<NAME>` = the item's own file name upper-cased (`PISTOL`, `RIFLE`, `SHOTGUN`, `BAT`, `WRENCH`) | one weapon's `grip` line only, so four guns can be tuned without four rebuilds — `grip_xform()` |

## What is still wrong

- **One hand, not two.** There is no two-handed rifle clip in the animation library this cast is
  rigged against, so a rifle is held exactly like a pistol, one-handed, with the same trigger arm.
  `HOLLOW_VM_ARMS` is wired up and waiting for the day a second hand has something to hold.
- **`weapons_trace` has no true surface normal for a level block.** `RayHit.normal` defaults to
  `-dir` (a fake normal pointed straight back at the shooter) and is only ever replaced with a real
  one — `terrain_normal()` — when the hit lands on terrain. A shot that hits a wall, a floor or any
  other level-geometry block gets the shooter's own reversed aim direction as its "surface normal",
  which is wrong for anything that would want to orient an effect (a decal, a spark cone) to the
  actual face it struck rather than back down the barrel. This is inherited by every hitscan and
  projectile-impact effect in the game, not something this document's viewmodel work introduced.
- **The reload's own snap-back is coded slower than its drop, not faster.** `vm_reload_curve()`'s
  comment states the intent as "the snap back is deliberately faster than the drop", but the
  constants as written give the drop a 0.22-wide entry and the snap-back a 0.30-wide exit — the
  snap-back takes longer, not less, of the reload's total time. Worth a second look against the
  stated intent rather than trusted at face value.
- **The muzzle flash's world-space point light has no equivalent suppression check for a remote
  viewer's own gun** the way the billboard does — `weapons_lights()` adds the light unconditionally
  from every `FE_SHOT`, which is correct (a shot should light a dark room for everyone), but it means
  the light and the viewmodel's own muzzle glow are two separate systems that happen to agree, not
  one system computing both; a future weapon whose flash timing diverges between the two would go
  unnoticed until someone looked for it.
- **The Poly Haven shotgun gap is permanent, not temporary.** No CC0 photoscan of a pump shotgun
  reachable without a login exists anywhere this research could find. The game's answer is to not
  have a shotgun rather than to have a hand-built one that does not match — a design decision, not
  a technical one, and reversible the day somebody buys one of the packs in the paid table.
- **One arm, not two.** The cast has no two-handed rifle clip, so the rifle — a 1.23 m bolt-action
  — is held in one hand. It reads as comedy rather than as a bug, which is the only reason it is
  acceptable, and it is the single thing a paid pack with proper hands would most obviously fix.
