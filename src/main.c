// hollow - entry point.
// Fixed-timestep simulation, variable-rate rendering, SDL3 GPU backend.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "game.h"
#include "prof.h"
#include "debug.h"
#include "quality.h"  // quality potato|normal|high: settings.txt key, --quality flag, the startup guess/probe
#include "camera.h"   // camera_set_mouse_sens: settings.txt owns the sensitivity
#include "audio.h"
#include "voice.h"   // --- voice ---
#include "bench.h"   // --bench: four scripted camera paths, timed and dumped as JSON

#define TICK_HZ 60
#define TICK_DT (1.0 / TICK_HZ)
#define MAX_FRAME_DT 0.25  // clamp after a stall so we don't spiral
#define MAX_TICKS_PER_FRAME 5   // 83 ms of simulation is the most one frame will catch up on

// The `stall:` block. `rows` is one frame each, warm-up frames dropped; `csv` (HOLLOW_STALL) is
// optional and gets a row per frame. p99 within twice the median, and nothing over 12 ms outside a
// level load, is the bar this is measured against.
static int stall_cmp(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return (x > y) - (x < y); }
static void stall_report(float (*rows)[PROF_COUNT], int n, int warmup, const char *csv) {
    if (n <= warmup + 8) return;
    int base = warmup, count = n - warmup;
    float *v = (float *)malloc((size_t)count * sizeof *v);
    if (!v) return;
    for (int i = 0; i < count; i++) v[i] = rows[base + i][PROF_FRAME];
    qsort(v, (size_t)count, sizeof *v, stall_cmp);
    float med = v[count / 2], p90 = v[(int)((float)count * 0.90f)], p99 = v[(int)((float)count * 0.99f)], mx = v[count - 1];
    int over12 = 0, over25 = 0, over2x = 0;
    for (int i = 0; i < count; i++) { if (v[i] > 12.0f) over12++; if (v[i] > 25.0f) over25++; if (v[i] > med * 2.0f) over2x++; }
    SDL_Log("stall: %d frames  median %.2f  p90 %.2f  p99 %.2f  max %.2f ms  |  over 12ms %d  over 25ms %d  over 2x median %d",
            count, (double)med, (double)p90, (double)p99, (double)mx, over12, over25, over2x);
    free(v);
    // The ten worst frames, each with the phases that were not noise in it.
    int worst[10]; int nw = 0;
    for (int i = 0; i < count; i++) {
        float f = rows[base + i][PROF_FRAME];
        int at = nw;
        while (at > 0 && rows[base + worst[at - 1]][PROF_FRAME] < f) { if (at < 10) worst[at] = worst[at - 1]; at--; }
        if (at < 10) { worst[at] = i; if (nw < 10) nw++; }
    }
    for (int k = 0; k < nw; k++) {
        const float *r = rows[base + worst[k]];
        char line[320]; int at = 0;
        at += snprintf(line + at, sizeof line - at, "stall:   #%d  %.2f ms =", base + worst[k], (double)r[PROF_FRAME]);
        for (int p = 1; p < PROF_COUNT && at < (int)sizeof line - 24; p++)
            if (r[p] > 0.20f) at += snprintf(line + at, sizeof line - at, " %s %.2f", prof_phase_name((ProfPhase)p), (double)r[p]);
        SDL_Log("%s", line);
    }
    if (csv && csv[0]) {
        FILE *f = fopen(csv, "w");
        if (f) {
            fprintf(f, "frame");
            for (int p = 0; p < PROF_COUNT; p++) fprintf(f, ",%s", prof_phase_name((ProfPhase)p));
            fprintf(f, "\n");
            for (int i = 0; i < n; i++) { fprintf(f, "%d", i); for (int p = 0; p < PROF_COUNT; p++) fprintf(f, ",%.4f", (double)rows[i][p]); fprintf(f, "\n"); }
            fclose(f);
            SDL_Log("stall: per-frame CSV written to %s", csv);
        }
    }
}

int main(int argc, char **argv) {
    platform_use_base_dir();   // portable builds run from the executable's directory
    // The game thread asks to be treated as interactive. On a machine with nothing else running
    // this changes nothing; on a machine that is also compiling something -- which is most
    // development machines, and plenty of players' -- an ordinary-priority main thread gets
    // preempted for as long as the scheduler feels like, and a frame that should have cost 7 ms
    // costs a couple of hundred. That is not a frame the renderer can be blamed for and not one
    // any profiling of this process will explain, because the process was not running. macOS maps
    // this onto the user-interactive QoS class, which is what every other real-time-ish
    // application on the machine is already asking for. HOLLOW_NO_PRIORITY=1 opts out.
    if (!SDL_getenv("HOLLOW_NO_PRIORITY") && !SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "could not raise the game thread's priority: %s", SDL_GetError());
    // --frames N      exit after N frames (headless checks, CI)
    // --screenshot P  write the internal frame to P before exiting
    // --start S       begin in state S: explore (default), fight, end
    // --bot           let a simple bot play the fight (with --start fight)
    // --volume V      master volume 0..1;  --quiet = 0.15;  --debug starts with the overlay on
    // --hero NAME     play with assets/characters/NAME.txt as the player
    // --host PORT [--slots N] | --join HOST:PORT | --name NAME  multiplayer (see netgame.h)
    // --no-scenes     skip cutscene playback from triggers and NPC talk (multiplayer sets this too)
    // --third         force third-person view
    // --first         force first-person view (R.E.P.O.-style: eye in the head, own model hidden)
    // --log FILE      write the debug log to FILE instead of hollow.log
    // --test NAME     run a scripted headless check: "throw" hurls a fragile item at a wall
    // --- voice ---
    // --voice ptt|open|off        push to talk (V / left bumper), open mic with an energy gate, or off
    // --voice-volume V            voice chat level, independent of --volume
    // --voice-monitor             hear your own changed voice locally
    // HOLLOW_VOICE_WAV=FILE       feed a WAV instead of a microphone (no recording device is opened)
    // HOLLOW_VOICE_DUMP=FILE      write the local voice bus, and one .slotN.wav per speaker, as WAVs
    // HOLLOW_SILENT=1             open no audio playback device at all (automated runs must be
    //                             silent). Implied by either voice hook above. Not the same as
    //                             --volume 0, which mutes the game but leaves voice audible.
    // --quality potato|normal|high   overrides settings.txt's `quality` line for this run (see quality.h)
    // --menu-test S   drive the main menu without a hand on the keyboard: "host", "join:HOST:PORT",
    //                 "settings" (open the settings page) or "settings:volume=40,vsync=off" (open it
    //                 and work the arrows until every named row is there, writing settings.txt)
    // With none of --host --join --level --start --bot, the game opens on the main menu (GS_MENU).
    // --bench [FILE]        run the four scripted perf paths headless and write FILE (bench.json).
    // --bench-shots DIR     also write DIR/<path>.png, one PNG per path, for a human to eyeball.
    const char *menu_test = NULL; bool menu_boot = true;
    const char *bench_json = NULL, *bench_shot_dir = NULL;
    bool bench_active = bench_requested(argc, argv, &bench_json, &bench_shot_dir);
    int max_frames = -1; const char *shot = NULL; const char *tool_shot = NULL; const char *shot_every_dir = NULL; int shot_every = 0; bool spawn_set = false; float spawn_x = 0, spawn_z = 0; const char *start = NULL; bool bot = false; float volume = 1.0f; const char *shot_when = NULL;
    bool debug_on = false, console_on = false; int tool_mode = 0; int fps_cap = 0; int vsync = 1; bool log_set = false;
    float mouse_sens = 1.0f;   // settings.txt `mouse_sens`: a multiplier on the default radians per mouse pixel
    bool sprint_toggle = false;   // settings.txt `sprint hold|toggle`: hold is the default and what every
                                  // shooter since Half-Life does; toggle is an accessibility option, not a mode.
    const char *voice_mode = "ptt"; float voice_vol = 1.0f; int voice_mon = 0;   // --- voice ---
    char quality_word[32] = ""; bool quality_cli_given = false;   // settings.txt `quality`, or --quality below
    static Game game;   // large; static keeps it off the stack (and zeroed)
    // settings.txt next to the assets folder: volume V, debug 0|1, hero NAME, fps N (0 = display rate), vsync 0|1, level NAME, quality potato|normal|high. Command-line flags override it.
    { char sp[640]; snprintf(sp, sizeof sp, "%s/settings.txt", HOLLOW_ASSET_DIR); size_t sn; char *st = SDL_LoadFile(sp, &sn);
      if (st) { char *cur = st; while (*cur) { char *line = cur; char *nl = strchr(cur, '\n'); if (nl) { *nl = 0; cur = nl + 1; } else cur += strlen(cur);
          char *hash = strchr(line, '#'); if (hash) *hash = 0; char key[32], val[128];
          if (sscanf(line, "%31s %127s", key, val) == 2) { if (!strcmp(key, "volume")) volume = (float)atof(val); else if (!strcmp(key, "debug")) debug_on = atoi(val) != 0; else if (!strcmp(key, "hero")) snprintf(game.hero_config, sizeof game.hero_config, "%s", val); else if (!strcmp(key, "fps")) fps_cap = atoi(val); else if (!strcmp(key, "vsync")) vsync = atoi(val); else if (!strcmp(key, "mouse_sens")) mouse_sens = (float)atof(val); else if (!strcmp(key, "sprint")) sprint_toggle = !strcmp(val, "toggle"); else if (!strcmp(key, "voice")) voice_mode = SDL_strdup(val);                       /* --- voice --- */ else if (!strcmp(key, "voice_volume")) voice_vol = (float)atof(val); else if (!strcmp(key, "voice_monitor")) voice_mon = atoi(val); else if (!strcmp(key, "level") && !game.level_path[0]) snprintf(game.level_path, sizeof game.level_path, "%s/levels/%s.txt", HOLLOW_ASSET_DIR, val); else if (!strcmp(key, "quality")) snprintf(quality_word, sizeof quality_word, "%s", val); } }
      if (SDL_getenv("HOLLOW_FPS")) fps_cap = atoi(SDL_getenv("HOLLOW_FPS"));
      if (SDL_getenv("HOLLOW_NOVSYNC")) vsync = 0;
        SDL_free(st); } }
    netgame_parse_args(&game.net, argc, argv);   // --host/--slots/--join/--name; must run before game_init
    if (netgame_on(&game.net)) game.no_scenes = true;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) max_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--tool-shot") && i + 1 < argc) tool_shot = argv[++i];   // PNG of the tool window's UI before exiting
        else if (!strcmp(argv[i], "--shot-every") && i + 2 < argc) { shot_every = atoi(argv[++i]); shot_every_dir = argv[++i]; }   // N DIR: a PNG every N frames
        else if (!strcmp(argv[i], "--spawn") && i + 2 < argc) { spawn_set = true; spawn_x = (float)atof(argv[++i]); spawn_z = (float)atof(argv[++i]); }   // start the hero at x z (captures)
        else if (!strcmp(argv[i], "--start") && i + 1 < argc) start = argv[++i];
        else if (!strcmp(argv[i], "--bot")) bot = true;
        else if (!strcmp(argv[i], "--level") && i + 1 < argc) snprintf(game.level_path, sizeof game.level_path, "%s/levels/%s.txt", HOLLOW_ASSET_DIR, argv[++i]);
        else if (!strcmp(argv[i], "--volume") && i + 1 < argc) volume = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--quiet")) volume = 0.15f;
        else if (!strcmp(argv[i], "--voice") && i + 1 < argc) voice_mode = argv[++i];                  // --- voice ---
        else if (!strcmp(argv[i], "--voice-volume") && i + 1 < argc) voice_vol = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--voice-monitor")) voice_mon = 1;
        else if (!strcmp(argv[i], "--debug")) debug_on = true;
        else if (!strcmp(argv[i], "--console")) console_on = true;
        else if (!strcmp(argv[i], "--tool") && i + 1 < argc) tool_mode = atoi(argv[++i]);   // 2 = world editor, 4 = character builder
        else if (!strcmp(argv[i], "--shot-when") && i + 1 < argc) shot_when = argv[++i];
        else if (!strcmp(argv[i], "--hero") && i + 1 < argc) snprintf(game.hero_config, sizeof game.hero_config, "%s", argv[++i]);   // ring | judge | play: screenshot at that battle moment, then exit
        else if (!strcmp(argv[i], "--no-scenes")) game.no_scenes = true;
        else if (!strcmp(argv[i], "--third")) game.force_third = true;
        else if (!strcmp(argv[i], "--first")) game.force_first = true;
        else if (!strcmp(argv[i], "--log") && i + 1 < argc) { log_set = true; snprintf(game.log_path, sizeof game.log_path, "%s", argv[++i]); }
        else if (!strcmp(argv[i], "--test") && i + 1 < argc) snprintf(game.test_mode, sizeof game.test_mode, "%s", argv[++i]);   // scripted headless check: throw
        else if (!strcmp(argv[i], "--menu-test") && i + 1 < argc) menu_test = argv[++i];   // --- menu --- scripted menu run
        else if (!strcmp(argv[i], "--quality") && i + 1 < argc) { snprintf(quality_word, sizeof quality_word, "%s", argv[++i]); quality_cli_given = true; }
        // --host/--slots/--join/--name already consumed by netgame_parse_args; skip so they are not mistaken for something else
        else if (!strcmp(argv[i], "--host") && i + 1 < argc) i++;
        else if (!strcmp(argv[i], "--slots") && i + 1 < argc) i++;
        else if (!strcmp(argv[i], "--join") && i + 1 < argc) i++;
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) i++;
    }
    // --- menu --- a flag that already says what to do skips the front door: multiplayer that was
    // asked for on the command line, a level or a state to jump to, a bot, a tool, a scripted check.
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--host") || !strcmp(argv[i], "--join") || !strcmp(argv[i], "--level")
            || !strcmp(argv[i], "--start") || !strcmp(argv[i], "--bot")
            || !strcmp(argv[i], "--tool") || !strcmp(argv[i], "--test")) menu_boot = false;
    if (!log_set && game.net.name[0]) snprintf(game.log_path, sizeof game.log_path, "hollow_%s.log", game.net.name);

    // --bench: a fixed, comparable island every time, whatever settings.txt or the command line
    // otherwise asked for. --level still wins if it was given explicitly, so a bench run can be
    // pointed at a different level on purpose; menu_boot is already forced off by the earlier
    // "something already says what to do" scan (--bench doesn't say so explicitly, so it is
    // repeated here). HOLLOW_BOT=shoot has to be set before game_init because weaponbot.c latches
    // it the first time it is asked (see weaponbot.c's bot_enabled) -- bench.c's shootout path is
    // the only one that turns g->bot on, so the other three paths never see it fire.
    // A bench run takes the compositor out of the loop as well as the vsync. Asking for IMMEDIATE
    // is not enough on macOS: the window server hands out drawables at its own pace and puts a
    // floor of about 120 frames a second under every number, which is why the first four-path run
    // came back with all four medians at 8.2 ms and 7.5 of those spent inside the swapchain
    // acquire. HOLLOW_NOPRESENT draws the whole frame -- including the upscale to the window's real
    // pixel count -- into off-screen targets and paces the CPU off a fence instead. See
    // platform_begin_frame. Set it here, before platform_init, which is where it is read.
    if (bench_active) SDL_setenv_unsafe("HOLLOW_NOPRESENT", "1", 1);
    // And no microphone. Voice opens a real capture device and encodes Opus on the game thread at
    // a bitrate that varies with what the room sounds like, which showed up as the shootout path
    // swinging between 2.1 and 3.7 ms from run to run while its draw counts stayed identical to
    // the draw. A benchmark cannot listen to the room.
    if (bench_active) voice_mode = "off";
    if (bench_active) {
        bool level_given = false;
        for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--level")) level_given = true;
        if (!level_given) snprintf(game.level_path, sizeof game.level_path, "%s/levels/island.txt", HOLLOW_ASSET_DIR);
        game.no_scenes = true;
        menu_boot = false;
        SDL_setenv_unsafe("HOLLOW_BOT", "shoot", 1);
    }

    // Resolve the quality tier before anything GPU-shaped exists: small_hdr (quality_apply_early,
    // below) has to be decided before gfx_init creates the swapchain's HDR target. A bench run
    // must not depend on the player's settings.txt tier, must not guess or probe (both take real
    // frames a benchmark can't afford to spend on itself), and must not write settings.txt back --
    // a benchmark that changes the ground it's measured on is not a benchmark -- so it ignores
    // whatever settings.txt said unless --quality asked for a tier explicitly, and defaults to normal.
    if (bench_active && !quality_cli_given) quality_word[0] = 0;
    bool quality_was_unset = false;
    Quality startup_quality;
    if (quality_word[0]) { startup_quality = quality_parse(quality_word, Q_NORMAL); quality_set(startup_quality); }
    else if (!bench_active) startup_quality = quality_startup(&quality_was_unset);
    else { startup_quality = Q_NORMAL; quality_set(startup_quality); }
    if (quality_was_unset && !bench_active) {
        // The probe itself starts further down, once loading is over -- see the comment there.
        // Park the guess in settings.txt right away, so the next run starts from it instead of
        // guessing again even if this run never reaches the two-second probe (killed early, a
        // crash, --frames cutting it short). If the probe below finds the guess wrong it corrects
        // this same line in place.
        game_settings_set(&game, "quality", quality_name(startup_quality));
    }
    quality_apply_early(startup_quality);

    Platform pf;
    if (!platform_init(&pf, "hollow", 1280, 800)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "platform_init failed: %s", SDL_GetError());
        return 1;
    }

    game_init(&game);
    if (!game_init_gfx(&game, &pf)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "gfx init failed: %s", SDL_GetError());
        return 1;
    }
    // load_defs (inside game_init_gfx) already re-asserted this tier once, before its own texture
    // loads -- see the comment there. Calling it again here is a cheap no-op (gfx_set_render_scale
    // and gfx_set_shadow_size both skip the rebuild when the size did not change) and is the one
    // place quality_apply runs unconditionally, whatever the level did or did not just do.
    quality_apply(&game, startup_quality);
    pf.debug = debug_on;
    if (console_on) game_set_tool(&game, 1);
    if (start) game_start_at(&game, start);
    if (!netgame_start(&game)) return 1;   // after --start, so the host reports the level it is really on
    // --- voice --- after netgame_start, which is what decides this process's slot, and after
    // audio_init (in game_init), which owns the mixer the voice bus hangs off.
    voice_set_mode_name(voice_mode); voice_set_volume(voice_vol); voice_set_monitor(voice_mon != 0);
    voice_init(&game);
    pf.fps_cap = fps_cap; if (!vsync) platform_set_vsync(&pf, false); else pf.vsync = true;
    camera_set_mouse_sens(mouse_sens);
    platform_set_sprint_mode(&pf, sprint_toggle);
    if (spawn_set) { PLAYER(&game).c.pos.x = spawn_x; PLAYER(&game).c.pos.z = spawn_z; }
    if (tool_mode) game_set_tool(&game, tool_mode);
    // --- menu --- nothing was asked for: open the main menu over the island
    if (menu_boot || menu_test) menu_open_main(&game);
    if (menu_test) menu_set_test(&game, menu_test);
    game.bot = bot;
    audio_set_master(volume * 0.8f);
    // --bench: uncapped and unsynced, so a frame time is the renderer's cost and not the display's
    // (see platform_present_mode_name's own comment on what asking for no vsync actually gets you).
    if (bench_active) { platform_set_vsync(&pf, false); pf.fps_cap = 0; bench_init(&game, &pf, bench_json, bench_shot_dir); }

    // The quality probe's two-second window has to cover two seconds of PLAY. Starting it back
    // where the tier was guessed put the window inside the level load that follows -- fifteen
    // seconds of models and textures on this island -- so it had always expired before the first
    // rendered frame, and quality_probe_frame concluded on frame one against an empty history:
    // "quality: probe median 0.00ms at normal -- keeping it", on every machine, every time. The
    // probe could therefore never do the one thing it exists for, which is drop a machine that
    // cannot hold the 12 ms budget down to potato. Start the clock here instead, after loading,
    // where quality_probe_start's prof_reset() also lands on the right frame.
    if (quality_was_unset && !bench_active) quality_probe_start();

    // HOLLOW_FIXED_FPS=N is HOLLOW_FIXED_DT's finer sibling: the clock advances 1/N of a second per
    // rendered frame instead of a whole tick, which is the only way to capture a strip of genuinely
    // CONSECUTIVE 144 Hz frames -- writing a PNG takes a third of a second, so on the wall clock
    // eight "consecutive" screenshots are a third of a second apart and show nothing about
    // smoothness. Never set in a played game.
    double fixed_frame = SDL_getenv("HOLLOW_FIXED_FPS") && atof(SDL_getenv("HOLLOW_FIXED_FPS")) > 0
                       ? 1.0 / atof(SDL_getenv("HOLLOW_FIXED_FPS")) : 0.0;
    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 prev = SDL_GetPerformanceCounter();
    double accumulator = 0.0;
    bool running = true;
    // HOLLOW_FIXED_DT: advance exactly one tick per rendered frame and ignore the wall clock, so
    // a given --frames count always lands on the same moment of the simulation. Without it two
    // captures of the same scene under two different render settings run at different speeds,
    // reach different points in a bot's walk, and cannot be compared. Frame timing is still
    // measured from the real clock, so this does not lie about performance.
    // --bench forces the same one-tick-per-frame determinism HOLLOW_FIXED_DT does: two runs of the
    // same path must reach the same tick on the same rendered frame, or their draw counts (and
    // screenshots) cannot be compared.
    const bool fixed_step = SDL_getenv("HOLLOW_FIXED_DT") != NULL || bench_active;
    // Frame times for the perf line. The smoothed game.frame_ms is the right thing on screen but
    // useless for comparing two runs: it is whatever the last twenty frames happened to cost, and
    // on a machine with anything else on it that lands anywhere. The median over a whole run,
    // with the first second thrown away, is the number worth quoting.
    enum { PERF_MAX = 30000, PERF_WARMUP = 60 };
    static float perf_ms[PERF_MAX];
    int perf_n = 0;
    Uint64 real_prev = prev;
    // ---- the stall report -------------------------------------------------------------------
    // A median is not what a player feels; the frame that took 200 ms is. Every rendered frame's
    // whole phase breakdown is kept (2 MB of static, no allocation in the loop) and turned into a
    // `stall:` block at exit: the percentiles, how many frames went over the 12 ms budget, and the
    // ten worst frames with WHERE the time went in each. HOLLOW_STALL=FILE also writes the lot as
    // a CSV, one row per frame, for anything that wants to plot it. The recording itself is a
    // memcpy of 19 floats per frame -- it cannot be what it measures.
    static float stall_ph[PERF_MAX][PROF_COUNT];
    int stall_n = 0;
    const char *stall_csv = SDL_getenv("HOLLOW_STALL");

    while (running) {
        prof_frame_begin();   // closes the previous frame's PROF_FRAME and pushes its phase totals into the history ring
        if (stall_n < PERF_MAX) { prof_copy_frame(stall_ph[stall_n]); if (stall_ph[stall_n][PROF_FRAME] > 0) stall_n++; }
        // A frame that cost more than twice the budget goes into hollow.log the moment it happens,
        // with the phases that were in it. A player saying "it stutters" and a log saying "median
        // 6.9 ms" are both true and neither is useful; this is the line that names the phase.
        // Capped at forty, because a genuinely broken run must not turn the log into the problem.
        if (stall_n > 1 && game.frames_total > 120) {
            const float *r = stall_ph[stall_n - 1];
            static int shouted = 0;
            if (r[PROF_FRAME] > 25.0f && shouted < 40) {
                shouted++;
                char line[300]; int at = 0;
                at += snprintf(line + at, sizeof line - at, "hitch: frame %u took %.1f ms =", game.frames_total, (double)r[PROF_FRAME]);
                for (int p = 1; p < PROF_COUNT && at < (int)sizeof line - 24; p++)
                    if (r[p] > 0.5f) at += snprintf(line + at, sizeof line - at, " %s %.1f", prof_phase_name((ProfPhase)p), (double)r[p]);
                dbg_log("%s", line);
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        double frame_dt = fixed_frame > 0 ? fixed_frame : fixed_step ? TICK_DT : (double)(now - prev) / (double)freq;
        game.frame_dt_raw = (float)frame_dt;   // before the clamp: what the meters have to be told
        prev = now;
        if (frame_dt > MAX_FRAME_DT) frame_dt = MAX_FRAME_DT;
        accumulator += frame_dt;

        // Input is polled once per frame; the sim consumes the latest state each tick.
        prof_begin(PROF_INPUT);
        running = platform_poll(&pf);
        prof_mark_input();   // the newest input state is ready now; input->present latency starts here
        if (pf.want_quit) running = false;

        // bench_pre_tick runs before game_view_look on purpose: it writes into pf.input, and
        // game_view_look is what turns look_x into an actual camera_look call this frame.
        if (bench_active && !bench_pre_tick(&game, &pf, &pf.input)) running = false;

        // Mouse look happens HERE: at the frame rate, before the ticks that read the view yaw, so a
        // 144 Hz screen turns 144 times a second and the movement direction the sim uses is the one
        // the mouse asked for this frame rather than up to 16.7 ms ago (Half-Life does the same).
        game_view_look(&game, &pf, (float)frame_dt);
        prof_end(PROF_INPUT);

        // One stall must not buy a second one. MAX_FRAME_DT already stops the accumulator running
        // away, but a quarter of a second of backlog is still fifteen ticks, and running fifteen
        // ticks inside one frame makes that frame late too -- which hands the next frame a backlog
        // of its own. That is the ladder behind the "stall, then a catch-up jump of hundreds of
        // millimetres" pattern in the smooth: log: the eye covers fifteen ticks of walking between
        // two pictures. So there is a ceiling on how much simulation one frame will do, and
        // anything past it is dropped rather than owed. Time is lost either way after a hitch; the
        // choice is only whether it is lost quietly or paid for with a second late frame.
        prof_begin(PROF_TICK);
        int ticks_this_frame = 0;
        while (accumulator >= TICK_DT && ticks_this_frame < MAX_TICKS_PER_FRAME) {
            voice_update(&game, &pf.input, (float)TICK_DT);   // --- voice --- before the tick, so a
            // frame captured now rides out on this tick's input packet instead of the next one
            game_tick(&game, &pf.input, TICK_DT);
            platform_clear_edges(&pf);   // each press is seen by exactly one tick
            accumulator -= TICK_DT;
            ticks_this_frame++;
        }
        if (accumulator >= TICK_DT) accumulator = fmod(accumulator, TICK_DT);   // drop the backlog, keep the phase
        prof_end(PROF_TICK);

        // The two-second probe (armed only when settings.txt had no `quality` line): never runs
        // during --bench, which must not probe and must not write settings.txt under itself.
        if (!bench_active) quality_probe_frame(&game, &pf);

        { Uint64 rn = SDL_GetPerformanceCounter();
          if (perf_n < PERF_MAX) perf_ms[perf_n++] = (float)((double)(rn - real_prev) * 1000.0 / (double)freq);
          real_prev = rn; }
        double alpha = accumulator / TICK_DT;  // for render interpolation
        game.frame_wall = (double)now / (double)freq;
        game.render_frame_dt = (float)frame_dt;   // the pacing clock: stamped where frame_dt is measured, not after the GPU has been waited on
        prof_begin(PROF_PRESENT_WAIT);
        platform_begin_frame(&pf);
        prof_end(PROF_PRESENT_WAIT);
        if (tool_shot) gfx_tool_screenshot_request(&game.gfx, pf.tool_w > 0 ? pf.tool_w : 720, pf.tool_h > 0 ? pf.tool_h : 820);
        char bench_shot_path[640] = "";
        if (bench_active) bench_pre_render(&game, &pf, bench_shot_path, sizeof bench_shot_path);
        prof_begin(PROF_RENDER);
        game_render(&game, &pf, (float)alpha);
        prof_end(PROF_RENDER);
        prof_mark_present();   // handed to the driver now: how old the newest input is at this point
        prof_begin(PROF_SUBMIT);
        platform_end_frame(&pf);
        prof_end(PROF_SUBMIT);
        platform_clear_frame_edges(&pf);
        if (shot_every > 0 && game.frames_total % shot_every == 0) { char sp[640]; snprintf(sp, sizeof sp, "%s/f%06u.png", shot_every_dir, game.frames_total); game_screenshot(&game, sp); }
        if (bench_shot_path[0]) game_screenshot(&game, bench_shot_path);   // gfx_screenshot reads back what was just submitted, so this has to be after platform_end_frame
        game.frames_total++;
        if (max_frames >= 0 && --max_frames == 0) running = false;
        if (shot_when && shot && game_shot_moment(&game, shot_when)) { game_screenshot(&game, shot); shot = NULL; running = false; }
    }
    if (bench_active) bench_finish(&game, &pf);
    if (shot) game_screenshot(&game, shot);
    if (tool_shot) game_tool_screenshot(&game, tool_shot);
    SDL_Log("perf: %.1f ms/frame (%.0f fps) draws %u props %d\n", game.frame_ms, game.frame_ms > 0 ? 1000.0f / game.frame_ms : 0, game.gfx.draw_calls, game.level.nprops);
    if (perf_n > PERF_WARMUP + 8) {
        int n = perf_n - PERF_WARMUP;
        float *v = (float *)malloc((size_t)n * sizeof *v);
        if (v) {
            memcpy(v, perf_ms + PERF_WARMUP, (size_t)n * sizeof *v);
            for (int i = 1; i < n; i++) { float k = v[i]; int j = i - 1; while (j >= 0 && v[j] > k) { v[j + 1] = v[j]; j--; } v[j + 1] = k; }
            SDL_Log("perftime: median %.3f ms  p10 %.3f  p90 %.3f  min %.3f  frames %d", v[n / 2], v[n / 10], v[n - 1 - n / 10], v[0], n);
            free(v);
        }
    }
    stall_report(stall_ph, stall_n, PERF_WARMUP, stall_csv);
    prof_dump(game.level_path[0] ? game.level_path : "run");
    SDL_Log("stats: battle=%d enemy_hp=%d round=%d | state=%d parries=%u hits_taken=%u deaths=%u boss_hp=%.0f player_hp=%.0f player_yaw=%.0f flash=%.2f t=%.3f player=(%.1f %.1f %.1f) boss=(%.1f %.1f %.1f) cam=(%.1f %.1f %.1f) dist=%.1f",
            game.battle.state, game.battle.enemy_hp, game.battle.round, game.state, game.parries, game.hits_taken, game.deaths, game.boss.c.hp, PLAYER(&game).c.hp, PLAYER(&game).c.yaw / DEG2RAD, game.flash, game.time,
            PLAYER(&game).c.pos.x, PLAYER(&game).c.pos.y, PLAYER(&game).c.pos.z, game.boss.c.pos.x, game.boss.c.pos.y, game.boss.c.pos.z, game.cam.eye.x, game.cam.eye.y, game.cam.eye.z, game.cam.cur_dist);

    voice_shutdown();   // --- voice --- before the audio device goes away with game_shutdown
    netgame_shutdown(&game);
    game_shutdown(&game);
    platform_shutdown(&pf);
    return 0;
}
