# The Lantern Count — vertical slice

Eight minutes: a peaceful mountain village at dawn, a crisis at the shrine, the Warden in the
ruined arena, a death cutscene. Everything is text under `assets/`; the game starts here
(`level lantern` in `assets/settings.txt`).

## Beats

| # | Beat | Where | Files |
|---|------|-------|-------|
| 1 | Dawn intro: narration, camera sweep over the village | spawn 0 0 -4 | `scenes/lantern_intro.txt` (trigger `intro`) |
| 2 | Entering the plaza: the Elder notices the stranger | z 21..23 | `scenes/lantern_town.txt` (trigger `town`) |
| 3 | Talk to the Elder, the Smith, the apprentice (E) | -4 30 / 6 28 / -6 37 | `characters/elder.txt smith.txt child.txt`, `scenes/lantern_elder/smith/child.txt` |
| 4 | Crisis at the shrine: the count begins, dusk falls at midday, music turns | 0 0 46 | `scenes/lantern_crisis.txt` (trigger `crisis`) |
| 5 | The arena gate: the Warden reveal | z 57..59 | `scenes/lantern_boss.txt` (trigger `boss_door`) |
| 6 | Card and parry battle | arena 0 0 72 | `enemies/warden_battle.txt`, `characters/warden.txt` |
| 7 | Death cutscene: the Warden kneels, night falls, fade | arena | `scenes/lantern_victory.txt` |

Level: `assets/levels/lantern.txt` (+ `lantern_terrain_*`), village parts in `assets/models/own/`
(`house_a/b/c`, `well`, `market_stall`, `lantern_post`, `shrine_lantern`).

## Verify (headless, muted)

    HOLLOW_FPS=60 ./build/bin/hollow --volume 0 --bot --frames 9000 --shot-every 300 /tmp/slice

The bot walks the road, talks to all three villagers, triggers every scene, wins the fight and
reaches the death cutscene in about 65 seconds of sim time; `hollow.log` lists each beat. Captures
of single beats: `--start scene:lantern_crisis.txt --spawn 0 42 --frames 900 --screenshot out.png`.

## Pipeline checklist this slice exercised

- terrain generator + hand shaping, road, pads, biome paint, water
- parts from shapes and scans (houses, well, stall, shrine), tint, stretch, grouping
- characters from borrowed KayKit parts, recolours, attachments; live portraits
- scene scripts with camera, dialogue, emotes, time of day, music; NPC talk triggers
- sun shadows, time of day, style layer, palette file
- battle with sub-tick parry timing; bot regression run
