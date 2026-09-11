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
    // Mouse look delta for THIS RENDERED FRAME, in mouse pixels, and the gamepad's look stick as a
    // rate in -1..1. look_x/look_y are cleared per frame (platform_clear_frame_edges), not per tick:
    // the view turns at the frame rate, so a delta that waited for a tick would arrive late and in
    // clumps of two or three frames, which is what a stepping view is made of.
    float look_x, look_y;
    float look_stick_x, look_stick_y;
    // Actions, edge-triggered this frame
    bool attack, parry, dodge, interact, lockon;
    bool jump;     // edge: Space. The dodge stayed on Shift; a co-op game needs the jump more.
    bool sprint;   // held
    bool crouch;   // held: Ctrl. Lowers the eye and halves the speed.
    // --- voice --- push to talk: V on the keyboard, left bumper on a pad. Held, not edge
    // triggered, and cleared while a tool window has focus so typing a `v` never opens the mic.
    bool voice_ptt;
    // Mouse, for menus and cards
    float mouse_x, mouse_y;          // window points
    bool  click, rclick;             // edge-triggered this frame
    bool  mouse_held, rmouse_held;
    float wheel;                     // scroll this frame
    // Generic keys for tools: edge-triggered presses and held state by scancode
    bool  key_down[512], key_held[512];
    bool  tool_key_down[512];         // key presses that landed in the tool window, cleared per tick (game logic)
    bool  tool_key_frame[512];        // the same presses, cleared per rendered frame (tool panels run in render)
    bool  ctrl, shift_held;
    float parry_age, click_age;       // seconds since the latest parry / click press (sub-tick judgement)
    // Mouse in the tool window (editor / debugger), in that window's points
    float tool_mx, tool_my; bool tool_down, tool_pressed, tool_released; float tool_wheel;
    bool tool_rdown, tool_rpressed;   // right button in the tool window (sprite editor colour pick / erase)
    // --- menu --- text typed this tick (UTF-8, from SDL_EVENT_TEXT_INPUT); cleared per tick
    char  text[32]; int ntext;
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
    bool console;    // the F4 debugger
    bool editing;    // an editor owns the game window: Esc deselects instead of quitting
    SDL_Window *console_win; SDL_GPUTexture *console_swap; Uint32 console_w, console_h;   // separate tool window (debugger, editors)
    int tool_w, tool_h;   // its logical size in points (the UI coordinate space)
    SDL_Rect saved_game_rect; bool game_rect_saved;   // game window placement before the tool window tiled it
    int fps_cap; bool vsync;   // frame cap (0 = display rate) and vsync; platform_end_frame enforces the cap
    SDL_GPUPresentMode present_mode;   // what the swapchain actually got, not what was asked for
    Uint64 next_frame_ns;
    Uint64 parry_ns, click_ns;
    bool tool_focus;      // the tool window has keyboard focus: held keys and movement do not reach the game
    bool text_input;   // --- menu --- a text field owns the keyboard: no movement, no tool keys
} Platform;

// Packaged (HOLLOW_PORTABLE) builds: make the executable's own directory the working directory so
// the relative "assets" path resolves. No-op in a dev build. platform_init calls it; call it first
// from main() as well if anything is loaded before the window exists (settings.txt is, today).
void platform_use_base_dir(void);
bool platform_init(Platform *pf, const char *title, int w, int h);
bool platform_poll(Platform *pf);       // returns false on quit. Edge inputs accumulate until platform_clear_edges.
void platform_clear_edges(Platform *pf);         // per simulation tick
void platform_clear_frame_edges(Platform *pf);   // per rendered frame: tool-window mouse and key edges the panels consume // call after a simulation tick has consumed the input
void platform_console_window(Platform *pf, bool open);   // open or close the tool window at its current size
// Open (or resize) the tool window with a logical size and title.
// Open (tiled beside the game window: `share` is the fraction of the screen the tool takes) or close the tool window.
void platform_tool_window(Platform *pf, bool open, int w, int h, const char *title);
void platform_tool_window_share(Platform *pf, bool open, float share, const char *title);
void platform_begin_frame(Platform *pf);
void platform_end_frame(Platform *pf);
void platform_set_vsync(Platform *pf, bool on);   // applies immediately
// The present mode the swapchain is really running in, as a word: "vsync", "mailbox", "immediate".
// Asking for no vsync is a request, not a promise: a driver that has neither IMMEDIATE nor MAILBOX
// keeps VSYNC and every frame time measured on it is the display's, not the renderer's. Print this.
const char *platform_present_mode_name(const Platform *pf);
void platform_shutdown(Platform *pf);
// Show a free cursor (menus, cards) or capture it for camera look.
void platform_set_cursor(Platform *pf, bool free_cursor);
// --- menu --- Start or stop SDL text input for the game window. While it is on, typed characters
// arrive in Input.text and no key reaches gameplay except the editing keys (Esc, Return, Backspace,
// Tab, arrows, Delete), so typing a name cannot walk the player or open a tool window.
void platform_text_input(Platform *pf, bool on);
// Mouse position in internal-resolution UI pixels, accounting for letterboxing.
void platform_mouse_ui(const Platform *pf, int iw, int ih, float *ux, float *uy);
