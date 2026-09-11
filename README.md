# hollow

Working title. An HD-2D JRPG: pixel-art sprites living in a lit low-poly 3D world, an isometric
overworld, in-engine cutscenes, and card battles where the enemy's attack frames are rhythm beats
you deflect osu-style. C11 on SDL3, targeting macOS, Linux (Steam Deck) and Windows.

This is the skeleton build: one corridor, one cutscene going in, one boss, one cutscene coming out.

## Play it (testers)

macOS / Linux:

    curl -fsSL https://raw.githubusercontent.com/Yugendren/goonstein/main/get.sh | sh && ./goonstein/goonstein

Windows (PowerShell):

    irm https://raw.githubusercontent.com/Yugendren/goonstein/main/get.ps1 | iex

### Play together

Run the game and pick `HOST GAME`. The address to read out sits in the top-right corner of the
screen, and in the Esc menu under `INVITE INFO`. Everyone else runs the same build, picks
`JOIN GAME` and types it in. Nobody needs the command line.

- Over the internet: put both machines on one [Tailscale](https://tailscale.com) tailnet and join the host's tailnet IP.
- The flags still work, and skip the menu: `--host 7777`, `--join IP:7777`.

Verified on macOS (Apple Silicon). The Linux and Windows zips are built by CI and have not been
run on real hardware yet, so treat them as untested. If the game fails to start, send the
`hollow.log` file that sits next to the binary.

## Build and run

    cmake -B build -G Ninja
    cmake --build build
    ./build/bin/goonstein

The first configure fetches and builds SDL3 from source (a couple of minutes). Incremental
rebuilds are sub-second. Shaders are precompiled and committed, so a fresh clone needs no shader
toolchain; after editing anything in `shaders/` run `tools/shaders.sh` (needs
`brew install shaderc spirv-cross`, or `apt install glslc spirv-cross`).

There are CMake presets for every target -- `mac-release`, `linux-release`,
`windows-msvc-release`, `windows-mingw-release` and the matching debug ones:

    cmake --preset linux-release && cmake --build --preset linux-release -j8

Release presets build a relocatable layout (`bin/` plus `bin/assets/`); `--target package` makes a
tarball or zip. See **PLATFORMS.md** for per-OS dependencies, the shader pipeline, Steam Deck notes
and what is verified versus untested.

## Controls

| Action        | Keyboard / mouse          | Gamepad          |
|---------------|---------------------------|------------------|
| Move          | WASD                      | Left stick       |
| Camera        | Mouse                     | Right stick      |
| Jump          | Space                     | --               |
| Crouch        | Ctrl (hold)               | --               |
| Attack        | Left mouse (or J)         | RB               |
| Deflect / block | Right mouse tap / hold (or K) | LB           |
| Step dodge / sprint | Shift tap / hold          | B          |
| Lock-on       | Middle mouse, Q or Tab    | R3               |
| Interact / pick a mate up | E (hold to pick up) | A            |
| Fire / swing  | Left mouse                | RB               |
| Reload        | R                         | X                |
| Draw / holster the weapon | Q or scroll wheel | Y            |
| Put the weapon down | G                   |                  |
| Push to talk  | V (hold)                  | LB (hold)        |
| Skip cutscene | Enter                     | Start            |
| Debug overlay | F1                        | Back / Select    |
| World editor  | F2                        |                  |
| Character builder | F3                    |                  |
| Debugger      | F4                        |                  |
| Reload data   | F5                        |                  |
| Pause / step  | F6 / F9                   |                  |
| Art-style preview | F7 cycles plain / camcorder / flat / ink |          |
| Debug snapshot | F8                       |                  |
| Menu (pause)  | Esc                       |                  |
| Player list   | hold Tab                  |                  |

The layout follows Sekiro on PC. Level files also hot-reload on save while the game is running.
Starting the game with no flags opens the main menu; Esc during play opens the in-game menu
instead of quitting. A tool window (F2, F3, F4) still takes Esc for itself while it is open, and
its own function key closes it from either window. Every tool and debug key is a function key: no
game key is ever a punctuation mark you might type at a menu.

Three views, chosen per level. `view top` (the default) is the fixed camera from the level's
`camera` line. `view third` (the slice) plays behind the hero with mouse look. `view first` is
R.E.P.O.-style first person: the eye rides the hero's head, using the same mouse sensitivity as
the orbit camera and clamping pitch to the same limit up and down, and the body turns with the
view, so movement is relative to where you are looking. Your own model is not drawn in first
person (the camera is inside its head) but it is still drawn into the sun shadow map, so you see
your own shadow on the ground; remote players and NPCs draw normally. The field of view is 70
degrees rather than the orbit camera's 55, and `view first` takes an optional head-bob amount,
`view first 0.35` (1 is the default subtle bob, 0 turns it off; two vertical dips and one lateral
sway per stride, fading out with speed; `HOLLOW_BOB=N` overrides it for a capture). The mouse is
captured for `view third` and `view first` alike while the game window is played (it is released
for tool windows, pause, scenes and the bot). The same holds for the boss: `combat realtime` is
the third-person fight, `combat cards` the card battle.

### Movement

Half-Life's shape, at whatever rate your screen runs.

**Look.** Mouse look is applied on every rendered *frame*, not on the 60 Hz tick: the delta the
mouse reported this frame turns the view this frame, and the simulation reads whatever yaw the view
has when its tick comes round. Before this the view turned 60 times a second and the picture was
drawn interpolating between the last two of those turns, so at 144 Hz the pan rate stepped between
two values 30 times a second (measured: alternating 0.0122 and 0.0183 radians a tick under a steady
hand) and the whole view lagged a tick behind the mouse. There is no smoothing and no acceleration
anywhere in the path -- the pointer delta *is* the rotation. `mouse_sens` in `assets/settings.txt`
multiplies it (1.0 is the default 0.0022 radians per mouse pixel). The body turns with the view.

**Ground.** Acceleration and friction, Quake-shaped, in `assets/player.txt`: walk `speed 3.2`,
`sprint_mult 1.56` for a 5.0 m/s jog on Shift, `accel 10`, `friction 8`, `stop_speed 1.4`. In the
air `air_accel 10` chases a wish speed capped at 0.9 m/s, which is the classic small air control:
steer a little, never run on. Gravity is 20 m/s^2 and `jump_height 1.0` metres is what Space buys
(the impulse is derived from the height, so changing one number changes the jump). Ctrl crouches:
half speed, and the eye drops half a metre.

**Stairs and edges.** The feet are glued to the ground they are standing on through a step up *or*
down of up to 0.5 m, so walking off the edge of a deck is a step rather than the start of a fall,
and the old bounce along a deck edge (fall, catch, fall) cannot happen. What the feet do instantly
the eye does over about 80 ms -- the Quake step-smoothing trick -- so a stair reads as a stair and
not as a pop. Ground steeper than 50 degrees is a slide, not a floor. Landing from a fall dips the
eye about 0.1 m per metre-per-second of impact and springs back inside 0.4 s.

**The head.** The walk bob is one cycle per 2.2 m of ground covered, +-18 mm vertical and +-14 mm
lateral, advanced on the frame clock. It used to be +-35 mm at 3.8 Hz driven off the tick, which
measured as a vibration rather than a walk; the viewmodel's own bob was a 10 Hz jackhammer and now
shares the head's phase. `view first 0` in the level turns the bob off, `HOLLOW_BOB=N` overrides it.

## Combat

The real-time fight (`combat realtime` levels) is a Sekiro-shaped duel: posture, not health, is
what you are really fighting for. `src/combat.c` owns the state machines, `assets/player.txt` and
`assets/enemies/*.txt` own the numbers, `src/charmodel.c` turns the states into animation.

**Attacking.** Left mouse swings an A / B / C chain that loops. Each swing carries the character
forward (root motion) and the third is a slower, heavier finisher. A press is buffered for 0.2 s,
so asking for the next swing during recovery gets it the moment the window opens rather than
swallowing it. After the contact frame the swing can be cancelled straight into a deflect or a
dodge; another swing has to wait for the blade to finish travelling. Attacking while sprinting
comes out as a dash attack that closes the gap.

The hit does not land on a number out of `player.txt`: it lands on the frame the animation says the
blade does. Character files mark that frame (`anim attack CLIP contact 0.42`, a fraction of the
clip), the fight reads it back through `charmodel_clip_timing` and times the swing to it, so
swapping in a different hero with different clips keeps the hits honest. `attack_windup` in
`player.txt` is only the fallback when a clip has no contact mark.

**Guarding.** Tap right mouse and you deflect: a `parry_window` of 0.14 s judged against the boss's
own contact frame, using the sub-tick age of the press (`Input.parry_age`) so a press landing
between two ticks is judged where it actually happened. A deflect costs you almost no posture and
takes a large bite out of the boss's — three deflects break most of a Warden combo open. Hold right
mouse instead and you block: no health lost, but ~85% of the attack's damage goes into your posture
bar, and the guard breaks after four or five heavy hits. A broken guard staggers you for 1.5 s and
the boss cuts its recovery short to take the free hit. Posture regenerates when you are free or
holding guard, and pauses for `posture_delay` after anything lands. Unblockable moves (`parry no`,
the Warden's grab) have to be dodged.

**Getting hit.** The reaction is picked by damage size — a light flinch, a head snap, or a full
knockback that shoves you back `knockback` metres. Hitstop is 0.04 s on a light hit, 0.09 s on a
heavy one and 0.12 s on a deflect, which is what makes a deflect feel like it stopped something.

**Movement.** Locomotion is a speed-driven blend, not a switch: idle, walk, jog and sprint clips are
mixed by the character's actual ground speed and phase-synced so the feet stay in step, with the
cycle rate-matched to the ground so they do not skate. Everything crossfades in 0.06-0.15 s,
including the return from an attack to idle.

**The boss.** It circles the player between moves, walks in when out of range, steps in on its
lunges and chains combos out of recovery. Every move has a telegraph colour and a windup long
enough to read at 60 Hz; below half health the windups shorten. Breaking its posture staggers it and
triples your damage for the duration.

`--start fight --bot` runs a bot that deflects on the boss's contact frame, dodges unblockables and
punishes recovery; it is the regression test for all of the above.

## Everything is a text file

The game is meant to be edited without touching C:

| What                  | Where                        | Format doc                 |
|-----------------------|------------------------------|----------------------------|
| Level geometry, cameras, triggers | `assets/levels/*.txt` | `assets/levels/README.md`  |
| Carryable items        | `assets/items/*.txt`         | "Items and carrying" above |
| Cutscenes             | `assets/scenes/*.txt`        | `assets/scenes/README.md`  |
| Dialogue portraits    | `assets/portraits.txt`       | speaker name and image     |
| Boss move sets        | `assets/enemies/*.txt`       | comments in `warden.txt`   |
| Character bindings, voice, spawn weapon | `assets/characters/*.txt` | comments in `src/charmodel.h` |
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

## Items and carrying

Loot: a rigid body with a model, a display name, a mass, a price and a breaking point, described by
a text file under `assets/items/NAME.txt` and placed by a level's `item NAME x y z [yaw]` line. The
host owns every item and simulates all of them (a single-player game is its own host); a client
predicts only the one item in its own hands and pins every other item where the network
interpolation puts it, the same way it treats a remote player.

**Controls.** Look at something within 2.5 m and the HUD names it and offers "E   hold to grab"; E
(keyboard) or South / A (gamepad) grabs it. With something in hand, left mouse held charges a throw
for up to 0.8 s and throws on release; right mouse, or E again, drops it. Carrying works the same in
`view first` and `view third`.

**Carrying is a spring, not a socket.** A held item stays a real physics body, pulled toward a point
1.2 m in front of the carrier's eye by a spring whose stiffness falls as the item gets heavier, so a
crate lags and swings where a tape reel snaps into place. It keeps colliding with the world while
held, which is how it gets knocked around on a doorframe. If that spring stays stretched past 1.5 m
for more than 0.22 s -- a doorframe snag, not a swing -- the item is pulled out of your hands and
dropped. Two-handed items (`painting`, `crate`) cap the carrier at 60% movement speed and turn
sprint off.

**Fragile.** Every tick the host checks the hardest impact each item's body took against its
`fragile` figure (an m/s closing speed measured at the centre of mass); go past it and the item
breaks: a burst of debris and sparks, a smash sound (or the item's own `sound`), and the value is
gone -- the HUD flashes "-$VALUE" for a couple of seconds. A client never breaks an item itself; it
waits to be told by a snapshot, so both sides always agree on what the run is worth. `fragile 0`
(the default) means unbreakable.

**The boat.** A level's `hold` trigger volume is the boat's cargo hold: everything resting inside it,
not held and not already broken, counts toward the run. Standing within 18 m of the middle of that
volume shows "HOLD:  N items,  $V" on the HUD.

**The item file.** Comments start with `#`; blank lines and unknown keys are ignored (a warning is
logged for the latter). A file with no `model` fails to load and the item never appears.

| Key | Default | Meaning |
|-----|---------|---------|
| `model` | *(required)* | model or `.part` file, relative to `assets/` |
| `display` | the file's name | what the HUD calls it |
| `mass` | `5` (kg, clamped to >= 0.1) | drives the carry spring's stiffness and the throw speed |
| `half` | `0.25 0.25 0.25` | box half-size in metres; ignored once `radius` is set |
| `radius` | `0` | > 0: a sphere of this radius instead of a box |
| `fragile` | `0` | impact speed in m/s that breaks it; `0` = unbreakable |
| `value` | `0` | dollars it's worth sitting in the boat |
| `two_handed` | `no` (also takes `0`/`1`, `false`/`true`) | caps the carrier at 60% speed, disables sprint |
| `scale` | `1` (clamped to >= 0.01) | model scale |
| `tint` | `1 1 1` (white) | model tint, and the colour of its debris |
| `sound` | none | name of the `SoundId` played when it breaks; unset still plays the default smash |
| `weapon` | none | `melee` or `gun`; absent means ordinary loot, and every key below is ignored |
| `damage` | `0` | of a goon's 100-point wind pool; 100 in one go puts them straight on the floor |
| `rate` | `1` (clamped to >= 0.05) | shots or swings per second |
| `range` | `0` | metres a shot carries, capped at 60; a melee swing always reaches 1.6 m |
| `ammo` | `0` | rounds in a full gun |
| `knock` | `0` | metres per second of shove given to whatever is hit |
| `pellets` | `1` (clamped to 1..24) | hitscan rays per shot: one for a pistol, a handful for a shotgun |
| `fire_sound` | none | name of the `SoundId` played on firing (`shot`, `boom`, `whoosh`) |
| `grip` | `0 0 0 0 0 0` | `x y z yaw pitch roll` that turns the model's own rest pose into "grip at the origin, business end down +Z" -- what the viewmodel and the hand attachment both assume. A model authored that way needs no line at all; the Kenney blasters point down -Z and so carry `grip 0 0.019 0.025 180 0 0` |

`assets/items/vase.txt`, one of the eight starter items, as a worked example:

    # vase.txt -- decorative porcelain, no paperwork proving where it came from.
    model      models/shapes/cylinder.obj
    display    Vase, Provenance Unclear
    mass       6                           # kg
    half       0.225 0.225 0.225           # matches cylinder.obj at scale 0.45
    fragile    4.0                         # m/s; porcelain, very fragile
    value      320                         # dollars
    two_handed no
    scale      0.45
    tint       0.68 0.74 0.80              # pale glazed porcelain
    sound      smash

A level places one with `item NAME x y z [yaw]` -- `y` is the item's own centre, not its base, so an
item resting on a table needs `y` at table height plus its own half-height (see
`assets/levels/README.md`). `yaw` defaults to 0. A level holds up to `LEVEL_MAX_ITEMS` (128) items.

**Debugging.** `HOLLOW_ITEM_TRACE=1` logs one line per item, once a second: position, velocity, the
ground height under it, who (if anyone) is holding it, and its state (asleep, broken, in the hold).
`HOLLOW_BOT_TRACE=1` logs what the loot-fetching explore bot is doing. `HOLLOW_PHYS_TRACE=N` dumps
every contact, once a tick, of the physics body owned by item index `N`. `--test throw` is a
scripted headless check: a hundred ticks in it takes the first fragile item on the level, places it
five metres off the nearest tall wall and throws it in at 16 m/s, then logs whether it broke --
real evidence the fragile path still works, not just that the code compiled.

## Weapons

A weapon is an item with a `weapon melee|gun` line. Everything about carrying loot is unchanged;
what changes is which hand it goes into. E on a weapon puts it in the **weapon hand**, which has no
carry spring, no leash and no physics body -- it is a model on the end of an arm. The **loot hand**
keeps working exactly as it did, so a pistol in one hand and a painting in the other is legal, and
a very stupid way to travel.

The island starts with a bat and a wrench by the bunkhouse and a pistol and a shotgun down by the
boat, and each goon lands holding whatever its character file asks for.

**Spawn loadouts.** One line in `assets/characters/NAME.txt` names the weapon that character is
seated with:

    spawn shotgun

| Slot | Character | `spawn` | Lands with |
|---|---|---|---|
| 0 | `goon_a` (Dez)   | `shotgun` | six shells |
| 1 | `goon_b` (Marko) | `bat`     | a bat |
| 2 | `goon_c` (Pip)   | `pistol`  | twelve rounds |
| 3 | `goon_d` (Bunny) | `wrench`  | a pipe wrench |

The name is an item file under `assets/items`; an unknown one is a warning in the log and empty
hands. The host creates the item the moment a slot is seated -- solo start, host start, or a client
joining mid-run -- and puts it in the weapon hand through `weapons_equip`, the same call `E` makes,
with a full magazine. Everyone else therefore learns about it exactly as they learn about a mate
picking a shotgun up off the sand, and from that moment it is an ordinary item: put it down with
`G`, throw it, break it, lose it in the sea. Nobody is handed a second one -- there is one loadout
item per slot per level, and a goon who already has something in the weapon hand keeps it.

Because a loadout item appears *after* the level has loaded, its network id is fixed by the slot
(`ITEM_LOADOUT_ID` in `src/items.h`) instead of taken from the running counter: a client can only be
told about item ids it already has, so host and clients each create their own copy when a slot is
seated and the host alone decides whose hand it is in.

**Controls.** Left mouse fires or swings; hold it for a gun, tap it for a bat. `R` reloads (1.2 s;
an empty gun clicks at you first). `Q` or the scroll wheel draws and holsters, swapping the left
mouse between firing and charging a throw -- with a gun drawn you cannot throw the loot, which is
what `Q` is for. `G` puts the weapon down. Carrying something two-handed forces the weapon onto
your back and keeps it there until your hands are free. A holstered weapon is drawn on the spine;
a held one on `hand_r`, through the model's own pistol or sword hold clip.

**Melee.** A swing lasts 0.35 s and lands at 0.45 of the way through it, on the first thing inside
a 1.6 m arc: an item takes an impulse (and breaks if it is fragile enough to mind), a goon takes
`damage` off their wind and a shove. The swing is animated by code in the viewmodel and by the
`Sword_Attack` clip on everyone else's screen.

**Guns.** Hitscan, with a tracer, a muzzle flash light and a puff of smoke, a recoil kick and an
ammo counter. A shotgun fires its `pellets` on a fixed golden-angle fan rather than a random one,
so the host and every client draw the same spread. Hits give items an impulse, goons a knockdown,
and the scenery a small dust puff. There is no blood and no gore anywhere in the effect path; that
is a hard rule from `DESIGN.md`, not a preference.

**Knockdown, not death.** A goon has 100 points of *wind*, which regenerates 12 a second after two
and a half seconds of nobody hitting them. Empty it and they collapse: controls off, camera still
first person but lying on its side (rolled 70 degrees, eye at 0.30 m), a `DOWN` line on the HUD and
a countdown. They get up after six seconds, or straight away if a mate stands within 2.2 m and
holds E for 1.5 s -- the HUD offers `HOLD E   PICK UP <NAME>` and shows the progress. Friendly fire
is always on, because that is the joke. Nothing in the game is ever called death; the lying pose is
the held last frame of a clip whose name we do not repeat in the UI.

**Authority.** The host owns every shot. A client presses the button, plays its own kick, sound,
flash and tracer on that frame so the gun feels connected to the mouse, and sends a reliable
`NRM_WEAP_FIRE` carrying the eye and aim it fired from. The host checks that slot has that weapon,
has a round left and is off cooldown, snaps the origin to its own eye position if the client's is
more than 2.5 m out, re-runs the hitscan against its own copy of the world, and applies the result.
Knockdown, ammo and wind ride in the player snapshot as three extra bytes; tracers, flashes, thuds
and clicks go out as `NPT_EVENT`, unreliable, because they describe one frame and a lost one is
simply not seen. A shot a client predicted and the host refused costs one round for a tenth of a
second and is then put back by the next snapshot.

| Message | Direction | Payload |
|---------|-----------|---------|
| `NRM_WEAP_FIRE` (reliable) | client -> host | eye (3 x f32), aim (3 x i16, unit vector) |
| `NRM_WEAP_RELOAD` (reliable) | client -> host | nothing |
| `NRM_WEAP_SWAP` (reliable) | client -> host | nothing |
| `NRM_WEAP_REVIVE` (reliable) | client -> host | target slot, holding flag; resent every 6 ticks while held |
| `NRM_ITEM_GRAB` / `NRM_ITEM_RELEASE` | client -> host | unchanged: picking a weapon up and putting it down ride the item messages |
| `NPT_EVENT` (unreliable) | host -> clients | count, then per event: slot, kind, what it hit, pellets, from and to in centimetres |

Snapshots gain three bytes per player: a flag byte (down, getting up, drawn, weapon kind, reloading,
swinging), the round count and the wind. A weapon in a hand is drawn off its owner's `hand_r` on
every client, so its position on the wire says nothing and it is only replicated when it changes
hands -- which is cheaper than the loot it sits next to.

**Debugging.** `--test down` puts the local goon on the floor at tick 150, so the view from down
there can be captured on purpose instead of waited for. `HOLLOW_BOT=shoot` turns the explore bot into a weapon bot: it walks to the nearest
weapon, picks it up, and shoots the nearest other player or item every couple of seconds, which is
how the whole path is exercised headlessly. `HOLLOW_GRIP="x y z yaw pitch roll scale"` (and
`HOLLOW_GRIP_PISTOL`, `HOLLOW_GRIP_SHOTGUN`, ...) retunes how a weapon sits in the hand without a
rebuild.

## Testing and debugging

`assets/settings.txt` holds personal defaults (volume, debug overlay, hero, `mouse_sens`); flags override it.
`--quiet` sets volume to 0.15 and `--volume 0` mutes. F4 opens the debugger:
a side panel with the state summary and a live stream of raw inputs (every key, mouse and pad
press with position) interleaved with the actions the game took. F1 toggles the wireframe overlay: state
machines, timers, mouse position, card hover/drag/target, parry press and judgement offsets,
and a live event log. Every event also goes to `hollow.log` in the working directory. F8 writes
`hollow_snapshot.txt` with the full state plus recent events and copies it to the clipboard,
so a bug report is: press F8 when it happens, paste.

## Tools (second window, live in the game)

Three tools, one function key each: F2 the world editor (terrain, placing, look), F3 the character
builder, F4 the debugger. `Esc` closes the open tool (with nothing selected in it), and Esc quits
only when no tool is open. On a Mac, Cmd works in place of Ctrl. Also `Ctrl+D` wireframes,
`Ctrl+R` reload data, `Ctrl+G` snapshot. Command line: `--tool 2` (world editor) / `--tool 4` (builder).

Tool windows open tiled beside the game window and the game window goes back where it was when
the tool closes (`HOLLOW_NO_TILE=1` leaves placement alone). Closing a tool window ends the tool;
nothing ever draws into the game window. Panels flow to the window width and scroll with the wheel.
Keys typed into a tool window go only to that tool; keys typed into the game window go only to
the game. `--tool-shot PATH` saves a PNG of the tool window; `HOLLOW_TOOL_SIZE="w h"` fixes its
size for headless layout checks.

### World editor (F2)

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

**The architecture kit.** The compound's buildings started as stacked boxes, so their windows were
black rectangles painted on a wall and their columns were cylinders. `tools/cad/` is a parametric
CAD kit that dresses them: nineteen pieces -- windows with sills and shutters, doors, an arched
opening, columns, cornices, balustrades, pantile eaves, stairs, a pergola bay, the Music Room's
drum and dome -- each one a CadQuery function with named parameters, exported both as
`tools/cad/out/NAME.step` to open in your own CAD tool and as `assets/models/own/arch/NAME.obj`
for the game. `assets/models/own/arch/*.part` then lays them over the existing shells at each
building's own origin, cutting nothing: the black rectangles stay where they are and end up inside
the new frames' reveals. Rebuild the whole kit with
`tools/cad/.venv/bin/python tools/cad/build_kit.py`; `tools/cad/README.md` says how to edit a
piece and drop an OBJ back. Two things worth knowing when you place one: an OBJ prop is loaded
with a single white texture, so its own UVs are never sampled and `tex NAME TILE` world-projects
instead, and a piece whose section is constant along its run (a cornice, a coping) may be
stretched by the `.part`'s scale while one whose features repeat along it (a balustrade, a tile
course) may not.

**LOOK.** Every lighting, fog, sky, grading and pixel-look value on a slider.

### Character builder (F3)

Makes a character from a rigged model without drawing anything. BODY & PARTS lists every rigged
`.glb` under `assets/models/kaykit`, `characters` and `import` (KayKit knight, barbarian, mage,
rogues and four skeletons ship, all CC0) and every part of the chosen one as toggles. ATTACH puts
any file from `import`, `parts` or `own` on a bone: a helmet on `head`, a weapon on `handslot.r`,
a saved part on `chest`; nudge it with the sliders and it follows every animation. COLOURS lists
the model's paint colours, most used first; pick one and move the sliders to repaint it, shading
and edges follow. BORROW PARTS mixes the nine KayKit files: they share one skeleton, so a mage hat, a rogue hood, a
skeleton arm or a barbarian axe can be put on any body with no rigging (hide the part it replaces,
then borrow; `borrow FILE NODE` lines). ANIMATION picks a fighting style, AUTO BIND fills every game action from the
clip names, and any clip previews on the hero. SAVE writes `assets/characters/NAME.txt`
(`model`, `hide`, `borrow`, `recolor`, `attach`, `anim` lines, and the `spawn` and `voice` lines it
was loaded with); SAVE + USE AS HERO also sets it as the hero
in `assets/settings.txt`. `--hero NAME` plays it, overriding only the local player's slot character.

### Lighting

Cheap classic stack, no ray tracing: a sun shadow map (one depth pass from the sun, fitted around
what the camera looks at, hard-edged to match the toon bands; `shadow` look line 0..1), banded sun
plus hemisphere ambient, up to sixteen point lights without shadows, height fog, bloom, tonemap and
grading. Time of day: a `daytime` look line (hours, `-1` off) or the TIME OF DAY slider on the
LOOK tab moves the sun along the author's noon bearing and blends sky, ambient, fog colour and
stars through night, dawn, noon, golden hour and dusk (`src/daylight.c`). Everything runs at full
speed on the Steam Deck class of hardware. `assets/levels/showcase.txt` demonstrates it with the
Poly Haven scans: `--start level:showcase`, `HOLLOW_NOSHADOW=1` to compare.

### Frame rate

The simulation is a fixed 60 ticks per second (parry windows, card timings and animations are
counted in ticks, so play is identical everywhere). Rendering runs at the display's rate and draws
characters, items and the camera between the last two ticks, so 90, 120, 144 or 240 Hz screens show
motion every frame.

What is interpolated and what is not matters. Positions -- players, the boss, every item's body --
are lerped by the frame's alpha (the fraction of a tick left in the accumulator, always in [0, 1),
and monotonic even when one frame swallows two ticks). The first-person eye is *not*: it is rebuilt
every frame from the interpolated feet plus the current view angles, because the view has already
turned this frame and lerping the last two ticks' eye positions would drag it back a whole tick.
Cameras nobody is driving -- a cutscene, the fixed overworld view -- are still interpolated.
Measured walking a straight line at 144 Hz, the rendered eye's ground speed varies by 0.03% of its
mean across frames; what is left of the frame-to-frame *distance* variation is the frame pacing
itself, not the camera.

Parry presses are dated to the moment of the press, not to the tick that saw them, so the rhythm
judgement is exact at any frame rate. The debugger
(`\`) has a FRAME RATE row: display, 30, 60, 90, 120, 144, 240 and a vsync toggle, saved to
`assets/settings.txt` (`fps N`, `vsync 0|1`; `HOLLOW_FPS=N`, `HOLLOW_NOVSYNC=1` override).
`HOLLOW_NOINTERP=1` draws the raw tick state.

Three hooks measure this without a hand on the keyboard. `HOLLOW_TRACE=FILE` writes one CSV line
per rendered frame (time, alpha, eye, view angles, feet, speed, grounded) and dumps it at exit;
`HOLLOW_AUTOWALK=DEGREES` holds W with the view aimed along that compass yaw, and
`HOLLOW_AUTOINPUT=sprint|crouch|jump` holds those with it; `HOLLOW_AUTOLOOK=PIXELS_PER_SECOND`
injects a steady pan where a real mouse's motion arrives. A shake is a sign-alternating delta in
that file. `assets/levels/feel.txt` is the bench they run on: flat ground, four 0.25 m steps, a deck
with an edge to walk off and a 2 m ledge to fall from.

    HOLLOW_SILENT=1 HOLLOW_FPS=144 HOLLOW_NOVSYNC=1 HOLLOW_AUTOWALK=90 HOLLOW_TRACE=/tmp/walk.csv \
      ./build/bin/goonstein --level feel --first --no-scenes --start explore --volume 0 --frames 900

### Hot reload

While the game runs: the level file, its terrain, every loaded model or part file, and the hero's
character file and model reload within a second of being saved. Edit a character in a text editor,
export a new glTF over an old one, or save from the tools, and the world updates in place.

### Performance budget

Three target machines: an M4 Mac at 144 Hz and 1080p, an RTX 3060 at 1440p, and an
integrated-GPU laptop at 1080p60. Only the first one has ever run this code, so everything below
is a measurement on that machine and a set of levers for the other two.

#### Where the frame goes

`src/prof.c` is a CPU stopwatch per phase with a 4096-frame history. It shows on the F1 overlay as
a bar list and prints as a `prof:` block at exit, into `hollow.log` and stdout:

    prof: assets/levels/island.txt  frames 339  (median ms / p90 ms)
    prof:   frame            9.936  14.018
    prof:   input            0.027   0.039
    prof:   tick             0.047   0.061
    prof:   render           9.753  11.156
    prof:     cull           0.000   0.000
    prof:     shadow         4.356   4.983
    prof:     world          5.155   5.827
    prof:     water          0.002   0.002
    prof:     particles      0.093   0.097
    prof:     post           0.090   0.112
    prof:     ui             0.008   0.010
    prof:   present_wait     0.014   3.545
    prof:   submit           0.015   0.021
    prof:   counters draws 9160 instances 9160 tris 9141225 batches 0 props_drawn 1296 props_culled 814
    prof:   input->present 9.89 ms (median)

That is the island on 2026-09-11, before any of the work below. It says the whole thing: 9.9 ms a
frame, of which 9.75 is the CPU writing 9160 draw calls into a command buffer, 0.047 is the
simulation, and 0.014 is time spent waiting for the GPU. The island was not GPU bound and it was
not simulation bound. It was bound by the number of times the CPU said "draw".

It does **not** measure GPU time. SDL3's GPU API has no timestamp queries of any kind, so there is
nothing to ask. `present_wait` stands in: it is the time the CPU spends blocked waiting for an
image to draw into, so a large value means the GPU is behind and a small one means it is not.
`input->present` is the gap between the input poll returning and the frame being handed to the
driver -- a floor under the real input-to-photon latency, not the whole of it, since the display's
own pipeline is past where this can see.

#### Measuring anything at all: the two switches

`HOLLOW_NOVSYNC=1` asks for no vsync. It used to ask for `IMMEDIATE`, ignore what
`SDL_SetGPUSwapchainParameters` returned, and leave the caller believing an uncapped run when the
swapchain had quietly stayed on `VSYNC`. It now walks IMMEDIATE -> MAILBOX -> VSYNC, checks the
setter's return value, and logs the mode that actually won:

    present mode: immediate (asked for no vsync)

That is still not enough to measure a renderer on macOS. With IMMEDIATE, an **empty** level costs
5.7 ms a frame and 5.1 of them are inside `SDL_WaitAndAcquireGPUSwapchainTexture`. That is the
window server handing out drawables at its own pace, and it puts a floor of about 120 frames a
second under every number this project can produce. Below the floor you are timing the compositor:
removing the entire shadow pass -- 246 draw calls and 1.3 million triangles -- moved the measured
frame time by nothing at all.

So `HOLLOW_NOPRESENT=1` acquires no swapchain image and draws the frame into the off-screen targets
it was always drawn into, including a stand-in of the swapchain's own size and format so the final
upscale to the window's real pixel count is still paid for. The CPU is held two frames ahead of the
GPU with a fence, which is the backpressure the swapchain used to provide, so what comes out is the
renderer's throughput rather than how fast the CPU can fill a command buffer. Nothing appears on
screen while it is on.

    island, third person, 400 frames, HOLLOW_FIXED_DT
      IMMEDIATE      frame 8.24 ms   present_wait 5.66   <- the floor
      NOPRESENT      frame 6.02 ms   present_wait 3.47
    corridor, an almost empty level
      IMMEDIATE      frame 5.69 ms   present_wait 5.13   <- all floor
      NOPRESENT      frame 3.71 ms   present_wait 2.89

#### The benchmark

    ./build/bin/goonstein --bench bench.json --bench-shots /tmp/shots --volume 0

Four fixed paths over the island, 60 warm-up frames and 600 measured frames each, one tick per
rendered frame so two runs land on the same tick of the same frame:

| path | what it is |
|---|---|
| `pier` | walking the stone mole in first person, the real physics and the real first-person camera |
| `courtyard` | third person orbiting the Villa Ambergris colonnade |
| `summit` | third person looking across the island from the eastern high point -- the heavy one |
| `shootout` | four seated goons circling each other with a bat, a pistol, a wrench and a shotgun |

It writes `bench.json`: median and p90 frame time, every profiler phase, and the draw / instance /
triangle / batch counts per path. `--bench-shots DIR` drops a PNG of each path so a human can check
that `summit` is still looking at the island. A bench run sets `HOLLOW_NOPRESENT` for the reason
above, forces the island level, skips the menu and never writes to `settings.txt`.

`tools/bench_check.py BASELINE NEW [--tol 0.15]` compares a run against `bench_baseline.json` and
fails any path more than 15% slower. The baseline is keyed by **machine tag** --
`driver/platform/cores`, e.g. `metal/macOS/10` -- because 1.8 ms a frame is a fact about one M4 and
a lie about anything else. A tag with no entry means "nothing to compare against". The `bench` job
in `.github/workflows/ci.yml` runs this on `macos-latest` and uploads the JSON.

#### What it costs now

One binary, one Release build, one machine (M4, 1280x800 internal, `HOLLOW_NOPRESENT`, best of four
runs each). "Before" is the same binary at `--quality high` with
`HOLLOW_NOINSTANCE=1 HOLLOW_NOLOD=1 HOLLOW_NOMIP=1`, which undo instancing, the far LODs and
mipmapping and leave the shadow map and texture caps where this game always had them -- so this
compares one thing, not two builds of two commits. "After" is the shipping default, `quality normal`.

| path | before | after | | draws | | triangles | |
|---|---|---|---|---|---|---|---|
| `pier` | 4.522 ms | **1.912 ms** | -58% | 5703 | **551** | 7 025 905 | **2 706 696** |
| `courtyard` | 4.405 ms | **1.712 ms** | -61% | 3665 | **507** | 3 739 822 | **1 945 250** |
| `summit` | 6.173 ms | **2.072 ms** | -66% | 6323 | **537** | 10 052 497 | **3 484 922** |
| `shootout` | 4.437 ms | **1.894 ms** | -57% | 3940 | **622** | 4 091 213 | **2 200 094** |

Draw counts are both passes together, the sun's and the camera's.

#### Instancing

One draw per (mesh, texture, uv mapping, glow), not one per object. A palm is
`assets/models/own/palm_tall.part`, thirty boxes and cylinders with a tint each, and the island
stands a hundred and fifteen of them: 3450 draw calls for two meshes and two textures. Everything
that differed was a matrix and a green, so the matrix and the tint moved onto a per-instance vertex
stream (`shaders/world_inst.vert`) and the tint reaches the fragment shader through the vertex
colour, which is exactly where `lit.frag` already multiplied the material tint in. Same arithmetic,
same pixels.

The shadow pass is instanced too, with its own culled set. Both sets are built in one walk over the
level before either render pass opens, because an upload is a copy pass and a copy pass cannot run
inside a render pass. Instances arrive in level order and a draw needs them contiguous, so the
batch counts become offsets and each item is scattered into place: one pass to collect, one to
place, no sort.

Collecting cost more than the draws it replaced until the two things it did per prop per pass
stopped being done per prop per pass: a `.part`'s piece names were resolved by `strcmp` against
every loaded file, and its piece matrices were built from three angles in degrees -- 41 000 sines a
frame for the palms alone. Both are constant for the life of the file; both are cached.

A prop with a skinned mesh anywhere in it cannot be instanced (skinning wants a joint matrix palette
per draw) and is drawn whole, the old way, off a small fallback list. The island, the lantern level
and the glade have none.

#### Triangles: far LODs

`tools/make_lods.sh` decimates every island prop over 8000 triangles into a `<model>.lod.glb`
beside it, with headless Blender. The originals are untouched. A pachira is a background shrub and
it is 76 914 triangles; a cheiridopsis succulent, a thing the size of a dinner plate, is 82 438.
Two thirds of the frame's triangles were plants drawn at a size where you could not count their
leaves if you tried.

`props.c` uses the stand-in for **every shadow-map instance** -- a shadow is a silhouette, it does
not care -- and for anything under about ninety pixels across (radius over distance below 0.04, ten
times the threshold at which a prop is dropped entirely). A shrub a metre across keeps its real mesh
out to twenty-five metres. The LOD's textures load capped at 256.

    island, two fixed cameras, 400 frames
      wide view   11 919 004 tris  7.357 ms  ->  3 959 394 tris  2.994 ms
      close view   3 173 545 tris  2.335 ms  ->  1 414 741 tris  1.675 ms

Draw calls do not move: this is the GPU's half of the problem and instancing was the CPU's.

#### Mipmaps

Until 2026-09-11 every texture in this tree was one mip level, so a 1024-pixel photoscan on a shrub
forty metres away sampled one texel in twenty out of a texture the cache could not hold: the worst
case for bandwidth and the worst case for aliasing at once. The world sampler even asked for
`SAMPLERMIPMAPMODE_LINEAR` and got nothing, because `max_lod` defaults to 0.

Every texture now has a full chain, filled by `SDL_GenerateMipmapsForGPUTexture`, and the world
sampler has `max_lod` and 8x anisotropy. The nearest and clamp samplers -- pixel art, the UI, the
single-level render targets, the shadow map -- never ask above level zero, so the pixel look is
untouched. `lit.frag` biases the fetch by -0.5 levels, because without it the ground detail texture
averages to its own mean by the middle distance and the beach goes flat cream. The bias is in the
shader and not on the sampler because Metal's sampler has no LOD bias at all.

This is the one deliberate visible change in this work: far textures stop shimmering. On an M4 it
is worth 3-5% of the frame; on a laptop chip with no cache to hide behind it should be worth much
more, and that is the machine it is for.

#### Quality tiers

`assets/settings.txt`, key `quality`, one of `potato`, `normal`, `high`; `--quality NAME`
overrides. The tier shows on the F1 overlay and in the menu's corner.

| | render scale | shadow map | texture cap | HDR target | scatter | far plane |
|---|---|---|---|---|---|---|
| `potato` | 0.66 | 512 | 256 | `R11G11B10` | 50% | 70% |
| `normal` | 1.00 | 1024 | 1024 | `RGBA16F` | 100% | 100% |
| `high` | 1.00 | 2048 | 2048 | `RGBA16F` | 100% | 100% |

`src/quality.h` holds that table and nothing else in the tree names those numbers.

Scatter thinning drops half of the props that both have a far LOD and are under three metres
across -- the grass, the plants and the small rocks, not the buildings or the boat or the
pavilion -- chosen by a hash of the prop's index in the level, so the same ones are gone every
frame and on every run. The far plane comes in to 70% with the fog scaled to match (`fog_start`
down, density up by the same factor), so the total fog opacity reached at the far plane is
unchanged and potato draws less distance rather than the same distance behind thicker fog.

On the first run, with no `quality` line in `settings.txt`, the game guesses from what SDL will
actually tell it -- `SDL_GetGPUDeviceDriver`, the logical core count and the system RAM, since
SDL3's GPU API exposes no device name at all -- and then checks the guess: two seconds of real
play, and if the median frame is over 12 ms it drops a tier and writes the answer back to
`settings.txt`. It only ever drops.

    --bench, this M4, best of four runs, ms per frame
                pier   courtyard  summit  shootout
      potato   1.370     1.285    1.627    1.373
      normal   1.929     1.713    2.072    1.927
      high     1.947     2.039    2.164    2.736

**`normal` and `high` measure the same on an M4.** The only thing between them is the shadow map,
1024 against 2048, and on this chip that costs nothing while it does visibly soften the shadow of
every branch and leaf (11.95% of pixels differ between the two, 5.62% by more than 8 of 255, all of
it in the sun shadow's edges). So the default tier ships a softer shadow than this game had before
and buys nothing for it *here*. It is still the right ladder for a laptop chip, where a 2048x2048
depth pass is sixteen times the fill of a 512 one. If the softer default is not wanted, `quality
high` is one line in `settings.txt`.

The benchmark does not open a microphone: voice encodes Opus on the game thread at a bitrate that
varies with what the room sounds like, which showed up as the `shootout` path swinging between 2.1
and 3.7 ms from run to run while its draw counts stayed identical to the draw. Even so, an idle M4
still swings about 40% on two of the four paths at 1.8 ms a frame, so `bench_check.py` takes
several runs and uses the lowest median per path, and the CI job runs the bench twice. Noise only
ever adds time, so of N runs of identical work the fastest is the one closest to what the work
costs.

#### Netcode

The packet build and parse are O(players + items) with no allocation anywhere in `netgame.c` or
`net.c`. `assets/levels/netbench.txt` is the lantern level with the item list filled to 64 so that
can be sized: a host plus three bot clients on loopback, four players seated, 64 items.

    host tick     0.031 ms median   0.180 ms p90     (a tick's budget is 16.7 ms)
    host out      22 kB/s, 90 packets/s, ~243 B each
    corrections   0        hard snaps 0        dropped 0

64 items instead of 12 moved the host's outbound from 13 to 22 kB/s and the tick by nothing
measurable. See "Headless four-process test" for how to run it.

#### The rest of the frame

- **Light culling.** `lit.frag` loops over every light it is given for every fragment -- sixteen of
  them, each with a square root and no early out -- and the island declares seventeen spread over
  three hundred metres. A point light of radius R can only change a surface within R of it, so a
  light whose sphere is outside the view frustum is dropped. This is a cull, not a heuristic: the
  frame comes out byte-identical. It buys nothing measurable on an M4 and is for the machines that
  are short of fragment throughput.
- **No per-frame filesystem or environment calls.** `level_reload_if_changed` asked the disk
  whether the level file had changed once per *tick*; it is now once a second, like every other hot
  reload here. `game_render_at` asked SDL nine environment questions a frame, and SDL keeps the
  environment behind a mutex; they are read once now.
- **No per-frame allocation** anywhere in the tick or the render path. The terrain mesh and the sea
  are dirty-gated, the UI vertex upload is sized to the vertices actually written and batched by
  texture, and the instance stream is one buffer allocated at startup.
- **MSAA** is off and has never been on: no pipeline in `gfx.c` sets a sample count.
- **Bloom** already runs at quarter resolution and is skipped entirely when the level asks for
  `bloom 0`; the particle pass is skipped when there are no particles; the shadow pass is skipped
  when the sun is below the horizon.

#### Checking any of it yourself

Every claim above can be undone in the binary you have:

| | |
|---|---|
| `HOLLOW_NOINSTANCE=1` | one draw per prop piece, as it was before instancing |
| `HOLLOW_NOLOD=1` | the real mesh everywhere, never a far stand-in |
| `HOLLOW_NOMIP=1` | every world texture fetch pinned to level 0, no anisotropy |
| `HOLLOW_NOSHADOW=1` | no sun shadow pass |
| `HOLLOW_NOPART=1` | no particles |
| `HOLLOW_NOPRESENT=1` | no compositor in the loop (see above) |
| `HOLLOW_NOVSYNC=1` | ask for IMMEDIATE, then MAILBOX |
| `HOLLOW_FIXED_DT=1` | one tick per rendered frame, so two runs are comparable |

#### What has not been tested

Every number here is an Apple M4 with unified memory and a large cache. The two machines this work
is actually aimed at -- a discrete RTX 3060 and an integrated laptop GPU -- have not drawn a frame
of it. The changes most likely to matter there and least likely to show here are exactly the ones
with the smallest numbers above: mipmaps and anisotropy (texture bandwidth), light culling
(fragment ALU), the potato tier's `R11G11B10` HDR target (half the bandwidth on a target the world
pass writes and two later passes read), and the render scale. A real test needs someone to run
`--bench` on those machines and send back `bench.json`; the machine tag in it is what tells the
baseline which numbers are whose.

### Headless Blender (installed with Homebrew, never opened)

Two scripts in `tools/blender`, run from the repo root:

    blender -b --python tools/blender/variant.py -- assets/models/kaykit/Knight.glb assets/models/characters/knight_big.glb --height 1.15 --width 1.2 --head 1.3
    blender -b --python tools/blender/decimate.py -- scan.obj assets/models/import/scan.glb --target 20000

`variant.py` makes proportion variants of a KayKit character (taller, wider, bigger head, longer
arms or legs); weights and all 76 clips are kept, so the result is a new body with the full
animation set (`assets/characters/knight_big.txt` is one). `decimate.py` brings a heavy scan or CAD
export (OBJ, STL, FBX, glTF) down to a triangle budget, stands it on the ground and exports glTF.

### Making borrowed assets ours

Two levers, and neither is per-asset drawing. **World style** (LOOK tab, `style` look line): the
whole frame passes through our palette (`palette snap`), gets inked edges from the depth buffer
(`ink edges`), can be posterised (`colour levels`) and pixelated (`world pixel size` 2 to 4, so
scans and packs become pixel art like the characters). One slider changes every asset in the
game at once. **Per-asset recolour**: a `MODEL.recolor` file beside a model (`r g b  r2 g2 b2`
per line, 0..255) moves that paint colour with its shading kept, applied on load; tint, stretch and
grouping in the world editor cover the rest. Meshes come from CAD, packs or scans and are
reshaped with the Blender scripts. `assets/models/polyhaven/barrel_03/*.recolor` is an example.
**The palette itself** is `assets/palette.txt`, one hex colour per line (up to 64); both the
character pass and the world style pass snap to it, and it reloads live when saved. Change that
file and the whole game's colour identity changes.

On top of those levers sit three complete candidate art directions -- **camcorder**, **flat** and
**ink** -- each a handful of `look` lines in `assets/looks/`. A level picks one with a single
`look include camcorder` line and nothing else changes. `assets/looks/README.md` describes what
each one is, what it costs, and what it would cost on a weak GPU; the levels in the tree are all
still on the plain look.

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
simple frame-perfect bot play the fight and logs every swing, parry and hit. `--third` and
`--first` force a view whatever the level says, and in first person the bot wanders instead of
walking the path, so a `--shot-every` trail shows the world moving. This is how the fight is
checked without a controller in hand.

### Capturing a comparable frame

Three environment variables exist only so that two captures of the same shot can be put side by
side. `HOLLOW_FIXED_DT=1` advances exactly one tick per rendered frame and ignores the wall clock,
so a given `--frames N` always lands on the same moment of the simulation -- without it a heavier
render setting reaches a different point in a bot's walk and the two frames are not the same shot.
`HOLLOW_FIXED_FPS=N` is the finer version of it -- 1/N of a second per frame rather than a whole
tick -- which is what a strip of genuinely consecutive 144 Hz frames needs, since writing a PNG
takes a third of a second and would otherwise put a third of a second between them.
`HOLLOW_NOHUD=1` drops the HUD. `HOLLOW_CAM="PITCH DIST FOV YAW [free]"` overrides the level's
`camera` line and now also frames the third-person orbit; the trailing `free` leaves the yaw to the
game, which matters whenever something still has to walk somewhere, because movement is
camera-relative and a frozen yaw freezes which way forward is. `HOLLOW_LOOK=NAME` applies
`assets/looks/NAME.txt` over whatever the level authored (see `assets/looks/README.md`). Together:

    HOLLOW_SILENT=1 HOLLOW_NOHUD=1 HOLLOW_FIXED_DT=1 HOLLOW_LOOK=ink \
      HOLLOW_CAM="22 26 50 0" ./build/bin/goonstein --level island --volume 0 --no-scenes \
      --third --spawn 0 108 --frames 200 --screenshot summit_ink.png

`--menu-test host[:PORT]` and `--menu-test join:HOST:PORT` (or the `HOLLOW_MENU_TEST` environment
variable) drive the menu the way a player would -- move the highlight, take the row, type the
address -- so the headless check exercises the real path. The log prints `menu: playing (hosting)`,
`(joined)` or `(solo)` when the menu hands the game over, and `menu: no answer from ADDR` when a
join times out.

    ./build/bin/goonstein --volume 0 --frames 300 --screenshot menu.png
    ./build/bin/goonstein --volume 0 --menu-test host:7801 --frames 420
    ./build/bin/goonstein --volume 0 --menu-test join:127.0.0.1:7801 --frames 900

## The menu

Launching `./build/bin/goonstein` with no flags opens the main menu over the island: a slow
camera orbit over the pier behind the rows `PLAY SOLO`, `HOST GAME`, `JOIN GAME`, `NAME`, `QUIT`.

Navigation: arrow keys or `W`/`S`, the left stick or the d-pad, or the mouse (hovering highlights,
clicking takes). `Enter`, `E` or the pad's A takes a row; `Esc` or the pad's B goes back.

`NAME` is edited in place; it is written to `name` in `assets/settings.txt` and is the name other
players see in join messages and the player list. While a text field owns the keyboard nothing
else in the game sees a key, so typing a name cannot walk the player or open a tool window.

`HOST GAME` listens on UDP `port` from `assets/settings.txt` (7777 by default) and starts playing
straight away. The host's LAN address sits in the top-right corner of the screen so it can be read
out to a friend; when the machine has more than one address, `INVITE INFO` in the Esc menu lists
all of them.

`JOIN GAME` is one text field, pre-filled with the last address that worked (`last_join` in
`assets/settings.txt`). `HOST:PORT`, or a bare address which gets the default port. `Enter`
connects; the screen says `connecting...` for up to five seconds and, if the host does not answer,
drops back to the menu with the reason on one line (no answer, the address could not be reached,
or the host is full).

In game, `Esc` opens: `RESUME`, `INVITE INFO`, then `END HOST` (host), `LEAVE GAME` (client) or
`MAIN MENU` (solo), then `QUIT`. The world keeps running behind the menu on purpose: a host that
stopped answering for the length of a menu would drop its clients.

`INVITE INFO` shows the address to give out (or the address you joined) and who is connected.
Holding `Tab` during play shows the player list: name, round-trip time and the slot colour.

Any of `--host`, `--join`, `--level`, `--start` or `--bot` skips the menu entirely, so every
existing command line behaves exactly as before -- the command-line flags below are the other way
in, still there for scripted testing and CI.

New `assets/settings.txt` keys: `name`, `port`, `last_join`.

## Networking

Four players on one map. One of them hosts and plays in the same process (a listen server), and
the host's simulation is the truth: it owns every player, runs the fixed 60 Hz tick for all of
them, and tells the others what happened. There is no dedicated server and no matchmaking yet —
you type in an address.

    ./build/bin/goonstein --host 7777 --slots 4 --start level:lantern --third
    ./build/bin/goonstein --join 192.168.1.20:7777 --name ada --start level:lantern --third

| Flag | What it does |
|------|--------------|
| `--host PORT` | listen on UDP PORT and play in the same process |
| `--slots N` | how many players the host seats, 1..4 (default 4) |
| `--join HOST:PORT` | join a listen server; HOST may be a name or an IPv4 address |
| `--name NAME` | your name in the log and in join messages; also sends the log to `hollow_NAME.log` |
| `--log FILE` | write this process's log somewhere else (so four processes do not clobber one file) |
| `--no-scenes` | never fire a trigger's cutscene; implied by `--host` and `--join` |
| `--third` | force the third-person camera whatever the level says |
| `--first` | force the first-person camera whatever the level says |
| `HOLLOW_NET_LOSS=0.2` | throw away 20% of received packets, for testing |

If both `--third` and `--first` are given, `--first` wins.

Each net slot loads its own character file: slot 0 plays `assets/characters/goon_a.txt`, slot 1
`goon_b.txt`, slot 2 `goon_c.txt`, slot 3 `goon_d.txt` — the four Goon Squad members — so four
processes on one map show four different people without anyone picking anything. A slot whose file
is missing falls back to `hero.txt` and is told apart by its net colour instead (that per-slot
tint is only applied to the fallback: the goons carry their own textures and tinting them again
would muddy them). `--hero NAME` (or the `hero` line in `assets/settings.txt`) still overrides the
character for whichever slot this process drives; the other three always show the goon their slot
owns.

### How it fits together

`src/net.c` is the transport and knows nothing about the game: one non-blocking UDP socket, peers
with sequence numbers, a 32-bit ack history, round-trip estimation and a small ordered reliable
channel. `src/net_sys.h` hides the difference between BSD sockets and Winsock, so the same code
builds on macOS, Linux and Windows. `src/netgame.c` is the glue and knows nothing about sockets:
slots, joining, snapshots, interpolation, prediction and the per-second log line. The game calls
into it at exactly four places (`netgame_pre_tick`, `netgame_post_tick`, `netgame_local_input`,
`netgame_view_pos`), so with no `--host` or `--join` the single-player paths are untouched.

**A host tick.** Drain the socket: seat anyone who sent a join, queue each client's intent, drop
anyone silent for five seconds. Tick the host's own player through the normal game code. Then
simulate every remote player by feeding its newest intent through the same `player_update` the
local player uses. Every other tick (30 Hz), send each client a snapshot.

**A client tick.** Drain the socket, apply the newest snapshot, tick its own player normally, send
this tick's intent. It only ever simulates itself.

**Remote players are interpolated.** Snapshots go into a per-slot ring with the time they arrived,
and remote players are drawn at `now - 100 ms`, between the two snapshots that bracket that
moment. That is the price of smooth motion over a jittery link: everyone else is 100 ms in the
past. The engine's existing between-tick interpolation still runs on top, so 120 Hz screens see
motion every frame.

**The local player is predicted.** The client runs the same movement code on its own input
immediately rather than waiting for the host. Every input is stamped with a tick and the position
it produced is remembered for 128 ticks. A snapshot says "I applied your input from tick T, and
this is where you ended up"; the client compares that with what it predicted for tick T. The
difference is added to the current position — the inputs since T were pure integration, so the
error carries forward unchanged — and the same amount is subtracted from a visual offset that
decays back to zero over about 70 ms, so the correction is applied to the simulation immediately
and to the picture gradually. An error over 2 m is a real desync rather than jitter, so it snaps
and says so in the log.

The move direction is **quantised before the client uses it**, to a signed byte per axis. The
client predicts with the same numbers the host will replay, so the two do not drift apart just
from rounding.

### Packet layout

Everything is little-endian and written through a bounds-checked byte cursor, so the wire format
does not depend on struct padding. Every packet starts with the same 16-byte header:

| Bytes | Field | |
|---|---|---|
| 2 | magic | `0x484E` |
| 1 | proto | protocol version |
| 1 | ptype | 0 none, 1 input, 2 snapshot |
| 2 | seq | this packet's sequence number |
| 2 | ack | newest sequence seen from the other side |
| 4 | ack_bits | which of the 32 before that also arrived |
| 2 | rel_ack | reliable messages received in order up to here |
| 1 | nrel | reliable blocks that follow |
| 1 | pad | |

Then `nrel` reliable blocks (`u16 id, u16 len, body`) and then the payload. Reliable messages —
join, accept, reject, player joined, player left, goodbye — ride along on the ordinary traffic and
are resent in every packet until the other side acks them, so joining works over a lossy link
without a separate retry timer.

**Input** (client to host, every tick, 10 bytes): client tick `u32`, move x and z as `i8` each
(world space, /127), look yaw `i16` (radians × 32767/π), buttons `u16`.

**Snapshot** (host to client, 30 Hz): server tick `u32`, the client's newest applied input tick
`u32`, player count `u8`, the boat's hold totals (`u16` count, `u32` value), item count `u8`, then
one record per player and one per item. Each record starts with a type and an id rather than being
a fixed struct, which is how item entities ride along in the same packet as players. A player
record is 22 bytes: position 3 × `f32`, yaw `i16`, anim `u8`, anim time `u16` (ms), hp `u16`,
player state `u8`.

Four players is a 113-byte snapshot. See the measured bandwidth below.

### Items over the network

An item record is 14 bytes: type `u8`, network id `u16`, position 3 × `i16` in centimetres (so
±327 m, a good deal more island than there is), orientation compressed to one signed byte per
quaternion component, and a flag byte (bit 0 broken, bits 1-3 the holder's net slot plus one so 0
means nobody, bit 4 resting in the hold). Anything held, changed, or actually moving -- more than
2 cm or three and a half degrees since the last snapshot it went out in -- is sent every snapshot.
Being awake is deliberately not enough on its own: a pile of crates settling against each other
stays awake for a while without visibly moving, and at 30 Hz that pile alone was three times the
whole bandwidth budget. Everything else is swept in a round robin sized so every still item lands in
a snapshot at least twice a second, which is also how a client that joined mid-run gets the whole
level's furniture inside half a second instead of waiting for each piece to move.

Measured on four localhost processes with 64 items on the island: **6.8 kB/s down per client** in
the steady state (8.1 kB/s averaged over a run that includes the opening scramble as 64 objects
fall and settle at once), against 15 kB/s before the two passes and the smaller record. With the
island's eight real items it is 4.3 kB/s.

Grabbing, throwing and dropping are not part of the snapshot: they travel as reliable client-to-host
messages, resent until acked like the join/leave messages above, and the host validates each one by
distance from the item before acting on it -- the same reach check a host-side grab gets. Debris,
the pieces that fly off a broken item, is never sent at all; each side that has learned an item
broke (the host by simulating it, a client by the broken flag arriving) plays its own burst locally.

### Testing on a LAN

The addresses can now be read off the host's own screen -- `HOST GAME`, then the corner readout
or `INVITE INFO` -- instead of running `ipconfig getifaddr en0`; the flags below are the other way
in, for scripted or headless testing.

On the host machine, find its address (`ipconfig getifaddr en0` on macOS, `ip addr` on Linux),
then:

    ./build/bin/goonstein --host 7777 --start level:lantern --third

Each other machine runs the same build and level:

    ./build/bin/goonstein --join HOST_ADDRESS:7777 --name bo --start level:lantern --third

Swap `--third` for `--first` on any of these to try the first-person view instead.

Open UDP 7777 on the host's firewall. Everyone must start on the same level; the host's level name
travels in the accept message and a mismatch is logged as a warning. Joining late is fine, and
quitting is fine — the others see the slot disappear. A client that stops sending for five seconds
is dropped.

There is **no NAT traversal**: over the open internet this needs port forwarding. Lobbies,
invites and relay come with Steam in M7.

### Headless four-process test

`--frames N` plus `--shot-every N DIR` makes each process exit on its own and leave a trail of
screenshots, so the whole thing runs without a human:

    ./build/bin/goonstein --volume 0 --host 7777 --start level:lantern --third \
        --frames 1800 --shot-every 300 /tmp/net/H &
    sleep 8
    for i in 1 2 3; do
      ./build/bin/goonstein --volume 0 --join 127.0.0.1:7777 --bot --name c$i \
          --start level:lantern --first --frames 1500 --shot-every 300 /tmp/net/C$i &
    done
    wait

The host stays `--third` so its screenshots show all four bodies; the clients run `--first` so the
trail also covers that view. `--bot` on a client wanders — a slow arc with a new random heading
every second or two, turning back when it strays too far from the spawn — so the screenshots show
four humanoids moving around each other. In first person the bot steers the view toward its
heading and walks straight ahead, so the trail of screenshots shows the world swinging past rather
than a fixed view strafing sideways. Each process writes its own log (`hollow_host.log`,
`hollow_c1.log`, ...) with a line per second:

    net client slot 2 peers 1 | in 30 pkt 3390 B | out 60 pkt 1560 B | rtt 16.8 ms |
      snap age 20 ms | corr 4 avg 0.021 max 0.084 m snaps 0 | dropped 0

plus a `net pos` line with every seated player's position, stamped with the wall clock, which is
what the test uses to check that the four processes agree about where everyone is
(`HOLLOW_NET_TRACE=1` logs that line at 5 Hz instead of once a second). Note that `rtt` is ack
turnaround, not wire latency: the other side only answers on its next send, so on loopback it
reads as most of one snapshot interval rather than nothing.

Add `HOLLOW_NET_LOSS=0.2` to every process to throw away one packet in five on receive.

### What it costs, measured

One host and three bot clients on loopback, the lantern level, 60 Hz, 45 seconds:

| | up | down |
|---|---|---|
| each client | 1.6 kB/s (60 packets/s, 26 B each) | 3.4 kB/s (30 packets/s, ~113 B each) |
| the host, three clients seated | 5.7 kB/s | 2.7 kB/s |

So a full four-player game costs the host about 6 kB/s out and 3 kB/s in — nothing, and it stays
flat as players join because the snapshot is one packet per client per 33 ms whatever is in it.

Reconciliation over the same run: 100 to 370 corrections in 26 seconds, mean 0.07 to 0.21 m each,
one or two hard snaps per client and those are the moment of joining, before the host's spawn has
arrived. Comparing each client's log with the host's, a client's picture of another player sits
0.2 m from the host's truth on average; measured against where the host says that player was
100 ms earlier — which is what the client is trying to draw — the median difference is 0.03 m. Its
own predicted position matches the host's to 0.02 m.

With `HOLLOW_NET_LOSS=0.2` on all four processes (about 1500 packets thrown away), nothing
changes: everyone still joins, the numbers above are the same to within noise, and all four
processes exit cleanly.

## Voice

Proximity voice chat. Everyone talks over the same UDP link the game already uses, everyone hears
everyone else positioned in the world, and every goon has a different voice.

### Keys and settings

| | Keyboard / mouse | Gamepad |
|---|---|---|
| Push to talk | **V** (hold) | **LB** (hold) |

A red dot and the word `talking` appear bottom-left while your microphone is live. Name tags float
over the other three goons; whoever is speaking gets a brighter tag and a small speaker icon whose
arcs grow with how loud they are in *your* mix, so the icon is also a distance readout.

`assets/settings.txt`, or the matching command-line flags, which win:

| Setting | Flag | Default | What it does |
|---|---|---|---|
| `voice ptt\|open\|off` | `--voice MODE` | `ptt` | `ptt` transmits while V / LB is held. `open` is an energy gate: it opens above an RMS of 0.010 and stays open for 350 ms after you stop, so the ends of words survive. `off` never transmits **and never opens a recording device**, so the OS microphone indicator stays dark. Receiving is always on -- `voice_volume 0` is how you mute other people. |
| `voice_volume V` | `--voice-volume V` | `1.0` | Voice level, 0 to 2. Completely independent of the game's `volume`: the voice bus is added *after* the master gain, so `--volume 0` still talks and still listens. |
| `voice_monitor 0\|1` | `--voice-monitor` | `0` | Hear your own changed voice locally, at 0.7 gain and dead centre. The only way to audition a character's `voice` line without a second machine. |

### A voice per character

Each character file carries one line (see `src/charmodel.h`):

    voice PITCH FORMANT EFFECT

`PITCH` multiplies the fundamental (clamped to 0.5..2.0), `FORMANT` moves the vocal tract
(0.7..1.5, bigger = brighter/smaller body) and `EFFECT` is `none`, `radio` or `ring`. Missing line
means `1.0 1.0 none`. The Goon Squad:

| | Line | Reads as |
|---|---|---|
| `goon_a` | `voice 0.85 1.00 none` | big and slow |
| `goon_b` | `voice 1.00 1.00 radio` | everything he says sounds like a walkie-talkie |
| `goon_c` | `voice 1.35 1.15 none` | small and high |
| `goon_d` | `voice 1.00 0.90 ring` | faintly robotic |

The line is re-read once a second, so editing a character file re-voices you without a restart.

**It is applied at the sender, before Opus.** That is the whole design decision: the host and all
three listeners hear exactly the same voice, the shifter runs once per speaker instead of once per
listener per speaker, and the codec never sees the unprocessed microphone.

**How the shifter works** (`src/voice_dsp.c`, no external library):

* *Pitch* is SOLA plus resampling. Grains of 960 samples with 50% overlap are laid down at an
  output hop of 480 and an input hop of `480 * pitch`, each grain's read position refined by a
  normalised cross-correlation search of +/-128 samples so the overlap lands on a matching phase.
  That time-scales by `1/pitch` without touching the spectrum; resampling the result by `pitch`
  then multiplies every frequency by `pitch` and puts the duration back. 960 samples in, 960 out,
  constant latency.
* *Formant* is a grain-resample, and it is an approximation, deliberately. Hann windows of 512
  samples are overlap-added at a fixed hop of 256, but each window is filled by reading the
  pitch-shifted signal at step `formant`, which scales every frequency inside the grain while the
  unchanged hop keeps the duration. It is not an LPC or cepstral envelope warp -- over the +/-15%
  the presets use it reads as a change of body size and costs one multiply-add per sample, which
  is the trade we wanted. Skipped entirely when `formant` is 1.0.
* *Effects* are last: `radio` is a 300 Hz Butterworth high-pass into a 3 kHz low-pass, a `tanh`
  soft clip and a little hiss scaled by the frame's own level (so it only hisses while you talk);
  `ring` is a 55 Hz sine ring-modulator at 60% depth with phase continuous across frames.

### On the wire

48 kHz mono, cut into 20 ms frames, Opus at 24 kbps constrained VBR with in-band FEC on and the
expected loss set to 20%. One frame is one block:

    u8  slot     who is talking (a client sends its own; the host overwrites it)
    u8  flags    bit 0: the speaker is dead
    u16 seq      per-speaker frame counter, wraps
    u16 t_ms     wall-clock milliseconds at capture, low 16 bits -- the latency readout
    u8  len      Opus bytes that follow
    u8  data[len]

Seven bytes of header on ~61 bytes of Opus.

**Client to host, the block rides on the input packet the client already sends every tick.** The
host reads the intent from the first 10 payload bytes and hands whatever follows to `voice.c`, so
talking costs a client its own bytes and not a second datagram. **Host to client it is an
`NPT_VOICE` packet, forwarded the instant it arrives**, with one byte rewritten. The host never
decodes anything it forwards.

Receiving is a jitter buffer and an Opus decoder per speaker (`src/voice_jitter.c`): 80 ms of
target depth, u16 sequence arithmetic so wrapping is a non-event, duplicates and late packets
counted and dropped. A gap whose *next* packet has arrived is recovered from that packet's in-band
FEC; a gap with nothing after it is concealed by Opus PLC for at most three frames before the
buffer admits the speaker has stopped.

### How it is heard

The mixer pulls the voice bus once per audio block, after the game mix and after the master
volume. Per speaker, per block:

* **Distance.** Smoothstep between 4 m (full) and 25 m (silent).
* **Occlusion.** `level_ray_solid` from your camera to their mouth: a solid block in the way costs
  a one-pole low-pass at 700 Hz and 30% of the level. That is the whole "he is behind the wall" cue.
* **Pan.** Their direction against the camera's right vector, equal-power, capped at 0.85 so a
  voice at your shoulder still has a body.
* **Dead players** are heard by everyone within range at half volume (`VOICE_DEAD_GAIN`). Nothing
  sets a co-op player's hp to zero yet; the hook is in and reads `flags` bit 0 and the local `hp`.

Left/right gains are ramped linearly across the block rather than stepped, which is what stops a
player sprinting past you from clicking.

### Testing without microphones

    HOLLOW_VOICE_WAV=FILE     read FILE instead of opening a recording device
    HOLLOW_VOICE_DUMP=FILE    write the local voice bus to FILE (stereo), and one
                              FILE.slotN.wav per speaker (mono, after distance/occlusion/pan)

`assets/audio/voice_test.wav` is 7.8 s of `say -o` converted with
`afconvert -f WAVE -d LEI16@48000 -c 1`. With `HOLLOW_VOICE_WAV` set, no recording device is opened
at all, so a headless run never trips the macOS microphone prompt -- and no *playback* device is
opened either (see below), so a test is silent in both directions.

    tools/voice_test.sh near        four processes, host at the spawn, ~2 m from the talker
    tools/voice_test.sh far         same, host parked 20 m away
    tools/voice_test.sh loss        near, with HOLLOW_NET_LOSS=0.2 on all four processes

and the dumps are measured with

    tools/voice_check.py pitch FILE...        fundamental (per-frame autocorrelation) and centroid
    tools/voice_check.py distance DIR...      the attenuation table: logged gain vs measured level
    tools/voice_check.py track LOG WAV        per-second gain against per-second level
    tools/voice_check.py level FILE           RMS envelope in dBFS
    tools/voice_check.py gaps  FILE           longest silent run: the loss-continuity check

Every process logs a `voice:` line once a second with the mode, bytes up and down, and per speaker
the distance, the applied gain, whether they are muffled, the decode/FEC/conceal/late counts and
both latencies (`transit` is capture to arrival, `lat` is capture to entering the playback ring).

### What it costs, measured

One host and three clients on loopback, macOS, 35-second runs. c1 is the only one talking; it
stands still so the distance is fixed, and `--hero goon_a` pins its voice so runs are comparable.
Everything below comes out of the `voice:` log lines and the dump WAVs, measured with
`tools/voice_check.py`.

**Bandwidth.** A talking client sends **2.5 to 3.1 kB/s**, mean 2.9. That is the whole marginal
cost of talking: 50 frames a second of ~51 bytes of Opus plus the 7-byte block header, riding on
the input packet the client was already sending. Giving voice its own datagrams would have added
another ~2.1 kB/s of UDP and IP headers on top. The host forwarding one speaker to the other two
clients spends 6.3 to 8.0 kB/s, which is that 2.9 doubled plus a 16-byte packet header each.

**Latency, capture to the moment the audio enters the playback ring**, measured with a wall-clock
millisecond stamp in every packet (all four processes are on one machine, so the clocks agree):

| | |
|---|---|
| capture to arrival at the host (`transit`) | 2 to 15 ms |
| capture to playback (`lat`) | **80 to 130 ms**, typically 110 |

against a budget of 20 ms to fill a frame, ~8 ms of waiting for the next tick, 23 to 28 ms of
shifter delay depending on the preset, ~10 ms of transit and 80 ms of jitter buffer. Well inside
the 150 ms target. A scheduling hiccup shows up as a `sync` in the log and one second where the
average is a few hundred milliseconds before it recovers.

**Proximity.** The same sentence, the same voice, three fixed distances, measured out of the
per-speaker dumps (`tools/voice_check.py distance`), levels taken as the 90th percentile of the
100 ms envelope so the loud vowels are compared and not the noise floor:

| run | distance | gain in the log | measured | the curve says | error |
|---|---|---|---|---|---|
| near | 4.7 m | 0.974 | 0 dB (reference) | 0 dB | -- |
| mid | 13.7 m | 0.557 | -4.65 dB | -4.86 dB | **0.22 dB** |
| far | 22.7 m | 0.039 | -28.27 dB | -28.03 dB | **0.24 dB** |

So the curve in the log is the curve in the mixer, to a quarter of a decibel over a 28 dB range.

Every one of those runs was made with `--volume 0`, which is the point: the voice bus is added
after the master gain, so muting the game does not mute your friends. `--voice-volume 0.5` on an
otherwise identical run measures 5.47 dB down on the same voice at the same distance, against a
theoretical 6.02 -- the rest is 20 cm of distance difference between the two runs.

**Occlusion.** `tools/voice_test.sh wall` on the corridor level puts solid geometry on the line
between the camera and the talker. The log reports `muffled` and a gain of exactly 0.70, and the
spectral centroid of the dump falls from **1003 Hz to 620 Hz** -- the 700 Hz one-pole doing its job.

**Packet loss.** `HOLLOW_NET_LOSS=0.2` on all four processes, so one packet in five is thrown away
on receive. Per second, out of 50 frames sent: 35 to 44 arrive, 35 to 45 decode normally, 4 to 12
are recovered from the next packet's in-band FEC, 1 to 5 are concealed, **none** are dropped as
late and **none** come out as silence. The longest silence in the dump is 1.46 s, which is the
test WAV's own loop seam -- the clean run's is 1.72 s, so 20% loss does not lengthen it at all.
Latency is unchanged at 119 to 126 ms.

**A voice per goon**, measured on the host's dump after the full round trip (shifter, Opus, UDP,
the host's forward, the jitter buffer, the decoder and the proximity mix), F0 by per-frame
autocorrelation over the voiced frames:

| | preset | F0 | vs the source | centroid |
|---|---|---|---|---|
| the recording | -- | 141.6 Hz | -- | 1416 Hz |
| `goon_a` | 0.85 1.00 none | 123.4 Hz | x0.87 | 1003 Hz |
| `goon_b` | 1.00 1.00 radio | 146.8 Hz | x1.04 | 1909 Hz |
| `goon_c` | 1.35 1.15 none | 196.7 Hz | x1.39 | 1717 Hz |
| `goon_d` | 1.00 0.90 ring | 68.3 Hz | x0.48 | 1039 Hz |

`goon_a` and `goon_c` land within 2% of their asked-for pitch. `goon_b` leaves the pitch alone and
moves the timbre instead, which is what the band-pass and the soft clip are for. `goon_d` measures
an octave and a bit down because a 55 Hz ring modulator genuinely makes the signal periodic at a
lower rate -- that buzz is the effect, not a measurement error.

### Nothing may be audible in a test

`HOLLOW_SILENT=1` opens no audio playback device at all -- not for the game, not for voice -- and
the two voice test hooks imply it, so `tools/voice_test.sh` cannot make a sound on the machine it
runs on however it is invoked. The voice bus is then rendered on the main thread at wall-clock
speed instead of by the mixer callback, so `HOLLOW_VOICE_DUMP` still receives exactly the mix you
would have heard and the jitter buffers are still clocked by it. This is **not** the same as
`--volume 0`, which mutes the game and deliberately leaves voice chat audible. If you regenerate
the test recording, `say` must be given `-o FILE`; it must never be allowed to speak aloud.


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
      net.*         UDP transport: peers, sequence numbers, acks, reliable channel
      net_sys.h     the one header that knows Winsock from BSD sockets
      netgame.*     four-player listen server: slots, snapshots, interpolation, prediction
    shaders/        Vulkan GLSL source, compiled by tools/shaders.sh
    assets/         all content (see above); assets/shaders holds compiled shaders
    ASSETS.md       licence manifest (KayKit CC0 props, Ninja Adventure CC0 sprites, music and sounds, stb, cgltf)

## Platform notes

One tree builds for macOS (Metal), Linux and Steam Deck (Vulkan) and Windows (D3D12). The GLSL in
`shaders/` compiles to SPIR-V, MSL and HLSL anywhere via `tools/shaders.sh`; DXIL has to be signed
by Microsoft's `dxil.dll` and so is produced on Windows by `tools/shaders.ps1` or by the CI job
that uploads it. `gfx.c` picks whichever compiled format the GPU device accepts and finds on disk,
and logs the choice at startup.

**PLATFORMS.md** has the full story: per-OS build instructions, the shader pipeline diagram, the
portable/packaged asset layout, Steam Deck specifics, the crossplay rules (same tick rate, same
little-endian wire format, host-authoritative so determinism is not required), the `net_sys.h`
socket shim, and an explicit list of what is verified on the dev machine versus what still needs
real Windows and Deck hardware.
