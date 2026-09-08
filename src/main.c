// hollow - entry point.
// Fixed-timestep simulation, variable-rate rendering, SDL3 GPU backend.

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdbool.h>
#include <stdio.h>

#include "platform.h"
#include "game.h"

#define TICK_HZ 60
#define TICK_DT (1.0 / TICK_HZ)
#define MAX_FRAME_DT 0.25  // clamp after a stall so we don't spiral

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    Platform pf;
    if (!platform_init(&pf, "hollow", 1280, 800)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "platform_init failed: %s", SDL_GetError());
        return 1;
    }

    Game game;
    game_init(&game);

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
    }

    game_shutdown(&game);
    platform_shutdown(&pf);
    return 0;
}
