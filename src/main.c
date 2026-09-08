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

#define TICK_HZ 60
#define TICK_DT (1.0 / TICK_HZ)
#define MAX_FRAME_DT 0.25  // clamp after a stall so we don't spiral

int main(int argc, char **argv) {
    // --frames N      exit after N frames (headless checks, CI)
    // --screenshot P  write the internal frame to P before exiting
    // --start S       begin in state S: explore (default), fight, end
    // --bot           let a simple bot play the fight (with --start fight)
    int max_frames = -1; const char *shot = NULL; const char *start = NULL; bool bot = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) max_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--start") && i + 1 < argc) start = argv[++i];
        else if (!strcmp(argv[i], "--bot")) bot = true;
    }

    Platform pf;
    if (!platform_init(&pf, "hollow", 1280, 800)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "platform_init failed: %s", SDL_GetError());
        return 1;
    }

    Game game;
    game_init(&game);
    if (!game_init_gfx(&game, &pf)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "gfx init failed: %s", SDL_GetError());
        return 1;
    }
    if (start) game_start_at(&game, start);
    game.bot = bot;

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
            accumulator -= TICK_DT;
        }

        double alpha = accumulator / TICK_DT;  // for render interpolation
        platform_begin_frame(&pf);
        game_render(&game, &pf, (float)alpha);
        platform_end_frame(&pf);
        if (max_frames >= 0 && --max_frames == 0) running = false;
    }
    if (shot) game_screenshot(&game, shot);
    SDL_Log("stats: state=%d parries=%u hits_taken=%u deaths=%u boss_hp=%.0f player_hp=%.0f player_yaw=%.0f flash=%.2f t=%.3f",
            game.state, game.parries, game.hits_taken, game.deaths, game.boss.c.hp, game.player.c.hp, game.player.c.yaw / DEG2RAD, game.flash, game.time);

    game_shutdown(&game);
    platform_shutdown(&pf);
    return 0;
}
