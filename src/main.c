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
#include "audio.h"

#define TICK_HZ 60
#define TICK_DT (1.0 / TICK_HZ)
#define MAX_FRAME_DT 0.25  // clamp after a stall so we don't spiral

int main(int argc, char **argv) {
    // --frames N      exit after N frames (headless checks, CI)
    // --screenshot P  write the internal frame to P before exiting
    // --start S       begin in state S: explore (default), fight, end
    // --bot           let a simple bot play the fight (with --start fight)
    // --volume V      master volume 0..1;  --quiet = 0.15;  --debug starts with the overlay on
    // --edit NAME     open the sprite editor on assets/sprites/own/NAME (created if missing); --size N frame size for new characters
    // --hero NAME     play with assets/characters/NAME.txt as the player
    int max_frames = -1; const char *shot = NULL; const char *start = NULL; bool bot = false; float volume = 1.0f; const char *shot_when = NULL;
    const char *edit = NULL; int edit_size = 32; bool debug_on = false, console_on = false; int tool_mode = 0;
    static Game game;   // large; static keeps it off the stack (and zeroed)
    // settings.txt next to the assets folder: volume V, debug 0|1, hero NAME. Command-line flags override it.
    { char sp[640]; snprintf(sp, sizeof sp, "%s/settings.txt", HOLLOW_ASSET_DIR); size_t sn; char *st = SDL_LoadFile(sp, &sn);
      if (st) { char *cur = st; while (*cur) { char *line = cur; char *nl = strchr(cur, '\n'); if (nl) { *nl = 0; cur = nl + 1; } else cur += strlen(cur);
          char *hash = strchr(line, '#'); if (hash) *hash = 0; char key[32], val[128];
          if (sscanf(line, "%31s %127s", key, val) == 2) { if (!strcmp(key, "volume")) volume = (float)atof(val); else if (!strcmp(key, "debug")) debug_on = atoi(val) != 0; else if (!strcmp(key, "hero")) snprintf(game.hero_config, sizeof game.hero_config, "%s", val); } }
        SDL_free(st); } }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) max_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--start") && i + 1 < argc) start = argv[++i];
        else if (!strcmp(argv[i], "--bot")) bot = true;
        else if (!strcmp(argv[i], "--level") && i + 1 < argc) snprintf(game.level_path, sizeof game.level_path, "%s/levels/%s.txt", HOLLOW_ASSET_DIR, argv[++i]);
        else if (!strcmp(argv[i], "--volume") && i + 1 < argc) volume = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--quiet")) volume = 0.15f;
        else if (!strcmp(argv[i], "--debug")) debug_on = true;
        else if (!strcmp(argv[i], "--console")) console_on = true;
        else if (!strcmp(argv[i], "--tool") && i + 1 < argc) tool_mode = atoi(argv[++i]);   // 2 = environment editor, 3 = sprite editor
        else if (!strcmp(argv[i], "--shot-when") && i + 1 < argc) shot_when = argv[++i];
        else if (!strcmp(argv[i], "--edit") && i + 1 < argc) edit = argv[++i];
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) edit_size = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--hero") && i + 1 < argc) snprintf(game.hero_config, sizeof game.hero_config, "%s", argv[++i]);   // ring | judge | play: screenshot at that battle moment, then exit
    }

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
    pf.debug = debug_on;
    if (console_on) game_set_tool(&game, 1);
    if (start) game_start_at(&game, start);
    if (edit) game_open_editor(&game, edit, edit_size);
    if (tool_mode) game_set_tool(&game, tool_mode);
    game.bot = bot;
    audio_set_master(volume * 0.8f);

    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 prev = SDL_GetPerformanceCounter();
    double accumulator = 0.0;
    bool running = true;

    while (running) {
        Uint64 now = SDL_GetPerformanceCounter();
        double frame_dt = (double)(now - prev) / (double)freq;
        prev = now;
        if (frame_dt > MAX_FRAME_DT) frame_dt = MAX_FRAME_DT;
        accumulator += frame_dt;

        // Input is polled once per frame; the sim consumes the latest state each tick.
        running = platform_poll(&pf);
        if (pf.want_quit) running = false;

        while (accumulator >= TICK_DT) {
            game_tick(&game, &pf.input, TICK_DT);
            platform_clear_edges(&pf);   // each press is seen by exactly one tick
            accumulator -= TICK_DT;
        }

        double alpha = accumulator / TICK_DT;  // for render interpolation
        platform_begin_frame(&pf);
        game_render(&game, &pf, (float)alpha);
        platform_end_frame(&pf);
        if (max_frames >= 0 && --max_frames == 0) running = false;
        if (shot_when && shot && game_shot_moment(&game, shot_when)) { game_screenshot(&game, shot); shot = NULL; running = false; }
    }
    if (shot) game_screenshot(&game, shot);
    SDL_Log("stats: battle=%d enemy_hp=%d round=%d | state=%d parries=%u hits_taken=%u deaths=%u boss_hp=%.0f player_hp=%.0f player_yaw=%.0f flash=%.2f t=%.3f player=(%.1f %.1f %.1f) boss=(%.1f %.1f %.1f) cam=(%.1f %.1f %.1f) dist=%.1f",
            game.battle.state, game.battle.enemy_hp, game.battle.round, game.state, game.parries, game.hits_taken, game.deaths, game.boss.c.hp, game.player.c.hp, game.player.c.yaw / DEG2RAD, game.flash, game.time,
            game.player.c.pos.x, game.player.c.pos.y, game.player.c.pos.z, game.boss.c.pos.x, game.boss.c.pos.y, game.boss.c.pos.z, game.cam.eye.x, game.cam.eye.y, game.cam.eye.z, game.cam.cur_dist);

    game_shutdown(&game);
    platform_shutdown(&pf);
    return 0;
}
