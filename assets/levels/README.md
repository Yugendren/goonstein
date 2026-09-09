# Level format

One command per line, `#` starts a comment. Units are metres, Y is up, angles in degrees.
Yaw 0 faces +Z, positive yaw turns toward -X when viewed from above? No: positive yaw rotates
+Z toward +X. The camera starts behind the player looking along the spawn yaw.

## Placement

    spawn    x y z yaw                         # player start
    boss     x y z yaw                         # boss start
    arena    minx miny minz maxx maxy maxz     # fight bounds (legacy, camera no longer clamps to it)
    block    x y z  sx sy sz  tex  r g b  tile  [solid|pass]
             # box: centre, size, texture (stone tile wood metal flesh plaster flat), tint, repeats per metre
             # tex "flat" is untextured: the tint is the colour. Blocks are solid unless "pass".
    prop     FILE x y z yaw scale [tint r g b] [glow r g b] [collide R]
             # FILE is relative to assets/, e.g. models/kaykit/hex/tree_single_A.gltf
             # glow adds emissive colour (bloom picks it up). collide R adds an invisible solid
             # cylinder of radius R (3 m tall) at the prop's base.
    collider x y z sx sy sz                    # invisible solid box
    light    x y z  r g b  radius intensity [flicker F]   # point light; F ~0.3 for torches
    emitter  TYPE x y z  ex ey ez  rate  r g b  size life
             # TYPE: spark ember firefly mist spore leaf smoke. ex ey ez are half-extents of the
             # spawn box around x y z. rate = particles per second. Additive types (firefly,
             # ember, spore, spark) glow: colours above 1.0 bloom.
    trigger  name  minx miny minz maxx maxy maxz  [once]
    scene    intro|boss|victory  FILE          # cutscene file under assets/scenes/ for that beat
    terrain  FILE                              # heightmap base name relative to assets/, e.g. levels/glade_terrain
             # (files FILE_h.png, FILE_c.png, FILE.txt)

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
    pixel    scale levels outline palette inner # 3D characters as pixel art: scale = screen pixels per art pixel (0 off),
                                               # levels = colours per channel when palette is 0, palette 1 = snap to Endesga 32,
                                               # outline 0..1 = dark silhouette line, inner 0..1 = crease lines
    daytime  hour                              # 0..24 clock: the sun, ambient, sky, stars, lift and fog colour
                                               # are computed for that hour (6.5 dawn, 13 noon, 18.5 golden, 1 night)
                                               # and override the lines above; the sun line's direction is kept as the
                                               # sun's bearing at noon, and fog density/shape stay as authored.
                                               # Leave the line out (or use a negative hour) to keep the values above.

Legacy `fog r g b near far` and `light dx dy dz ambient r g b` lines still parse but are ignored
by the renderer once `fogv` / `sun` are present. `cam` lines are ignored (camera is free).
