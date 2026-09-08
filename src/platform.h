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
    bool sprint;   // held
    // Mouse, for menus and cards
    float mouse_x, mouse_y;          // window points
    bool  click, rclick;             // edge-triggered this frame
    bool  mouse_held, rmouse_held;
    float wheel;                     // scroll this frame
    // Generic keys for tools: edge-triggered presses and held state by scancode
    bool  key_down[512], key_held[512];
    bool  tool_key_down[512];         // key presses that landed in the tool window (its editor gets these, the game does not)
    bool  ctrl, shift_held;
    // Mouse in the tool window (editor / debugger), in that window's points
    float tool_mx, tool_my; bool tool_down, tool_pressed, tool_released; float tool_wheel;
    bool tool_rdown, tool_rpressed;   // right button in the tool window (sprite editor colour pick / erase)
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
    bool console;    // the \ debugger
    bool editing;    // an editor owns the game window: Esc deselects instead of quitting
    SDL_Window *console_win; SDL_GPUTexture *console_swap; Uint32 console_w, console_h;   // separate tool window (debugger, editors)
    int tool_w, tool_h;   // its logical size in points (the UI coordinate space)
    bool tool_focus;      // the tool window has keyboard focus: held keys and movement do not reach the game
} Platform;

bool platform_init(Platform *pf, const char *title, int w, int h);
bool platform_poll(Platform *pf);       // returns false on quit. Edge inputs accumulate until platform_clear_edges.
void platform_clear_edges(Platform *pf); // call after a simulation tick has consumed the input
void platform_console_window(Platform *pf, bool open);   // open or close the tool window at its current size
// Open (or resize) the tool window with a logical size and title.
void platform_tool_window(Platform *pf, bool open, int w, int h, const char *title);
void platform_begin_frame(Platform *pf);
void platform_end_frame(Platform *pf);
void platform_shutdown(Platform *pf);
// Show a free cursor (menus, cards) or capture it for camera look.
void platform_set_cursor(Platform *pf, bool free_cursor);
// Mouse position in internal-resolution UI pixels, accounting for letterboxing.
void platform_mouse_ui(const Platform *pf, int iw, int ih, float *ux, float *uy);
