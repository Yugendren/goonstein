// --bench: four scripted camera paths through the island (pier, courtyard, summit, shootout), run
// headless at a fixed tick and timed on the real clock, then dumped as bench.json for
// tools/bench_check.py to compare against a per-machine baseline (bench_baseline.json). See that
// file's "_comment" for why the baseline is keyed by machine rather than kept as one global number:
// a frame time from an M4 says nothing about a CI runner, or about somebody else's laptop.
//
// Everything here lives behind four functions the main loop calls in a fixed order -- see main.c
// for exactly where. bench.c owns no rendering or gameplay code of its own; it only writes into the
// Input the tick loop was going to read anyway and pins the camera the same way a cutscene does
// (camera_set_scene), so a path exercises the ordinary game rather than a special path through it.
#pragma once
#include <stdbool.h>
#include <stddef.h>

struct Game;
struct Platform;
struct Input;

// Scans argv for --bench [FILE] and --bench-shots DIR. Returns true when --bench was given.
// *out_json is "bench.json" when no FILE followed --bench; *out_shot_dir is NULL when
// --bench-shots was not given at all. Neither pointer is ever heap allocated: both point into argv
// or at a string literal, so there is nothing for the caller to free.
bool bench_requested(int argc, char **argv, const char **out_json, const char **out_shot_dir);

// Once the game and its GPU state exist: remembers where bench_finish should write the JSON and
// (if not NULL) the screenshot directory, and resets the internal path/frame counters to the start.
void bench_init(struct Game *g, struct Platform *pf, const char *json_path, const char *shot_dir);

// Call once per frame, right after platform_poll and BEFORE game_view_look/the tick loop: this is
// what lets a path's look_x reach camera_look and its move_x/move_y reach the tick that is about to
// run. Starts the next path's one-time teleport the first time it is called for that path (which is
// also when it calls prof_reset -- see bench.c's top comment for why that alone is the path's
// warm-up) and writes this frame's synthetic input into *in. Returns false once all four paths have
// finished; the caller should stop the loop when it does.
bool bench_pre_tick(struct Game *g, struct Platform *pf, struct Input *in);

// Call after the tick loop, before game_render: pins the camera for the paths that need one
// (everything but pier, which exercises the real first-person camera). If this frame is the
// current path's screenshot frame, writes "SHOT_DIR/<path>.png" into shot_path_out, else writes an
// empty string; the caller should game_screenshot() that path once the frame has actually rendered
// (gfx_screenshot reads back the frame that was just submitted, so this has to happen after
// game_render, not here).
void bench_pre_render(struct Game *g, struct Platform *pf, char *shot_path_out, size_t shot_path_cap);

// Writes bench.json from the four paths' recorded stats. Call once, after the loop has stopped.
void bench_finish(struct Game *g, struct Platform *pf);
