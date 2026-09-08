#include "platform.h"
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
    memset(in->key_down, 0, sizeof in->key_down);
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
        case SDL_EVENT_KEY_DOWN:
            if (e.key.scancode < 512) in->key_down[e.key.scancode] = true;   // repeats count for nudging
            if (e.key.repeat) break;
            switch (e.key.scancode) {
            case SDL_SCANCODE_ESCAPE: pf->want_quit = true; break;
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
            if (e.button.button == SDL_BUTTON_LEFT) { in->attack = true; in->click = true; in->mouse_held = true; }
            else if (e.button.button == SDL_BUTTON_RIGHT) { in->parry = true; in->rclick = true; in->rmouse_held = true; }
            else if (e.button.button == SDL_BUTTON_MIDDLE) in->lockon = true;
            in->mouse_x = e.button.x; in->mouse_y = e.button.y;
            break;
        case SDL_EVENT_MOUSE_WHEEL: in->wheel += e.wheel.y; break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) in->mouse_held = false;
            else if (e.button.button == SDL_BUTTON_RIGHT) in->rmouse_held = false;
            break;
        case SDL_EVENT_MOUSE_MOTION:
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
    if (pf->gamepad && SDL_GetGamepadButton(pf->gamepad, SDL_GAMEPAD_BUTTON_EAST)) in->sprint = true;
    in->move_x = (float)(keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]);
    in->move_y = (float)(keys[SDL_SCANCODE_S] - keys[SDL_SCANCODE_W]);
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
}

void platform_end_frame(Platform *pf) {
    if (pf->cmd) SDL_SubmitGPUCommandBuffer(pf->cmd);
    pf->cmd = NULL;
    pf->swapchain = NULL;
}

void platform_shutdown(Platform *pf) {
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
