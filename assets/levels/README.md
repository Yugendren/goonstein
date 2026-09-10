# Level format

One command per line, `#` starts a comment. Units are metres, Y is up, angles in degrees.
Yaw 0 faces +Z, positive yaw turns toward -X when viewed from above? No: positive yaw rotates
+Z toward +X. The camera starts behind the player looking along the spawn yaw.

## Placement

    spawn    x y z yaw                         # player start
    boss     x y z yaw                         # boss start
    arena    minx miny minz maxx maxy maxz     # fight bounds (legacy, camera no longer clamps to it)
    block    x y z  sx sy sz  tex  r g b  tile  [solid|pass]
             # box: centre, size, texture, tint, repeats per metre. The texture name is one of the
             # seven procedural ones (stone tile wood metal flesh plaster flat) or the stem of any
             # image in assets/textures/ -- drop foo.jpg in there and `foo` is a texture name here.
             # tex "flat" is untextured: the tint is the colour. The trailing word is optional and
             # defaults to solid; write "pass" for a block characters walk through.
    prop     FILE x y z yaw scale [tint r g b] [glow r g b] [stretch sx sy sz] [collide R [H] [deck]]
             #                                 [tex NAME [TILE]] [name NAME]
             # FILE is relative to assets/, e.g. models/kaykit/hex/tree_single_A.gltf
             # glow adds emissive colour (bloom picks it up). collide R adds an invisible solid
             # box 2R across at the prop's base, H metres tall (default 5, tall enough that the
             # camera cannot peek over a wall). `deck` makes it a floor instead of a wall: the top
             # face at y+H is walkable and the box never blocks movement -- piers, boat decks,
             # anything you are meant to stand on. Leaving `collide` off takes whatever the .part
             # itself declares (a `collide` line inside it, scaled by this prop's scale), which is
             # how a hundred palms get trunks without a hundred collider lines here. A collider
             # narrower than 1.2 m across is ignored by the camera, so it does not bounce off trunks.
             # tex NAME paints the whole prop with one world texture (any name a `block` takes)
             # instead of the model's own, projected in world space at TILE repeats per metre
             # (default 1) so scale and stretch never smear it. A .part's piece may carry its own
             # `tex`, which wins over the prop's.
    collider x y z sx sy sz                    # invisible solid box
    light    x y z  r g b  radius intensity [flicker F]   # point light; F ~0.3 for torches
    emitter  TYPE x y z  ex ey ez  rate  r g b  size life
             # TYPE: spark ember firefly mist spore leaf smoke. ex ey ez are half-extents of the
             # spawn box around x y z. rate = particles per second. Additive types (firefly,
             # ember, spore, spark) glow: colours above 1.0 bloom.
    item     NAME x y z [yaw]                  # places assets/items/NAME.txt at that point
             # NAME is the base name of a file under assets/items/ (see that folder and README.md,
             # "Items and carrying"). y is the item's own centre, not its base -- an item sitting on
             # a table needs y at table height plus its own half-height. yaw defaults to 0. A level
             # holds up to LEVEL_MAX_ITEMS (128) items.
    trigger  name  minx miny minz maxx maxy maxz  [once]
             # a trigger named exactly "hold" is never a scene cue -- level_trigger_at skips it by
             # name -- and is instead the boat's cargo hold: every item resting inside it, not held
             # and not broken, counts toward the run. See README.md, "Items and carrying". The
             # island's own hold (in assets/levels/island.txt) sits with its floor at the boat's
             # hull rather than level with the deck: an item resting on the deck is only a few
             # centimetres above that floor, and a box that starts exactly at deck height would
             # count it in and out again every time the physics settles it another millimetre.
    scene    intro|boss|victory  FILE          # cutscene file under assets/scenes/ for that beat
    terrain  FILE                              # heightmap base name relative to assets/, e.g. levels/glade_terrain
             # (files FILE_h.png, FILE_c.png, FILE.txt)

## Standing on things

Characters resolve their footing once a tick: the ground is the terrain (or y = 0 where the level
has none), or the top of any solid block or `deck` collider whose xz contains them and whose top is
within 0.5 m of their feet. Walking onto something that high steps up onto it; walking off the edge
drops with gravity. There is no jump, so anything more than 0.5 m tall is a wall, not a step.

Inside a cutscene the y in `actor NAME move x y z dur` is a *floor*, not the final answer: the
actor is still lifted onto whatever ground is under them (write 0 to hug the terrain) but never
sinks below what the scene asked for, which is how an actor climbs out of a boat onto a quay.

## Look

    sun      dx dy dz  intensity  r g b        # direction the light travels
    ambient  sr sg sb  gr gg gb                # sky (from above) and ground (from below) ambient
    fogv     r g b  density  base  falloff  scatter  start
             # density per metre (0.02 light, 0.08 thick); base = height where fog is full;
             # falloff per metre above base; scatter = glow toward the sun; start = clear metres
    sky      zr zg zb  hr hg hb  gr gg gb  sun_glow  stars  fog_blend
             # zenith, horizon, below-horizon colours; stars 0..3; fog_blend 0..1 haze at the horizon
    toon     softness  shadow_floor  rim_power  # band softness 0.02 hard..0.3 soft; floor = darkest lit
    grade    exposure  saturation  contrast  bloom  bloom_threshold
    lift     r g b                             # added to shadows (cool blue lifts read as night)
    gain     r g b                             # multiplied into highlights
    shadow   strength                          # sun shadow map strength 0..1 (0 = off)
    look far METRES                            # camera far plane (default 80). Nothing further from the eye is drawn,
                                               # so a level bigger than a walled village needs its own value (the
                                               # island uses 600). The sun shadow map is fitted to the visible range
                                               # and fades out at its own edge, so raising this costs draws, not shadows.
                                               # Keep `fogv` thick enough to hide whatever the far plane now reveals.
    look res   scale [nearest|linear]          # internal render resolution, 1 = full (1280x800), 0.25..1 = upscaled from
                                               # fewer shaded pixels; the word picks the upscale filter (default linear)
    look texcap pixels                         # cap every texture's longest edge at this many pixels (0 = engine default);
                                               # VRAM and bandwidth, not a look on its own
    look flat  amount                          # 0..1 blend every material toward its own texture's mean colour, so a
                                               # photoscan and a hand-made box read as the same kind of surface
    look grain amount                          # 0..1 film grain; small reads, 0.02 is already visible
    look vignette amount                       # 0..2 corner darkening (default 0.45)
    look chroma pixels                         # 0..4 chromatic aberration at the frame's corners (cheap-lens look)
    look dither amount                         # 0..0.2 ordered 4x4 dither added before the `style` colour crunch;
                                               # roughly 1/LEVELS hides banding from a low `style ... LEVELS ...`
    look ink   width [wobble] [luma]           # ink line width in pixels (1 = the old one-pixel depth ink); wobble is
                                               # how far the sample point wanders in pixels, stepped at 8 Hz so lines
                                               # "boil"; luma 0..1 adds a screen-luminance edge for interior creases
                                               # (a folded arm, a window in a wall) that the depth edge cannot see.
                                               # Trailing values keep their defaults (1 0 0).
    look hatch amount                          # 0..1 screen-space crossed hatching where the frame is already dark
    look paper amount                          # 0..1 paper texture (assets/textures/paper.png) multiplied over the frame
    look include file                          # pulls in assets/looks/FILE.txt (FILE or FILE.txt, either works) and
                                               # applies its lines as if they were written here -- everything before the
                                               # include is overridden by it, everything after wins over it. An included
                                               # file may only contain look lines (the ones on this page), plus comments
                                               # and blank lines. See assets/looks/README.md for the three candidate looks.
    combat   cards|realtime                    # the boss fight: card battle with rhythm parries (default) or the third-person action fight
    view     top|third                         # overworld view: the fixed camera line (default) or behind the hero with mouse look (third person)
    camera   pitch dist fov [yaw]              # overworld camera: 36 14 32 -35 isometric (default), 25 9 50 closer over the shoulder
    style    snap outline levels pixel         # world style layer: snap the whole frame to the palette 0..1, ink depth edges 0..1,
                                               # colour levels (0 = palette instead), world pixel size (1 = off, 2..4 = pixel-art world).
                                               # The ink test is a RELATIVE depth step (dz/z), not an absolute one, so one
                                               # outline setting works at 8 m and at 200 m; past about 250 m the depth buffer
                                               # has no precision left and nothing is inked, which is what stops the horizon
                                               # coming out as a black bar. `look ink` sets its width, wobble and luma edge.
    pixel    scale [levels] [outline] [palette] [inner] # 3D characters as pixel art: scale = screen pixels per art pixel (0 off),
                                               # levels = colours per channel when palette is 0, palette 1 = snap to Endesga 32,
                                               # outline 0..1 = dark silhouette line, inner 0..1 = crease lines.
                                               # Takes 1 to 5 numbers; trailing ones keep their default (3 8 1 0 0.6),
                                               # so "pixel 0" turns the pass off.
    daytime  hour                              # 0..24 clock: the sun, ambient, sky, stars, lift and fog colour
                                               # are computed for that hour (6.5 dawn, 13 noon, 18.5 golden, 1 night)
                                               # and override the lines above; the sun line's direction is kept as the
                                               # sun's bearing at noon, and fog density/shape stay as authored.
                                               # Leave the line out (or use a negative hour) to keep the values above.

Legacy `fog r g b near far` and `light dx dy dz ambient r g b` lines still parse but are ignored
by the renderer once `fogv` / `sun` are present. `cam` lines are ignored (camera is free).

    scene    NAME FILE
    npc      NAME CHARACTER x y z yaw SCENE [radius]
