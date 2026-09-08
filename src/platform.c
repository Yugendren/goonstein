#include "platform.h"
#include "debug.h"
#include <string.h>

static float dead(float v) { return (v > -0.15f && v < 0.15f) ? 0.0f : v; }

bool platform_init(Platform *pf, const char *title, int w, int h) {
    memset(pf, 0, sizeof *pf);

    SDL_SetAppMetadata(title, "0.0.1", "dev.hollow");
    SDL_SetHint(SDL_HINT_ASSERT, "abort");  // never block on a dialog; log and die
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) return false;

    pf->window = SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!pf->window) return false;

    // Shaders: SPIR-V on Vulkan (Linux/Deck), MSL on Metal (mac), DXIL on D3D12 (Windows).
    pf->gpu = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_DXIL,
#ifdef HOLLOW_DEBUG
        true,
#else
        false,
#endif
        NULL);
    if (!pf->gpu) return false;
    if (!SDL_ClaimWindowForGPUDevice(pf->gpu, pf->window)) return false;
    SDL_SetGPUSwapchainParameters(pf->gpu, pf->window,
                                  SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);

    SDL_Log("GPU driver: %s", SDL_GetGPUDeviceDriver(pf->gpu));
    SDL_SetWindowRelativeMouseMode(pf->window, true);

    // Pick up an already-connected gamepad; hotplug is handled in poll.
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (ids && count > 0) pf->gamepad = SDL_OpenGamepad(ids[0]);
    SDL_free(ids);
    return true;
}

void platform_clear_edges(Platform *pf) {
    Input *in = &pf->input;
    in->attack = in->parry = in->dodge = in->interact = in->debug_toggle = false;
    in->pause_toggle = in->step = in->reload = in->skip = in->lockon = false;
    in->click = in->rclick = false; in->wheel = 0;
    in->tool_pressed = in->tool_released = in->tool_rpressed = false; in->tool_wheel = 0;
    memset(in->key_down, 0, sizeof in->key_down);
    memset(in->tool_key_down, 0, sizeof in->tool_key_down);
    in->look_x = in->look_y = 0.0f;
}

bool platform_poll(Platform *pf) {
    Input *in = &pf->input;
    // Edge-triggered actions are NOT cleared here: a press that lands on a frame with no simulation
    // tick (120 Hz display, 60 Hz sim) must survive until the next tick consumes it.

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT: return false;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (pf->console_win && e.window.windowID == SDL_GetWindowID(pf->console_win)) platform_console_window(pf, false);
            else if (e.window.windowID == SDL_GetWindowID(pf->window)) return false;
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            pf->tool_focus = pf->console_win && e.window.windowID == SDL_GetWindowID(pf->console_win) && e.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (pf->console_win && e.key.windowID == SDL_GetWindowID(pf->console_win)) {
                // typed into the tool window: only that tool sees it
                if (e.key.scancode < 512) in->tool_key_down[e.key.scancode] = true;
                if (!e.key.repeat) dbg_log("[in] tool key %s", SDL_GetScancodeName(e.key.scancode));
                break;
            }
            if (e.key.scancode < 512) in->key_down[e.key.scancode] = true;   // repeats count for nudging
            if (e.key.repeat) break;
            dbg_log("[in] key %s", SDL_GetScancodeName(e.key.scancode));
            switch (e.key.scancode) {
            case SDL_SCANCODE_ESCAPE: if (!pf->editing && !pf->console) pf->want_quit = true; break;
            case SDL_SCANCODE_F1: in->debug_toggle = true; pf->debug = !pf->debug; break;
            case SDL_SCANCODE_F2: in->pause_toggle = true; break;
            case SDL_SCANCODE_F3: in->step = true; break;
            case SDL_SCANCODE_F5: in->reload = true; break;
            case SDL_SCANCODE_RETURN: in->skip = true; break;
            // Sekiro PC layout: LMB attack, RMB deflect, Shift step/sprint, MMB or Q lock-on, E interact.
            case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: case SDL_SCANCODE_SPACE: in->dodge = true; break;
            case SDL_SCANCODE_E: in->interact = true; break;
            case SDL_SCANCODE_Q: case SDL_SCANCODE_TAB: in->lockon = true; break;
            case SDL_SCANCODE_J: in->attack = true; break;   // keyboard-only fallbacks
            case SDL_SCANCODE_K: in->parry = true; break;
            default: break;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (pf->console_win && e.button.windowID == SDL_GetWindowID(pf->console_win)) {
                in->tool_mx = e.button.x; in->tool_my = e.button.y;
                if (e.button.button == SDL_BUTTON_LEFT) { in->tool_pressed = true; in->tool_down = true; }
                else if (e.button.button == SDL_BUTTON_RIGHT) { in->tool_rpressed = true; in->tool_rdown = true; }
                break;
            }
            dbg_log("[in] mouse %s down at %.0f %.0f", e.button.button == SDL_BUTTON_LEFT ? "left" : e.button.button == SDL_BUTTON_RIGHT ? "right" : "middle", e.button.x, e.button.y);
            if (e.button.button == SDL_BUTTON_LEFT) { in->attack = true; in->click = true; in->mouse_held = true; }
            else if (e.button.button == SDL_BUTTON_RIGHT) { in->parry = true; in->rclick = true; in->rmouse_held = true; }
            else if (e.button.button == SDL_BUTTON_MIDDLE) in->lockon = true;
            in->mouse_x = e.button.x; in->mouse_y = e.button.y;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (pf->console_win && e.wheel.windowID == SDL_GetWindowID(pf->console_win)) in->tool_wheel += e.wheel.y; else in->wheel += e.wheel.y;
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (pf->console_win && e.button.windowID == SDL_GetWindowID(pf->console_win)) {
                in->tool_mx = e.button.x; in->tool_my = e.button.y;
                if (e.button.button == SDL_BUTTON_LEFT) { in->tool_released = true; in->tool_down = false; }
                else if (e.button.button == SDL_BUTTON_RIGHT) in->tool_rdown = false;
                break;
            }
            dbg_log("[in] mouse %s up at %.0f %.0f", e.button.button == SDL_BUTTON_LEFT ? "left" : e.button.button == SDL_BUTTON_RIGHT ? "right" : "middle", e.button.x, e.button.y);
            if (e.button.button == SDL_BUTTON_LEFT) in->mouse_held = false;
            else if (e.button.button == SDL_BUTTON_RIGHT) in->rmouse_held = false;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (pf->console_win && e.motion.windowID == SDL_GetWindowID(pf->console_win)) { in->tool_mx = e.motion.x; in->tool_my = e.motion.y; break; }
            in->look_x += e.motion.xrel;
            in->look_y += e.motion.yrel;
            in->mouse_x = e.motion.x; in->mouse_y = e.motion.y;
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!pf->gamepad) pf->gamepad = SDL_OpenGamepad(e.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (pf->gamepad && SDL_GetGamepadID(pf->gamepad) == e.gdevice.which) {
                SDL_CloseGamepad(pf->gamepad);
                pf->gamepad = NULL;
            }
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            dbg_log("[in] pad %s", SDL_GetGamepadStringForButton((SDL_GamepadButton)e.gbutton.button));
            switch (e.gbutton.button) {
            // Sekiro pad layout: RB attack, LB deflect, B step/sprint, A interact, R3 lock-on
            case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: in->attack = true; break;
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:  in->parry = true; break;
            case SDL_GAMEPAD_BUTTON_EAST:  in->dodge = true; break;
            case SDL_GAMEPAD_BUTTON_SOUTH: in->interact = true; break;
            case SDL_GAMEPAD_BUTTON_START: in->skip = true; break;
            case SDL_GAMEPAD_BUTTON_RIGHT_STICK: in->lockon = true; break;
            case SDL_GAMEPAD_BUTTON_BACK:  in->debug_toggle = true; pf->debug = !pf->debug; break;
            default: break;
            }
            break;
        default: break;
        }
    }

    // Held movement: keyboard, overridden by stick if it's deflected.
    const bool *keys = SDL_GetKeyboardState(NULL);
    in->sprint = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    for (int i = 0; i < 512; i++) in->key_held[i] = keys[i];
    in->ctrl = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL] || keys[SDL_SCANCODE_LGUI] || keys[SDL_SCANCODE_RGUI];
    in->shift_held = in->sprint;
    if (pf->tool_focus) { memset(in->key_held, 0, sizeof in->key_held); in->sprint = false; }   // held keys belong to the focused window
    if (pf->gamepad && SDL_GetGamepadButton(pf->gamepad, SDL_GAMEPAD_BUTTON_EAST)) in->sprint = true;
    in->move_x = pf->tool_focus ? 0 : (float)(keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]);
    in->move_y = pf->tool_focus ? 0 : (float)(keys[SDL_SCANCODE_S] - keys[SDL_SCANCODE_W]);
    if (pf->gamepad) {
        float sx = dead(SDL_GetGamepadAxis(pf->gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f);
        float sy = dead(SDL_GetGamepadAxis(pf->gamepad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f);
        if (sx != 0.0f || sy != 0.0f) { in->move_x = sx; in->move_y = sy; }
        in->look_x += dead(SDL_GetGamepadAxis(pf->gamepad, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f) * 6.0f;
        in->look_y += dead(SDL_GetGamepadAxis(pf->gamepad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f) * 6.0f;
    }
    return true;
}

void platform_begin_frame(Platform *pf) {
    pf->cmd = SDL_AcquireGPUCommandBuffer(pf->gpu);
    pf->swapchain = NULL;
    if (!pf->cmd) return;
    // Blocks until a swapchain image is ready; pairs with VSYNC present mode.
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(pf->cmd, pf->window, &pf->swapchain,
                                               &pf->swap_w, &pf->swap_h)) {
        pf->swapchain = NULL;
    }
    pf->console_swap = NULL;
    if (pf->console_win && !SDL_AcquireGPUSwapchainTexture(pf->cmd, pf->console_win, &pf->console_swap, &pf->console_w, &pf->console_h)) pf->console_swap = NULL;
}

void platform_end_frame(Platform *pf) {
    if (pf->cmd) SDL_SubmitGPUCommandBuffer(pf->cmd);
    pf->cmd = NULL;
    pf->swapchain = NULL;
}

void platform_tool_window(Platform *pf, bool open, int w, int h, const char *title) {
    if (open && pf->console_win) {
        if (pf->tool_w != w || pf->tool_h != h) SDL_SetWindowSize(pf->console_win, w, h);
        SDL_SetWindowTitle(pf->console_win, title);
        pf->tool_w = w; pf->tool_h = h; pf->console = true;
        return;
    }
    pf->tool_w = w; pf->tool_h = h;
    if (open && !pf->console_win && !SDL_getenv("HOLLOW_CONSOLE_INLINE")) {
        pf->console_win = SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (pf->console_win && !SDL_ClaimWindowForGPUDevice(pf->gpu, pf->console_win)) { SDL_DestroyWindow(pf->console_win); pf->console_win = NULL; }
        if (pf->console_win) SDL_SetGPUSwapchainParameters(pf->gpu, pf->console_win, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);
        if (!pf->console_win) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "tool window failed: %s (falling back to in-game panel)", SDL_GetError());
        SDL_RaiseWindow(pf->window);
    } else if (!open && pf->console_win) {
        SDL_WaitForGPUIdle(pf->gpu);
        SDL_ReleaseWindowFromGPUDevice(pf->gpu, pf->console_win);
        SDL_DestroyWindow(pf->console_win); pf->console_win = NULL; pf->console_swap = NULL;
    }
    pf->console = open;
}

void platform_console_window(Platform *pf, bool open) { platform_tool_window(pf, open, pf->tool_w > 0 ? pf->tool_w : 720, pf->tool_h > 0 ? pf->tool_h : 820, "hollow debugger"); }

void platform_shutdown(Platform *pf) {
    if (pf->console_win) platform_console_window(pf, false);
    if (pf->gamepad) SDL_CloseGamepad(pf->gamepad);
    if (pf->gpu && pf->window) SDL_ReleaseWindowFromGPUDevice(pf->gpu, pf->window);
    if (pf->gpu) SDL_DestroyGPUDevice(pf->gpu);
    if (pf->window) SDL_DestroyWindow(pf->window);
    SDL_Quit();
}

void platform_set_cursor(Platform *pf, bool free_cursor) {
    SDL_SetWindowRelativeMouseMode(pf->window, !free_cursor);
    if (free_cursor) SDL_ShowCursor(); else SDL_HideCursor();
}

void platform_mouse_ui(const Platform *pf, int iw, int ih, float *ux, float *uy) {
    int ww = 1, wh = 1;
    SDL_GetWindowSize(pf->window, &ww, &wh);
    float ta = (float)iw / (float)ih, sw = (float)ww, sh = (float)wh;
    float vw = sw, vh = sw / ta; if (vh > sh) { vh = sh; vw = sh * ta; }
    float ox = (sw - vw) * 0.5f, oy = (sh - vh) * 0.5f;
    *ux = (pf->input.mouse_x - ox) / vw * (float)iw;
    *uy = (pf->input.mouse_y - oy) / vh * (float)ih;
}
