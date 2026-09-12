#include "platform.h"
#include "debug.h"
#include "prof.h"
#include <stdio.h>    // sscanf: Apple's headers pull this in for free, glibc does not
#include <string.h>
#include <math.h>    // fabs/fabsf: the cap-vs-refresh divisor test

// Packaged builds (-DHOLLOW_PORTABLE=ON) compile HOLLOW_ASSET_DIR as the relative path "assets",
// so the process has to stand next to it. Dev builds bake in an absolute source path and this is
// a no-op, which keeps relative --screenshot paths resolving against the shell's cwd as before.
#ifdef HOLLOW_PORTABLE
#ifdef _WIN32
#include <direct.h>
#define hollow_chdir _chdir
#else
#include <unistd.h>
#define hollow_chdir chdir
#endif
#endif

void platform_use_base_dir(void) {
#ifdef HOLLOW_PORTABLE
    static bool done = false;
    if (done) return;
    done = true;
    const char *base = SDL_GetBasePath();   // the directory holding the executable, with a separator
    if (!base || !*base) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "no base path: assets must be in the working directory"); return; }
    if (hollow_chdir(base) != 0) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "could not chdir to %s", base);
#endif
}

static float dead(float v) { return (v > -0.15f && v < 0.15f) ? 0.0f : v; }

bool platform_init(Platform *pf, const char *title, int w, int h) {
    memset(pf, 0, sizeof *pf);
    platform_use_base_dir();   // before anything reads an asset

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
    // Read no_present before claiming: on a Windows box reached over SSH with no interactive
    // desktop, SDL_ClaimWindowForGPUDevice fails hard (0x887A0022, "A resource is not available
    // at the time of the call") even though window creation succeeded. That is exactly where a
    // headless bench wants to run, so HOLLOW_NOPRESENT skips the claim (and the vsync negotiation,
    // which also needs a claimed window) entirely rather than trying it and dying.
    pf->no_present = SDL_getenv("HOLLOW_NOPRESENT") != NULL;
    if (!pf->no_present) {
        if (!SDL_ClaimWindowForGPUDevice(pf->gpu, pf->window)) return false;
        platform_set_vsync(pf, SDL_getenv("HOLLOW_NOVSYNC") == NULL);
    } else {
        pf->present_mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
        pf->vsync = false;
        SDL_Log("present mode: none (HOLLOW_NOPRESENT: drawing off screen, two frames in flight on a fence)");
    }

    SDL_Log("GPU driver: %s", SDL_GetGPUDeviceDriver(pf->gpu));
    // The refresh rate is not a curiosity, it is the clock every frame is eventually quantised to.
    // Read it here so the cap can be checked against it before the first frame. A display that
    // reports 0 (or no display at all, which is every headless run) leaves refresh_hz at 0 and
    // everything below turns into a no-op.
    { SDL_DisplayID d = SDL_GetDisplayForWindow(pf->window);
      const SDL_DisplayMode *m = d ? SDL_GetCurrentDisplayMode(d) : NULL;
      pf->refresh_hz = m && m->refresh_rate > 1.0f ? (int)(m->refresh_rate + 0.5f) : 0;
      if (pf->refresh_hz) { SDL_Log("display: %d Hz", pf->refresh_hz); dbg_log("display: %d Hz", pf->refresh_hz); }
      else { SDL_Log("display: refresh rate unknown"); dbg_log("display: refresh rate unknown"); } }
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
    in->jump = false;
    in->click = in->rclick = false; in->wheel = 0;
    memset(in->key_down, 0, sizeof in->key_down);
    memset(in->tool_key_down, 0, sizeof in->tool_key_down);
    // look_x/look_y are NOT cleared here any more: the camera consumes them once per frame, which
    // is the whole point of frame-rate mouse look. See platform_clear_frame_edges.
    in->text[0] = 0; in->ntext = 0;   // --- menu ---
}

// Tool panels run in the render step, which happens every frame, so their edges live for exactly
// one frame. (Clearing them per tick lost every click that landed on a frame that ticked before
// it rendered: the "click twice" bug.)
void platform_clear_frame_edges(Platform *pf) {
    Input *in = &pf->input;
    in->tool_pressed = in->tool_released = in->tool_rpressed = false; in->tool_wheel = 0;
    in->look_x = in->look_y = 0.0f;   // the view already turned by this much: one frame, one delta
    memset(in->tool_key_frame, 0, sizeof in->tool_key_frame);
}

bool platform_poll(Platform *pf) {
    Input *in = &pf->input;
    // Edge-triggered actions are NOT cleared here: a press that lands on a frame with no simulation
    // tick (120 Hz display, 60 Hz sim) must survive until the next tick consumes it.

    prof_begin(PROF_IN_EVENTS);
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT: return false;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (pf->console_win && e.window.windowID == SDL_GetWindowID(pf->console_win)) platform_console_window(pf, false);
            else if (e.window.windowID == SDL_GetWindowID(pf->window)) return false;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            if (pf->console_win && e.window.windowID == SDL_GetWindowID(pf->console_win)) { pf->tool_w = e.window.data1; pf->tool_h = e.window.data2; }
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            pf->tool_focus = pf->console_win && e.window.windowID == SDL_GetWindowID(pf->console_win) && e.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (pf->console_win && e.key.windowID == SDL_GetWindowID(pf->console_win)) {
                // typed into the tool window: only that tool sees it
                if (e.key.scancode < 512) { in->tool_key_down[e.key.scancode] = true; in->tool_key_frame[e.key.scancode] = true; }
                if (!e.key.repeat) dbg_log("[in] tool key %s", SDL_GetScancodeName(e.key.scancode));
                break;
            }
            if (pf->text_input) {
                // --- menu --- a text field owns the keyboard: only the editing keys reach the game
                switch (e.key.scancode) {
                case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER:
                case SDL_SCANCODE_BACKSPACE: case SDL_SCANCODE_DELETE: case SDL_SCANCODE_TAB:
                case SDL_SCANCODE_LEFT: case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_UP: case SDL_SCANCODE_DOWN:
                    in->key_down[e.key.scancode] = true;
                    break;
                default: break;
                }
                break;
            }
            if (e.key.scancode < 512) in->key_down[e.key.scancode] = true;   // repeats count for nudging
            if (e.key.repeat) break;
            dbg_log("[in] key %s", SDL_GetScancodeName(e.key.scancode));
            switch (e.key.scancode) {
            // --- menu --- Esc is an ordinary key now: the in-game menu owns quitting (src/menu.c)
            // The function keys are the whole tool row: F1 overlay, F2 world editor, F3 character
            // builder, F4 debugger, F5 reload, F6 pause, F7 look, F8 snapshot, F9 step. The ones
            // that open a tool are read straight out of key_down by game.c.
            case SDL_SCANCODE_F1: in->debug_toggle = true; pf->debug = !pf->debug; break;
            case SDL_SCANCODE_F5: in->reload = true; break;
            case SDL_SCANCODE_F6: in->pause_toggle = true; break;
            case SDL_SCANCODE_F9: in->step = true; break;
            case SDL_SCANCODE_RETURN: in->skip = true; break;
            // Sekiro PC layout: LMB attack, RMB deflect, Shift step/sprint, MMB or Q lock-on, E interact.
            case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT:
                in->dodge = true;
                if (pf->sprint_toggle) pf->sprint_latch = !pf->sprint_latch;   // toggle mode: the press edge flips it, not the hold
                break;
            case SDL_SCANCODE_SPACE: in->jump = true; break;
            case SDL_SCANCODE_E: in->interact = true; break;
            case SDL_SCANCODE_Q: case SDL_SCANCODE_TAB: in->lockon = true; break;
            case SDL_SCANCODE_J: in->attack = true; break;   // keyboard-only fallbacks
            case SDL_SCANCODE_K: in->parry = true; pf->parry_ns = e.key.timestamp; break;
            default: break;
            }
            break;
        // --- menu --- typed characters while a text field owns input (see platform_text_input)
        case SDL_EVENT_TEXT_INPUT: {
            if (pf->console_win && e.text.windowID == SDL_GetWindowID(pf->console_win)) break;   // tool window has its own key handling
            size_t len = strlen(in->text);
            size_t room = sizeof in->text - 1 - len;
            size_t add = strlen(e.text.text);
            if (add > room) add = room;
            if (add > 0) { memcpy(in->text + len, e.text.text, add); in->text[len + add] = 0; }
            in->ntext = (int)strlen(in->text);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (pf->console_win && e.button.windowID == SDL_GetWindowID(pf->console_win)) {
                in->tool_mx = e.button.x; in->tool_my = e.button.y;
                if (e.button.button == SDL_BUTTON_LEFT) { in->tool_pressed = true; in->tool_down = true; }
                else if (e.button.button == SDL_BUTTON_RIGHT) { in->tool_rpressed = true; in->tool_rdown = true; }
                break;
            }
            dbg_log("[in] mouse %s down at %.0f %.0f", e.button.button == SDL_BUTTON_LEFT ? "left" : e.button.button == SDL_BUTTON_RIGHT ? "right" : "middle", e.button.x, e.button.y);
            if (e.button.button == SDL_BUTTON_LEFT) { in->attack = true; in->click = true; in->mouse_held = true; pf->click_ns = e.button.timestamp; }
            else if (e.button.button == SDL_BUTTON_RIGHT) { in->parry = true; in->rclick = true; in->rmouse_held = true; pf->parry_ns = e.button.timestamp; }
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
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:  in->parry = true; pf->parry_ns = e.gbutton.timestamp; break;
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

    prof_end(PROF_IN_EVENTS);
    prof_begin(PROF_IN_STATE);
    // How long ago the latest parry / click press happened, so the sim can judge it between ticks.
    { Uint64 now = SDL_GetTicksNS(); in->parry_age = pf->parry_ns && now > pf->parry_ns ? (float)((now - pf->parry_ns) / 1e9) : 0; in->click_age = pf->click_ns && now > pf->click_ns ? (float)((now - pf->click_ns) / 1e9) : 0; }
    // Held movement: keyboard, overridden by stick if it's deflected.
    const bool *keys = SDL_GetKeyboardState(NULL);
    bool shift_down = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    if (pf->sprint_toggle) {
        // Toggle mode: the latch was flipped on the press edge above; it just needs clearing when
        // there is nothing left to sprint at, so it cannot survive letting go of every movement key.
        bool moving = !(pf->tool_focus || pf->text_input) &&
                      (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_D]);
        if (!moving) pf->sprint_latch = false;
        in->sprint = pf->sprint_latch;
    } else in->sprint = shift_down;   // hold mode: exactly as before
    in->jump_held = (pf->tool_focus || pf->text_input) ? false : keys[SDL_SCANCODE_SPACE];
    for (int i = 0; i < 512; i++) in->key_held[i] = keys[i];
    in->ctrl = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL] || keys[SDL_SCANCODE_LGUI] || keys[SDL_SCANCODE_RGUI];
    // Crouch is Ctrl alone: the tool shortcuts all take Ctrl WITH a letter, and Command is left out
    // of it so cmd-tabbing away from the game does not leave you squatting.
    in->crouch = (pf->tool_focus || pf->text_input) ? false : (keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_RCTRL]);
    in->shift_held = shift_down;   // the raw key, not the sprint latch -- toggle mode still needs to know Shift itself
    if (pf->tool_focus || pf->text_input) { memset(in->key_held, 0, sizeof in->key_held); in->sprint = false; }   // held keys belong to the focused window // --- menu --- or a text field
    if (pf->gamepad && SDL_GetGamepadButton(pf->gamepad, SDL_GAMEPAD_BUTTON_EAST)) in->sprint = true;
    // --- voice --- push to talk. On a pad the left bumper is also the old fight's deflect; the
    // co-op game has no deflect, so they can share the button.
    in->voice_ptt = in->key_held[SDL_SCANCODE_V] ||
                    (pf->gamepad && SDL_GetGamepadButton(pf->gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
    in->move_x = (pf->tool_focus || pf->text_input) ? 0 : (float)(keys[SDL_SCANCODE_D] - keys[SDL_SCANCODE_A]);
    in->move_y = (pf->tool_focus || pf->text_input) ? 0 : (float)(keys[SDL_SCANCODE_S] - keys[SDL_SCANCODE_W]);
    if (pf->gamepad) {
        float sx = dead(SDL_GetGamepadAxis(pf->gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f);
        float sy = dead(SDL_GetGamepadAxis(pf->gamepad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f);
        if (sx != 0.0f || sy != 0.0f) { in->move_x = sx; in->move_y = sy; }
        in->look_stick_x = dead(SDL_GetGamepadAxis(pf->gamepad, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f);
        in->look_stick_y = dead(SDL_GetGamepadAxis(pf->gamepad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f);
    }
    if (pf->tool_focus || pf->text_input) in->look_stick_x = in->look_stick_y = 0;   // the look stick belongs to the focused window too
    // HOLLOW_AUTOLOOK=PIXELS_PER_SECOND: a steady pan injected where a real mouse's motion arrives,
    // so a headless run can measure how evenly the view turns. Nothing in a played game touches it.
    if (SDL_getenv("HOLLOW_AUTOLOOK")) {
        static Uint64 last = 0; Uint64 now = SDL_GetTicksNS();
        float dt = last ? (float)((now - last) * 1e-9) : 0; last = now;
        if (dt > 0.5f) dt = 0;
        in->look_x += (float)SDL_atof(SDL_getenv("HOLLOW_AUTOLOOK")) * dt;
    }
    prof_end(PROF_IN_STATE);
    return true;
}

// HOLLOW_NOPRESENT=1 -- what it is for, and why "no vsync" was not enough.
//
// With IMMEDIATE present mode on this Mac, an EMPTY level still costs 5.7 ms a frame, and 5.1 of
// those are spent inside SDL_WaitAndAcquireGPUSwapchainTexture. That is not the renderer and it is
// not the GPU: it is the window server handing out drawables at its own pace. It puts a floor of
// about 120 frames a second under every measurement, which is fine for a game and useless for a
// benchmark -- below the floor you are timing the compositor, and a change that makes the renderer
// twice as fast reads as no change at all.
//
// So: acquire no swapchain image at all, and draw the whole frame into the off-screen targets it
// was always drawn into (gfx_end blits into a stand-in of the swapchain's size and format, so even
// the final upscale is still paid for). The CPU is held two frames ahead of the GPU with a fence,
// which is exactly the backpressure the swapchain used to provide, so the number that comes out is
// the renderer's throughput rather than how fast the CPU can fill a command buffer.
//
// Nothing appears on screen while this is on. It is for --bench and for measuring, never for play.
void platform_begin_frame(Platform *pf) {
    pf->cmd = SDL_AcquireGPUCommandBuffer(pf->gpu);
    pf->swapchain = NULL;
    if (!pf->cmd) return;
    if (pf->no_present) {
        SDL_GetWindowSizeInPixels(pf->window, (int *)&pf->swap_w, (int *)&pf->swap_h);
        unsigned slot = pf->inflight_i % 2;
        if (pf->inflight[slot]) {
            SDL_WaitForGPUFences(pf->gpu, true, &pf->inflight[slot], 1);
            SDL_ReleaseGPUFence(pf->gpu, pf->inflight[slot]);
            pf->inflight[slot] = NULL;
        }
        pf->console_swap = NULL;
        return;
    }
    // Blocks until a swapchain image is ready; pairs with VSYNC present mode.
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(pf->cmd, pf->window, &pf->swapchain,
                                               &pf->swap_w, &pf->swap_h)) {
        pf->swapchain = NULL;
    }
    pf->console_swap = NULL;
    if (pf->console_win && !SDL_AcquireGPUSwapchainTexture(pf->cmd, pf->console_win, &pf->console_swap, &pf->console_w, &pf->console_h)) pf->console_swap = NULL;
}

const char *platform_present_mode_name(const Platform *pf) {
    switch (pf->present_mode) {
    case SDL_GPU_PRESENTMODE_IMMEDIATE: return "immediate";
    case SDL_GPU_PRESENTMODE_MAILBOX:   return "mailbox";
    default:                            return "vsync";
    }
}

// Turning vsync off is a request with three possible answers, and which one the driver gives
// decides whether any frame-time number from this process means anything.
//
//   IMMEDIATE  present the moment the frame is done. Tears. The only mode that measures the
//              renderer rather than the display.
//   MAILBOX    render uncapped, show the newest finished frame at the refresh. No tearing, and
//              the CPU/GPU still run flat out, so frame times are still the renderer's.
//   VSYNC      block on the display. Every number becomes a multiple of the refresh period.
//
// Metal on this Mac reports IMMEDIATE as supported and then refuses SDL_SetGPUSwapchainParameters
// for it on some window configurations, which is how `HOLLOW_NOVSYNC=1` used to come back silently
// capped: the call failed, nobody looked at the return value, and 9.98 ms medians were the 100 Hz
// panel talking. So try the modes in order, check what each call returns, and keep the first that
// takes. The mode that won is logged and available to the profiler and the benchmark.
void platform_set_vsync(Platform *pf, bool on) {
    // --bench calls this unconditionally after platform_init (main.c), even in HOLLOW_NOPRESENT
    // mode where the window was never claimed. Without this guard SDL_WindowSupportsGPUPresentMode
    // fails ("window has not been claimed!"), the fallback branch below overwrites present_mode
    // with VSYNC, and the bench JSON stops saying "immediate". platform_init already set
    // present_mode/vsync correctly for no_present, so there is nothing to renegotiate here.
    if (pf->no_present) return;
    pf->vsync = on;
    SDL_GPUPresentMode order[3]; int n = 0;
    if (on) order[n++] = SDL_GPU_PRESENTMODE_VSYNC;
    else { order[n++] = SDL_GPU_PRESENTMODE_IMMEDIATE; order[n++] = SDL_GPU_PRESENTMODE_MAILBOX; order[n++] = SDL_GPU_PRESENTMODE_VSYNC; }
    for (int i = 0; i < n; i++) {
        if (!SDL_WindowSupportsGPUPresentMode(pf->gpu, pf->window, order[i])) continue;
        if (!SDL_SetGPUSwapchainParameters(pf->gpu, pf->window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, order[i])) continue;
        pf->present_mode = order[i];
        SDL_Log("present mode: %s (asked for %s)", platform_present_mode_name(pf), on ? "vsync" : "no vsync");
        return;
    }
    // Nothing took, not even VSYNC: whatever the swapchain was created with is what we have.
    pf->present_mode = SDL_GPU_PRESENTMODE_VSYNC;
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "present mode: no requested mode accepted (%s); frame times are the display's, not the renderer's", SDL_GetError());
}

int platform_refresh_hz(const Platform *pf) { return pf->refresh_hz; }

int platform_fps_options(const Platform *pf, int *out, int max) {
    int n = 0;
    if (max > 0) out[n++] = 0;   // "display": no cap of our own, vsync does the pacing
    int r = pf->refresh_hz;
    if (!r) { const int fallback[] = { 30, 60, 90, 120, 144, 240 };   // no display to ask: the old fixed list
              for (int i = 0; i < 6 && n < max; i++) out[n++] = fallback[i];
              return n; }
    // Divisors only, and only down to a quarter of the refresh: below 30-ish fps the cadence stops
    // being the problem. Written descending after the 0, so the fastest smooth option is first.
    for (int k = 1; k <= 4 && n < max; k++) {
        int cap = (int)((double)r / k + 0.5);
        if (cap < 24) break;
        bool dup = false; for (int i = 1; i < n; i++) if (out[i] == cap) dup = true;
        if (!dup) out[n++] = cap;
    }
    return n;
}

int platform_snap_fps_cap(const Platform *pf, int cap) {
    // Off the display's clock there is nothing to beat against: an uncapped run, a torn present, or
    // a headless one. HOLLOW_NOSNAP is for measurement runs that deliberately want an odd cap.
    if (cap <= 0 || !pf->vsync || !pf->refresh_hz || pf->no_present || SDL_getenv("HOLLOW_NOSNAP")) return cap;
    double r = pf->refresh_hz;
    double k = r / cap;
    int kn = (int)(k + 0.5); if (kn < 1) kn = 1;
    int snapped = (int)(r / kn + 0.5);
    // Within half a per cent of a divisor already (144 asked for on a 144.0 Hz panel, 60 on 59.94)
    // is a divisor; leave the player's number alone so the menu does not appear to edit itself.
    if (cap > 0 && fabs(k - kn) / kn < 0.005) return cap;
    return snapped;
}

void platform_set_fps_cap(Platform *pf, int cap) {
    int snapped = platform_snap_fps_cap(pf, cap);
    if (snapped != cap)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "pace: a %d fps cap on a %d Hz display is %.2f refreshes a frame -- every frame is held for a "
                    "different number of them and the world judders while the frame time stays flat. Using %d (%d "
                    "refresh%s a frame) instead; set vsync 0 in settings.txt to keep the odd number.",
                    cap, pf->refresh_hz, (double)pf->refresh_hz / cap, snapped,
                    (int)((double)pf->refresh_hz / snapped + 0.5), (int)((double)pf->refresh_hz / snapped + 0.5) == 1 ? "" : "es");
    else if (cap > 0 && pf->refresh_hz) {
        SDL_Log("pace: %d fps cap on a %d Hz display, %.2f refreshes a frame", cap, pf->refresh_hz, (double)pf->refresh_hz / cap);
        dbg_log("pace: %d fps cap on a %d Hz display, %.2f refreshes a frame", cap, pf->refresh_hz, (double)pf->refresh_hz / cap);
    } else if (cap == 0 && pf->refresh_hz)
        dbg_log("pace: no cap, %d Hz display%s", pf->refresh_hz, pf->vsync ? " (vsync paces)" : " (no vsync)");
    pf->fps_cap = snapped;
    pf->next_frame_ns = 0;
}

void platform_end_frame(Platform *pf) {
    if (pf->cmd && pf->no_present) {
        pf->inflight[pf->inflight_i % 2] = SDL_SubmitGPUCommandBufferAndAcquireFence(pf->cmd);
        pf->inflight_i++;
    } else if (pf->cmd) SDL_SubmitGPUCommandBuffer(pf->cmd);
    pf->cmd = NULL;
    pf->swapchain = NULL;
    // The frame cap. Two things matter and neither is the average: the period has to be held to
    // well inside a millisecond (a cap whose own jitter is 2 ms is a stutter generator), and the
    // deadline has to be absolute rather than "sleep 11 ms from here", or every frame's sleep
    // overshoot accumulates into a drifting frame rate.
    //
    // A cap that matches the refresh is left to vsync: capping at 144 on a 144 Hz panel means two
    // pacers fighting, and the one that loses shows up as a dropped frame every few seconds.
    // "vsync is pacing us" means the frames are really going to a display that really blocks on it:
    // a HOLLOW_NOPRESENT run has pf->vsync set and no display in the loop at all, and handing it the
    // pacing means no pacing, which is a 400 fps measurement run that was asked to be a 144 one.
    bool vsync_paces = pf->vsync && !pf->no_present && pf->present_mode == SDL_GPU_PRESENTMODE_VSYNC
                       && pf->refresh_hz && pf->fps_cap > 0
                       && fabsf((float)pf->fps_cap - (float)pf->refresh_hz) < (float)pf->refresh_hz * 0.02f;
    if (pf->fps_cap > 0 && !vsync_paces) {
        prof_begin(PROF_CAP_SLEEP);
        Uint64 period = SDL_NS_PER_SECOND / (Uint64)pf->fps_cap, now = SDL_GetTicksNS();
        if (pf->next_frame_ns == 0 || now > pf->next_frame_ns + period * 4) pf->next_frame_ns = now;
        pf->next_frame_ns += period;
        // Sleep for everything but the last millisecond, then spin. The OS will not wake a thread
        // to better than a few hundred microseconds and will happily be late by more; the spin is
        // the only way to land on the deadline. One millisecond of spin at 144 fps is 14% of one
        // core, which is the price of a frame time that does not wander, and it is only paid when
        // the cap is doing the pacing rather than the display.
        const Uint64 SPIN_NS = 1 * SDL_NS_PER_MS;
        if (pf->next_frame_ns > now + SPIN_NS) SDL_DelayNS(pf->next_frame_ns - now - SPIN_NS);
        while (SDL_GetTicksNS() < pf->next_frame_ns) { /* spin the last millisecond onto the deadline */ }
        prof_end(PROF_CAP_SLEEP);
    } else if (pf->fps_cap > 0) {
        pf->next_frame_ns = 0;   // vsync owns the pacing; do not carry a stale deadline into a cap change
    }
}

// Tile: the game window on the left, the tool window on the right, together filling the display
// the game is on. The game keeps its aspect through letterboxing, so any split works.
static void tile_windows(Platform *pf, float share, int *tw, int *th, int *tx, int *ty) {
    SDL_Rect r; SDL_DisplayID d = SDL_GetDisplayForWindow(pf->window);
    if (!d || !SDL_GetDisplayUsableBounds(d, &r)) { r = (SDL_Rect){ 0, 0, 1440, 900 }; }
    if (!pf->game_rect_saved) { SDL_GetWindowPosition(pf->window, &pf->saved_game_rect.x, &pf->saved_game_rect.y); SDL_GetWindowSize(pf->window, &pf->saved_game_rect.w, &pf->saved_game_rect.h); pf->game_rect_saved = true; }
    int gap = 8;
    *tw = (int)((float)(r.w - gap) * share); *th = r.h;
    int gw = r.w - gap - *tw, gh = r.h;
    if (gw > (int)(gh * 1.6f)) gw = (int)(gh * 1.6f); else gh = (int)(gw / 1.6f);
    SDL_SetWindowSize(pf->window, gw, gh);
    SDL_SetWindowPosition(pf->window, r.x, r.y + (r.h - gh) / 2);
    *tx = r.x + gw + gap; *ty = r.y;
}

void platform_tool_window_share(Platform *pf, bool open, float share, const char *title) {
    if (open && pf->console_win) { SDL_SetWindowTitle(pf->console_win, title); pf->console = true; return; }
    if (open && !pf->console_win && !SDL_getenv("HOLLOW_CONSOLE_INLINE")) {
        int w = 720, h = 820, x = 0, y = 0;
        bool tiled = !SDL_getenv("HOLLOW_NO_TILE") && !SDL_getenv("HOLLOW_TOOL_SIZE");
        if (SDL_getenv("HOLLOW_TOOL_SIZE")) sscanf(SDL_getenv("HOLLOW_TOOL_SIZE"), "%d %d", &w, &h);   // headless layout checks: "w h" in points
        if (tiled) tile_windows(pf, share, &w, &h, &x, &y);
        pf->console_win = SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (pf->console_win && !SDL_ClaimWindowForGPUDevice(pf->gpu, pf->console_win)) { SDL_DestroyWindow(pf->console_win); pf->console_win = NULL; }
        if (pf->console_win) {
            SDL_SetGPUSwapchainParameters(pf->gpu, pf->console_win, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);
            if (tiled) SDL_SetWindowPosition(pf->console_win, x, y);
            SDL_GetWindowSize(pf->console_win, &pf->tool_w, &pf->tool_h);
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "tool window failed: %s", SDL_GetError());
            pf->tool_w = w; pf->tool_h = h; pf->console = false; return;
        }
        SDL_RaiseWindow(pf->window);
    } else if (!open && pf->console_win) {
        SDL_WaitForGPUIdle(pf->gpu);
        SDL_ReleaseWindowFromGPUDevice(pf->gpu, pf->console_win);
        SDL_DestroyWindow(pf->console_win); pf->console_win = NULL; pf->console_swap = NULL;
        if (pf->game_rect_saved) { SDL_SetWindowSize(pf->window, pf->saved_game_rect.w, pf->saved_game_rect.h); SDL_SetWindowPosition(pf->window, pf->saved_game_rect.x, pf->saved_game_rect.y); pf->game_rect_saved = false; }
    }
    pf->console = open;
}

void platform_tool_window(Platform *pf, bool open, int w, int h, const char *title) {
    // legacy entry: the requested size only picks the split (wide tools get more of the screen)
    (void)h;
    platform_tool_window_share(pf, open, w >= 1000 ? 0.62f : 0.42f, title);
}

void platform_console_window(Platform *pf, bool open) { platform_tool_window(pf, open, pf->tool_w > 0 ? pf->tool_w : 720, pf->tool_h > 0 ? pf->tool_h : 820, "hollow debugger"); }

void platform_shutdown(Platform *pf) {
    for (unsigned i = 0; i < 2; i++) if (pf->inflight[i]) { SDL_WaitForGPUFences(pf->gpu, true, &pf->inflight[i], 1); SDL_ReleaseGPUFence(pf->gpu, pf->inflight[i]); pf->inflight[i] = NULL; }
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

// Hold (default): in->sprint tracks Shift for as long as it's down, same as always. Toggle: a
// Shift press edge flips a latch kept here, and that latch is in->sprint until either it is
// flipped again or the player lets go of every movement key (see the latch clear in poll) -- so
// toggle-sprint cannot survive a full stop and carry into standing still or a menu.
void platform_set_sprint_mode(Platform *pf, bool toggle) {
    pf->sprint_toggle = toggle;
    pf->sprint_latch = false;
}

// --- menu ---
void platform_text_input(Platform *pf, bool on) {
    if (pf->text_input == on) return;
    pf->text_input = on;
    if (on) SDL_StartTextInput(pf->window); else SDL_StopTextInput(pf->window);
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
