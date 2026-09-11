// hollow - entry point.
// Fixed-timestep simulation, variable-rate rendering, SDL3 GPU backend.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "game.h"
#include "prof.h"
#include "quality.h"  // quality potato|normal|high: settings.txt key, --quality flag, the startup guess/probe
#include "camera.h"   // camera_set_mouse_sens: settings.txt owns the sensitivity
#include "audio.h"
#include "voice.h"   // --- voice ---
#include "bench.h"   // --bench: four scripted camera paths, timed and dumped as JSON

#define TICK_HZ 60
#define TICK_DT (1.0 / TICK_HZ)
#define MAX_FRAME_DT 0.25  // clamp after a stall so we don't spiral

int main(int argc, char **argv) {
    platform_use_base_dir();   // portable builds run from the executable's directory
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
    // --menu-test S   drive the main menu without a hand on the keyboard: "host" or "join:HOST:PORT"
    // With none of --host --join --level --start --bot, the game opens on the main menu (GS_MENU).
    // --bench [FILE]        run the four scripted perf paths headless and write FILE (bench.json).
    // --bench-shots DIR     also write DIR/<path>.png, one PNG per path, for a human to eyeball.
    const char *menu_test = NULL; bool menu_boot = true;
    const char *bench_json = NULL, *bench_shot_dir = NULL;
    bool bench_active = bench_requested(argc, argv, &bench_json, &bench_shot_dir);
    int max_frames = -1; const char *shot = NULL; const char *tool_shot = NULL; const char *shot_every_dir = NULL; int shot_every = 0; bool spawn_set = false; float spawn_x = 0, spawn_z = 0; const char *start = NULL; bool bot = false; float volume = 1.0f; const char *shot_when = NULL;
    bool debug_on = false, console_on = false; int tool_mode = 0; int fps_cap = 0; int vsync = 1; bool log_set = false;
    float mouse_sens = 1.0f;   // settings.txt `mouse_sens`: a multiplier on the default radians per mouse pixel
    const char *voice_mode = "ptt"; float voice_vol = 1.0f; int voice_mon = 0;   // --- voice ---
    char quality_word[32] = ""; bool quality_cli_given = false;   // settings.txt `quality`, or --quality below
    static Game game;   // large; static keeps it off the stack (and zeroed)
    // settings.txt next to the assets folder: volume V, debug 0|1, hero NAME, fps N (0 = display rate), vsync 0|1, level NAME, quality potato|normal|high. Command-line flags override it.
    { char sp[640]; snprintf(sp, sizeof sp, "%s/settings.txt", HOLLOW_ASSET_DIR); size_t sn; char *st = SDL_LoadFile(sp, &sn);
      if (st) { char *cur = st; while (*cur) { char *line = cur; char *nl = strchr(cur, '\n'); if (nl) { *nl = 0; cur = nl + 1; } else cur += strlen(cur);
          char *hash = strchr(line, '#'); if (hash) *hash = 0; char key[32], val[128];
          if (sscanf(line, "%31s %127s", key, val) == 2) { if (!strcmp(key, "volume")) volume = (float)atof(val); else if (!strcmp(key, "debug")) debug_on = atoi(val) != 0; else if (!strcmp(key, "hero")) snprintf(game.hero_config, sizeof game.hero_config, "%s", val); else if (!strcmp(key, "fps")) fps_cap = atoi(val); else if (!strcmp(key, "vsync")) vsync = atoi(val); else if (!strcmp(key, "mouse_sens")) mouse_sens = (float)atof(val); else if (!strcmp(key, "voice")) voice_mode = SDL_strdup(val);                       /* --- voice --- */ else if (!strcmp(key, "voice_volume")) voice_vol = (float)atof(val); else if (!strcmp(key, "voice_monitor")) voice_mon = atoi(val); else if (!strcmp(key, "level") && !game.level_path[0]) snprintf(game.level_path, sizeof game.level_path, "%s/levels/%s.txt", HOLLOW_ASSET_DIR, val); else if (!strcmp(key, "quality")) snprintf(quality_word, sizeof quality_word, "%s", val); } }
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
        quality_probe_start();   // only when settings.txt had no `quality` line
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

    while (running) {
        prof_frame_begin();   // closes the previous frame's PROF_FRAME and pushes its phase totals into the history ring
        Uint64 now = SDL_GetPerformanceCounter();
        double frame_dt = fixed_frame > 0 ? fixed_frame : fixed_step ? TICK_DT : (double)(now - prev) / (double)freq;
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

        prof_begin(PROF_TICK);
        while (accumulator >= TICK_DT) {
            voice_update(&game, &pf.input, (float)TICK_DT);   // --- voice --- before the tick, so a
            // frame captured now rides out on this tick's input packet instead of the next one
            game_tick(&game, &pf.input, TICK_DT);
            platform_clear_edges(&pf);   // each press is seen by exactly one tick
            accumulator -= TICK_DT;
        }
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
