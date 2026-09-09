# hollow

Working title. An HD-2D JRPG: pixel-art sprites living in a lit low-poly 3D world, an isometric
overworld, in-engine cutscenes, and card battles where the enemy's attack frames are rhythm beats
you deflect osu-style. C11 on SDL3, targeting macOS, Linux (Steam Deck) and Windows.

This is the skeleton build: one corridor, one cutscene going in, one boss, one cutscene coming out.

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
| Attack        | Left mouse (or J)         | RB               |
| Deflect / block | Right mouse tap / hold (or K) | LB           |
| Step dodge / sprint | Shift tap / hold (or Space) | B          |
| Lock-on       | Middle mouse, Q or Tab    | R3               |
| Interact      | E                         | A                |
| Skip cutscene | Enter                     | Start            |
| Debug overlay | F1                        | Back / Select    |
| Pause / step  | F2 / F3                   |                  |
| Reload data   | F5                        |                  |
| Quit          | Esc                       |                  |

The layout follows Sekiro on PC. Level files also hot-reload on save while the game is running.

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

## Tools (second window, live in the game)

Three tools, one key each: `[` the world editor (terrain, placing, look), `]` the character
builder, `\` the debugger. `Esc` closes the open tool (with nothing selected in it), and Esc quits
only when no tool is open. On a Mac, Cmd works in place of Ctrl. Also `Ctrl+D` wireframes,
`Ctrl+R` reload data, `Ctrl+G` snapshot. Command line: `--tool 2` (world editor) / `--tool 4` (builder).

Tool windows open tiled beside the game window and the game window goes back where it was when
the tool closes (`HOLLOW_NO_TILE=1` leaves placement alone). Closing a tool window ends the tool;
nothing ever draws into the game window. Panels flow to the window width and scroll with the wheel.
Keys typed into a tool window go only to that tool; keys typed into the game window go only to
the game. `--tool-shot PATH` saves a PNG of the tool window; `HOLLOW_TOOL_SIZE="w h"` fixes its
size for headless layout checks.

### World editor (`[`)

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

**LOOK.** Every lighting, fog, sky, grading and pixel-look value on a slider.

### Character builder (`]`)

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
(`model`, `hide`, `borrow`, `recolor`, `attach`, `anim` lines); SAVE + USE AS HERO also sets it as the hero
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
counted in ticks, so play is identical everywhere). Rendering runs at the display's rate with
vsync and draws characters and the camera interpolated between the last two ticks, so 90, 120,
144 or 240 Hz screens show motion every frame. Parry presses are dated to the moment of the press,
not to the tick that saw them, so the rhythm judgement is exact at any frame rate. The debugger
(`\`) has a FRAME RATE row: display, 30, 60, 90, 120, 144, 240 and a vsync toggle, saved to
`assets/settings.txt` (`fps N`, `vsync 0|1`; `HOLLOW_FPS=N`, `HOLLOW_NOVSYNC=1` override).
`HOLLOW_NOINTERP=1` draws the raw tick state.

### Hot reload

While the game runs: the level file, its terrain, every loaded model or part file, and the hero's
character file and model reload within a second of being saved. Edit a character in a text editor,
export a new glTF over an old one, or save from the tools, and the world updates in place.

### Performance budget

Props are culled per pass against the camera and the sun frustum, and skipped when smaller than a
few pixels; bounding spheres are cached per file. A generated world with 1779 pieces draws about
30 pieces at the play camera and about 200 at a wide editor view, at 4 to 7 ms a frame on an M4
with shadows on. Textures cap at 512 for props. The debugger shows frame time, draw calls and the
drawn/culled counts; `HOLLOW_NOVSYNC=1` and the `perf:` line at exit measure headlessly.

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
`u32`, entity count `u8`, then one record per entity. Each record starts with a type and an id
rather than being a fixed struct, so carried objects and thrown props can be added to the same
packet later without a new message. A player record is 22 bytes: position 3 × `f32`, yaw `i16`,
anim `u8`, anim time `u16` (ms), hp `u16`, player state `u8`.

Four players is a 113-byte snapshot. See the measured bandwidth below.

### Testing on a LAN

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
