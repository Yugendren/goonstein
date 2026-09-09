# Goonstein Island (working title; alt: "Rescue Something")

The Goon Squad: four idiots on a motor boat go to rescue what they think are kids from a billionaire's island.
The name is a double pun: the meme word "goon" and the historical goon squads, hired muscle of the 19th and
20th centuries (strikebreakers, union enforcers). They are enthusiastic, unqualified and paid by nobody.
There are no kids. There is a conspiracy hole, and the deeper they go the dumber it gets.
Co-op physics comedy for four, played with friends for a few evenings. Fun and games, nothing serious.

Forked from `hollow` on 2026-09-10 (see ARCHIVE.md for what the engine already does).

## Pillars

1. **Physics comedy over voice.** The game is what happens between four people carrying fragile things
   through a place that wants them to fail. Proximity voice makes distance the drama.
2. **A conspiracy hole, not a crime scene.** The island is a meme museum. Every floor down is a new layer
   of internet lore. It is never about the real crimes.
3. **Loot with meaning.** Evidence is the loot. Between runs you present it at a press conference and the
   world shrugs. The shrug is the running joke.
4. **Drawn by hand.** The whole game looks like it was doodled: ink outlines, flat fills, paper, wobble.
   Characters and items are the user's own sketches. Roblox / Peak / Webfishing-style bodies.

## Hard rules (these decide whether the game can exist)

- **No children in the game.** Not as characters, props, text or implication. The boys expect kids;
  they find memes. The victims of the real case are absent by design, never referenced, never a joke.
- **No living real people.** Archetypes only: The Prince, The Financier, The Scientist, The Guy Who Only
  Flew There Once. No real names, no likenesses, no client list.
- **Only old history is real.** Anything presented as factual is JFK-era or earlier (Roswell, MKUltra,
  the grassy knoll, Operation Northwoods, Bohemian Grove, Tesla's death ray). Everything set in the
  current era is invented. The island is a play on a public satellite outline, with a made-up name.
- **The island name and every location name are fictional.**

## The loop (one run, about 15 minutes)

1. **Boat in at dusk.** One motor boat, the shared extraction point. Land on the dock.
2. **Clear the compound and side areas.** Evidence is loot: paintings, tapes, pages of the book, the
   bust, the temple door. Physics carry, fragile, loud. Enemies are island staff and lore creatures.
3. **Go deeper.** Each run can unlock the next level under the island. Deeper is stranger:
   temple -> tunnels -> submarine pen -> the lab -> the lizard lounge -> ... (open list, see LORE.md).
4. **Boat out before dawn** or lose the evidence.
5. **Press conference.** Present the evidence. Headlines shrug. Buy gear. Go again.

Optional long-term punchline (undecided): the bottom floor is a filing cabinet.

## Milestones

| # | Block | Done when |
|---|-------|-----------|
| M1 | Netcode | Host + 3 clients move humanoids on one map on localhost; bots as clients; snapshots + interpolation; headless verified |
| M2 | Grab and carry | Pick up, carry two-handed, throw, drop; fragile objects break; boat with a cargo hold |
| M3 | The island | Terrain from the public outline, dock, main house, temple, pool, helipad, side areas; dusk to dawn clock |
| M4 | Run loop | Land, loot, extract, press conference, gear shop; first underground level |
| M5 | Voice | Proximity voice chat |
| M6 | Look | Ink renderer, drawn characters and items from the user's sketches |
| M7 | Steam | Lobbies, invites, friends |

## What we keep from hollow

Renderer, lighting and time of day, terrain generator, world editor, character builder, parts / CAD
OBJ import, humanoid rig and clips, third-person camera, bot harness, hot reload, frame-rate options.
The card battle, the Lantern Count slice, dialogue scenes and the Sekiro fight stay in the tree but
are not part of this game.
