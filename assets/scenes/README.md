# Cutscene format

One command per line, `#` starts a comment. The first field is the time in seconds.

    T cam   ex ey ez  tx ty tz  fov  [cut]     # camera keyframe; eased from the previous key unless cut
    T say   "text"  dur  [speaker]             # subtitle
    T actor NAME move x y z dur                # walk to a point
    T actor NAME face x y z                    # turn toward a point
    T actor NAME anim NAME                     # idle walk attack parry kneel dead roar hurt
    T actor NAME teleport x y z yaw
    T fade  from to dur                        # 0 = black, 1 = visible
    T letterbox on|off
    T shake amount dur
    T sound NAME                               # footstep swing hit parry hurt stagger roar death blip heart door sting
    T end

Actors: player, boss.
