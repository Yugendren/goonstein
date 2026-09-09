# Cutscene format

One command per line, `#` starts a comment. The first field is the time in seconds.

    T cam   ex ey ez  tx ty tz  fov  [cut]     # camera keyframe; eased from the previous key unless cut
    T say   "text"  dur  [speaker] [emote E]   # no speaker: narration subtitle. With a speaker: a
                                               # dialogue box with that character's portrait. E is an
                                               # emotion: happy laugh smug wink sad cry angry furious
                                               # surprise alert question think love heartbreak sleep
                                               # nervous neutral shout dead annoyed
    T actor NAME move x y z dur                # walk to a point
    T actor NAME face x y z                    # turn toward a point
    T actor NAME anim NAME                     # idle walk attack parry kneel dead roar hurt
    T actor NAME teleport x y z yaw
    T fade  from to dur                        # 0 = black, 1 = visible
    T letterbox on|off
    T shake amount dur
    T sound NAME                               # footstep swing hit parry hurt stagger roar death blip heart door sting
    T daytime HOUR [dur]                       # time of day moves to HOUR over dur seconds (6.5 dawn, 13 noon, 18.5 golden, 20.5 dusk, 1 night)
    T music FILE|stop                          # a file under assets/sprites/ninja/Audio/Musics, cross-faded
    T end

Actors: player, boss, any NPC name from the level's `npc` lines, and `prop:NAME` -- a level prop
tagged `name NAME` on its `prop` line. A prop takes `move`, `teleport` and `face` (not `anim`); its
move is eased in and out, because a boat does not start and stop like a lift. The prop's collider
travels with it and anyone standing on that collider is carried along, so four men can ride a boat
in and step off it.

An actor's y in `move` and `teleport` is a floor, not the final answer: characters are put on
whatever ground is under them, so `move x 0 z dur` walks along the terrain, while a larger y (a
quay, a deck) lifts them onto it. Speakers named after an NPC get
that NPC's live portrait; "Hero" and "The Warden" are mapped in assets/portraits.txt.
