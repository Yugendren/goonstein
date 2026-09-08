# Level format

One command per line, `#` starts a comment. Units are metres, Y is up. Angles in degrees.

    fog      r g b near far
    light    dx dy dz ambient r g b          # direction the light travels
    spawn    x y z yaw                       # player start
    boss     x y z yaw                       # boss start
    arena    minx miny minz maxx maxy maxz   # fight bounds for the follow camera
    block    x y z  sx sy sz  tex  r g b  tile  [solid|pass]   # centre, size, texture, tint, repeats per metre
    cam      name  minx miny minz maxx maxy maxz  eyex eyey eyez  tx ty tz  fov
    trigger  name  minx miny minz maxx maxy maxz  [once]

Textures: stone tile wood metal flesh plaster. Blocks default to solid.
Cameras are matched in file order; the first volume containing the player wins.
