// Thin wrapper over SDL3 window, GPU device, and input state.
// Everything platform-specific lives behind this header.
#pragma once

#include <SDL3/SDL.h>
#include <stdbool.h>

typedef struct Input {
    // Digital
    bool quit_requested;
    bool debug_toggle, pause_toggle, step, reload, skip;
    // Movement stick / WASD, in [-1, 1]
    float move_x, move_y;
    // Camera stick / mouse delta
    float look_x, look_y;
    // Actions, edge-triggered this frame
    bool attack, parry, dodge, interact, lockon;
} Input;

typedef struct Platform {
    SDL_Window    *window;
    SDL_GPUDevice *gpu;
    SDL_Gamepad   *gamepad;
    SDL_GPUCommandBuffer *cmd;       // valid between begin_frame / end_frame
    SDL_GPUTexture       *swapchain; // valid between begin_frame / end_frame
    Uint32 swap_w, swap_h;
    Input input;
    bool want_quit;
    bool debug;
} Platform;

bool platform_init(Platform *pf, const char *title, int w, int h);
bool platform_poll(Platform *pf);       // returns false on quit
void platform_begin_frame(Platform *pf);
void platform_end_frame(Platform *pf);
void platform_shutdown(Platform *pf);
