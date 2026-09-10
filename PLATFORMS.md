# Platforms

macOS, Linux (desktop and Steam Deck) and Windows from one tree, one CMake project and one shader
pipeline. This file is the contract: how to build each target, what is actually verified, what is
only read-and-believed, and what crossplay demands of the two ends.

## Status

| | build | run | notes |
|---|---|---|---|
| macOS 15, Apple Silicon, Metal | verified | verified | the dev machine; MSL shaders |
| macOS via MoltenVK (Vulkan) | verified | verified | forced with `SDL_GPU_DRIVER=vulkan`; proves the SPIR-V path |
| Ubuntu 24.04, arm64 (Docker) | verified | not run | headless container, no GPU; configures, compiles and links |
| Ubuntu / SteamOS on x86-64 | verified (CI) | untested | GitHub `ubuntu-latest`; `ldd` shows only libc/libm, so the zip is self-contained |
| Steam Deck | untested | untested | see below |
| Windows x86-64, MinGW | verified | untested | cross-compiled from macOS with mingw-w64; links `goonstein.exe` |
| Windows, MSVC | verified (CI) | verified (CI, WARP) | GitHub `windows-latest`, VS 2022 x64; D3D12 on the software adapter draws the island, 90 frames, screenshot checked |
| Windows, MSVC, real GPU | verified (CI) | untested | a tester launched the 2026-09-10 zip and it crashed; that crash is fixed, but no real Windows GPU has drawn a frame since |

"Verified" means a command was run on this machine and its output checked. "Verified (CI)" means a
GitHub runner did it. Until 2026-09-11 that only ever meant a compile and a link; the Windows job
now also runs the game and looks at the pixels (see below), so "verified (CI, WARP)" means a frame
was drawn and inspected -- by a software rasteriser, not a graphics driver. Everything else is
honest guesswork until someone runs it.

## Releases

Every push to `main` runs `.github/workflows/release.yml`, which builds the `HOLLOW_PORTABLE`
layout on all three targets and republishes `goonstein-{macos-arm64,linux-x86_64,windows-x86_64}
.zip` under the rolling `latest` tag. Each zip is one top-level `goonstein/` folder holding the
binary and `assets/`; the Windows one also carries the `.dxil` shaders that job compiles with
`dxc`, since D3D12 accepts nothing else and signed DXIL cannot be produced on macOS. Testers get
them with `get.sh` / `get.ps1` (see the top of README.md) rather than a build toolchain.

## Build

The first configure fetches and builds SDL3 from source (pinned to `release-3.4.16`) and libopus
(pinned to `v1.5.2`, for voice chat), which takes a
couple of minutes. After that, incremental builds are sub-second.

### macOS

    brew install cmake ninja shaderc spirv-cross
    cmake -B build -G Ninja
    cmake --build build -j8
    ./build/bin/goonstein

### Linux (Ubuntu 24.04 and derivatives)

SDL3 is built from source, so it needs the platform's dev headers. This is the full set; drop the
audio backends you do not want.

    sudo apt-get install -y build-essential cmake ninja-build pkg-config \
      libasound2-dev libpulse-dev libpipewire-0.3-dev libjack-dev libsndio-dev \
      libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev \
      libxss-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev \
      libegl1-mesa-dev libgl1-mesa-dev libdrm-dev libgbm-dev \
      libudev-dev libdbus-1-dev libibus-1.0-dev libvulkan-dev
    cmake --preset linux-release
    cmake --build --preset linux-release -j8

To recompile shaders on Linux you also need `glslc` and `spirv-cross`.

### Windows

MSVC (Visual Studio 2022, "Desktop development with C++" workload):

    cmake --preset windows-msvc-release
    cmake --build --preset windows-msvc-release

MinGW (MSYS2 UCRT64, `pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja}`):

    cmake --preset windows-mingw-release
    cmake --build --preset windows-mingw-release

The executable is a console subsystem app so `--frames N` runs can print. Flip `WIN32_EXECUTABLE`
to `TRUE` in CMakeLists.txt for a shipping build.

The MinGW path was checked for real by cross-compiling from the Mac (`brew install mingw-w64`,
a toolchain file setting `CMAKE_SYSTEM_NAME Windows` and the `x86_64-w64-mingw32-*` tools). SDL3
builds from source under mingw without any option changes, the `if(WIN32)` block links `ws2_32`,
`winmm` and `-static-libgcc`, `HOLLOW_PORTABLE` compiles in, and the link produces a PE32+ console
executable. The binary has never been *run* on Windows.

Three things it turned up, all fixed:

* `sscanf` in `platform.c` and `snprintf` in `uifx.c` used without `<stdio.h>` -- a warning under
  GCC on Linux, a hard error under GCC 16 for mingw.
* `%zu` passed to `SDL_LogError` in `model.c`. Plain `printf` accepts it, but SDL's
  `format(printf, ...)` attribute is checked against the Windows target, where `%zu` is not
  recognised, so it was a `-Wformat` warning with a real risk of printing garbage. Now cast to
  `unsigned long` with `%lu`.
* The link pulled in **`libwinpthread-1.dll`**, which is not part of Windows and is not bundled by
  `install()`/CPack -- a packaged build would have failed to launch on any machine without MSYS2.
  `if(MINGW)` now passes `-static`. Checked with `objdump -p`: the import table is now only stock
  system DLLs and the `api-ms-win-crt-*` UCRT forwarders that ship with Windows 10 and later.

### Presets

`CMakePresets.json` defines `mac-debug`, `mac-release`, `linux-debug`, `linux-release`,
`windows-msvc-debug`, `windows-msvc-release` and `windows-mingw-release`, each gated on the host
OS, plus a build preset per configure preset and a `<name>-package` build preset for each release.
Presets build into `build/<preset>/`; the plain `cmake -B build -G Ninja` dev flow still uses
`build/` and is untouched.

Release presets set `HOLLOW_PORTABLE=ON`, debug presets leave it off. See "Assets" below.

## Shaders

The GLSL 450 sources in `shaders/` are the single source of truth. They use SDL3 GPU's
descriptor-set convention (set 0-1 vertex, set 2-3 fragment), which is what lets one source
produce all four backend formats with the right bindings.

    shaders/*.vert,*.frag  (GLSL 450, Vulkan dialect)
             |
        glslc -O                    -> assets/shaders/NAME.spv       SPIR-V, for Vulkan
             |
        spirv-cross --msl           -> assets/shaders/NAME.msl       MSL source, for Metal
        spirv-cross --hlsl -sm 60   -> assets/shaders/NAME.hlsl      HLSL source, intermediate
             |                                                        (spaces 1/2/3 already correct)
        xcrun metal + metallib      -> assets/shaders/NAME.metallib   optional, macOS only
        dxc -T vs_6_0|ps_6_0        -> assets/shaders/NAME.dxil      D3D12, Windows only
             |
        the compiled files are committed, so a fresh clone builds and runs without any
        shader toolchain at all

`tools/shaders.sh` runs everything it can on the current machine (macOS and Linux):

    tools/shaders.sh              rebuild every shader
    tools/shaders.sh --only lit.frag
    tools/shaders.sh --check      rebuild into a temp dir and diff; non-zero if stale (CI gate)
    HOLLOW_SKIP_METALLIB=1 tools/shaders.sh

`tools/shaders.ps1` is the Windows half. Given `dxc` (Windows SDK, or a DirectXShaderCompiler
release, or `$env:DXC`) it compiles the committed HLSL into DXIL. If `glslc.exe` and
`spirv-cross.exe` are also on PATH it runs the whole pipeline instead.

### DXIL cannot be produced on this Mac

D3D12 will only load **signed** DXIL, and the signing step lives in Microsoft's `dxil.dll`, which
ships only on Windows. `dxc` is not in Homebrew either. So `assets/shaders/*.dxil` has to come from
a Windows machine. The `DXIL (Windows)` job in `.github/workflows/ci.yml` does exactly that: it
runs `tools/shaders.ps1` on `windows-latest` and uploads `assets/shaders/*.dxil` as an artifact.
Download it and commit the files whenever a shader changes.

Until DXIL is committed, a Windows build still runs if the GPU driver offers Vulkan; on a D3D12-only
driver it stops at startup with a log line naming the script that produces the missing format.

### Runtime selection

`load_shader` in `src/gfx.c` asks `SDL_GetGPUShaderFormats` what the device accepts and then takes
the first format that is also present on disk, in this order:

    metallib -> msl -> spv -> dxil -> dxbc

so a Metal device prefers a prebuilt library and falls back to compiling MSL at startup, a Vulkan
device takes SPIR-V, and a D3D12 device takes DXIL. The choice is logged once:

    shaders: SPIR-V (.spv) on GPU driver vulkan

If nothing matches, the error names the driver, the formats it wanted and the script that makes
them, instead of the old bare "shader missing".

`.metallib` needs the full Xcode Metal toolchain (`xcrun -sdk macosx metal`). This machine has only
the Command Line Tools, so metallib generation is skipped here and only `.msl` is shipped for Metal.

## Windows renders in CI, not just compiles

On 2026-09-10 a tester ran the published `goonstein-windows-x86_64.zip` and reported a black world
with the HUD still legible over it, and then a crash. CI had been green throughout, because
building was the only thing it had ever asked Windows to do.

The `Windows (MSVC)` job in `.github/workflows/ci.yml` now compiles the DXIL, installs the portable
layout and runs the packaged binary the way a tester runs it:

    SDL_GPU_DRIVER=direct3d12 HOLLOW_SILENT=1 \
      goonstein.exe --volume 0 --level island --frames 90 --screenshot shot.png --log run.log

`SDL_GPU_DRIVER` is pinned so the run cannot quietly fall back to Vulkan and prove nothing about
D3D12. GitHub's Windows runners have no GPU, so D3D12 lands on **WARP**, Microsoft's software
adapter -- slow (about 4 fps at 1280x800), but a real D3D12 device with a real shader compiler and
a real root signature, which is what catches a pipeline that will not compile or a format the
backend does not have.

Then it looks at the pixels. `tools/shot_check.py` decodes the PNG with nothing but the Python
standard library and fails the job on a frame that is black or too flat to be a scene:

    dist/bin/shot.png 1280x800 mean_luma=0.455 stddev=0.286 unique_colors=2751 nonblack=0.88

A plain "it exited zero" check would have waved a black screen straight through, which is the whole
point. `shot.png` and `run.log` are uploaded as the `windows-render-smoke` artifact whether the step
passes or fails, and the log is printed into the job output so a failure is readable without
downloading anything.

**What this caught immediately.** Not a graphics bug. The run exited `0xC00000FD`,
`STATUS_STACK_OVERFLOW`, one frame after `state -> EXPLORE`, with every pipeline created and every
texture format supported. Windows reserves 1 MB of stack for the main thread where macOS and Linux
reserve 8, and three functions built their result in a local struct before committing it:
`CharModel` at 1,225,656 bytes in `hero_hot_reload` (called once a second from `game_tick`), `Level`
at 636,560 in `level_load`, `Terrain` at 266,512 in `terrain_load`. The first of those alone is more
stack than Windows hands out. All three scratch copies are on the heap now, and the Windows link
asks for the same 8 MB stack the other platforms give away, so the next big local is not another
Windows-only crash found by a tester.

**What it still does not prove.** WARP is a software rasteriser. It shares D3D12's validation, its
shader compiler and its root-signature rules, so it catches nearly everything structural -- but it
is not a driver. Vendor driver bugs, real swapchain and present behaviour, display scaling, HDR, and
anything that depends on actual GPU timing still need a Windows machine with a graphics card.

## Assets and packaging

Two modes, chosen by `-DHOLLOW_PORTABLE`:

* **off (default, dev builds)** — `HOLLOW_ASSET_DIR` is the absolute path of `assets/` in the source
  tree. The binary can be run from anywhere and relative `--screenshot` paths resolve against the
  shell's working directory, exactly as before.
* **on (release presets, CI, packages)** — `HOLLOW_ASSET_DIR` is the relative string `"assets"`, and
  `platform_use_base_dir()` (`src/platform.c`) chdirs to the executable's own directory before
  anything is loaded. Assets travel with the binary.

    cmake --install build/mac-release --prefix dist
    dist/
      bin/
        goonstein
        assets/...

`cmake --build build/<preset> --target package` makes `goonstein-0.0.1-<System>-<arch>.tar.gz`
(`.zip` on Windows) with that same layout. `--target dist` stages it inside the build dir instead.

Note that a release preset's `build/<preset>/bin/goonstein` will **not** run in place: it is a
portable build, so it looks for `assets` beside itself and there is nothing there until you
install. Run `--target dist` and use `build/<preset>/dist/bin/goonstein`, or just use a debug
preset, which points at the source tree. Running it in place gives the shader error above naming
the missing directory, not a crash.

## Steam Deck

The Deck is just Linux: build with the `linux-release` preset. There is no `HOLLOW_STEAMDECK`
define and there should not be one.

* **Graphics** — Vulkan only. The SPIR-V shaders are the ones that matter, and they are the ones
  verified through MoltenVK on this Mac.
* **Resolution** — 1280x800, 16:10. The game already opens at exactly 1280x800, so the Deck's native
  mode needs no special case. The renderer letterboxes to the internal aspect, so docked 16:9 output
  is handled by the same path.
* **Gamepad** — SDL3's gamepad layer handles the Deck's built-in controller; Steam Input presents it
  as an Xbox-style pad, so the existing RB/LB/B/A/R3 bindings land correctly. The face-button and
  stick handling in `platform_poll` is generic, no Deck branch.
* **Frame pacing** — leave vsync on. `platform_end_frame` also honours `fps_cap`; 60 is a sensible
  Deck default for battery, 40 for the 40 Hz display mode.
* **Untested.** Nobody has run this on a Deck. The first real test should check: gamepad hotplug,
  the Vulkan swapchain at 1280x800, suspend/resume (SDL sends `SDL_EVENT_WILL_ENTER_BACKGROUND`,
  which the game currently ignores), and that the on-screen keyboard is not needed anywhere.

## Crossplay

The netcode is **host-authoritative**: one machine simulates, the others send input and render the
snapshots they are sent. That single decision removes almost every cross-platform hazard.

* **Same tick rate.** `TICK_HZ` is 60 in `src/main.c` and must be identical on every platform and in
  every build. It is a compile-time constant, not a setting, and it should stay that way. Rendering
  is decoupled and may run at any rate.
* **Same wire format.** `src/net.h` writes every field through an explicit little-endian byte cursor,
  so packets do not depend on struct layout, padding or the compiler. All three targets are
  little-endian anyway; the cursor means that stays true if that ever changes.
* **Float behaviour.** `-ffp-contract=off` (GCC/Clang) and `/fp:precise` (MSVC) stop the compiler
  from fusing multiply-add pairs, which is the most common way two builds of the same source
  disagree in the last bits. **Bit-exact determinism is explicitly NOT a requirement** — it would
  additionally need strict IEEE mode, no vectoriser reassociation, a fixed libm, and matched
  transcendental functions across three platforms, which is a large and fragile amount of work. The
  host is authoritative, so a client that computes a slightly different float only affects its own
  prediction, and the next snapshot corrects it. Contraction is disabled anyway because it is free
  and makes divergence smaller and easier to reason about.
* **Sockets.** `src/net_sys.h` is the only file that knows Winsock from BSD sockets. See below.
* **Version gate.** `NET_PROTO` in `src/net.h` should be bumped on any wire change, and a client
  with a different value must be refused with a clear message rather than desyncing.

## Voice chat (libopus)

`FetchContent` builds libopus from source alongside SDL3, so voice needs no system package on any
of the three platforms. The options `CMakeLists.txt` forces off matter for portability:

* `OPUS_BUILD_PROGRAMS`, `OPUS_BUILD_TESTING` -- opus_demo and the test suite are dead weight, and
  the tests want a `RunTest.cmake` path that assumes adb on Android.
* `OPUS_INSTALL_PKG_CONFIG_MODULE`, `OPUS_INSTALL_CMAKE_CONFIG_MODULE` -- opus adds its own
  `install()` rules, which would otherwise land a `.pc` file and a CMake package config inside our
  CPack payload.
* `OPUS_DRED`, `OPUS_OSCE` -- the two opus 1.5 features that pull neural model weights. These are
  the only parts of opus that want anything external at configure time, and they are off by
  default; we force them anyway so a future default flip cannot break an offline build.
* `OPUS_CUSTOM_MODES` -- unused, and it enlarges the API surface.

Nothing in the opus build needs perl, python, nasm or any other external tool: the ARM assembly
that would need `arm2gnu.pl` is only reached on MSVC ARM targets with intrinsics disabled, and we
never disable them. On Apple Silicon the configure log prints
`Runtime cpu capability detection needed for MAY_HAVE_NEON` -- that is opus reporting it will not
compile a runtime-dispatched NEON path (arm64 always has NEON, so the plain intrinsics path is
used); it is a message, not an error, and configure succeeds.

The microphone goes through SDL3's recording API (`SDL_AUDIO_DEVICE_DEFAULT_RECORDING`), so it is
CoreAudio on macOS, WASAPI on Windows and PulseAudio/PipeWire on Linux with no code of ours in
between. macOS will show the microphone permission prompt the first time a build opens a recording
device; `HOLLOW_VOICE_WAV=FILE` opens none at all, which is why the headless tests use it.

## net_sys.h

Header-only, no `.c` file, no SDL dependency. Include it *instead of* `<sys/socket.h>` and friends
and then write normal socket code: `socket`, `bind`, `sendto`, `recvfrom`, `setsockopt`,
`getaddrinfo`, `inet_pton`, `htons`/`ntohl` are spelled the same on both platforms. Only what
genuinely differs is wrapped.

| | |
|---|---|
| `netsys_socket` | the handle type: `int` on unix, `SOCKET` on Windows |
| `NETSYS_INVALID_SOCKET` | the "no socket" value |
| `NETSYS_SOCKET_ERROR` | the failure return of `bind`/`sendto`/`recvfrom` |
| `netsys_socklen`, `netsys_ssize` | `socklen_t`/`ssize_t` vs `int`/`int` |
| `NETSYS_OPTVAL(p)`, `NETSYS_OPTVAL_RW(p)` | cast for `setsockopt`/`getsockopt` values |
| `bool netsys_init(void)` | `WSAStartup(2,2)`, reference counted; no-op on unix |
| `void netsys_quit(void)` | matching `WSACleanup` |
| `bool netsys_valid(netsys_socket)` | **use this, never `fd >= 0`** — `SOCKET` is unsigned |
| `void netsys_close(netsys_socket)` | `close` / `closesocket` |
| `bool netsys_set_nonblocking(netsys_socket)` | `O_NONBLOCK` / `FIONBIO` |
| `int netsys_errno(void)` | `errno` / `WSAGetLastError()`; read it immediately after the failing call |
| `bool netsys_would_block(int err)` | `EAGAIN`/`EWOULDBLOCK` / `WSAEWOULDBLOCK` |
| `bool netsys_is_conn_reset(int err)` | see the Windows trap below |
| `bool netsys_is_unreachable(int err)` | host/net unreachable |
| `const char *netsys_strerror(int err, char *buf, size_t n)` | thread-safe message; returns `buf` |
| `bool netsys_udp_ignore_conn_reset(netsys_socket)` | Windows `SIO_UDP_CONNRESET` off; no-op on unix |
| `int netsys_wait_readable(netsys_socket, int ms)` | `select` wrapper, 1 / 0 / -1. Optional |
| `netsys_socket netsys_udp_open(port, rcvbuf, sndbuf)` | bound, non-blocking, `SO_REUSEADDR`, conn-reset off; port 0 = OS picks |
| `unsigned short netsys_local_port(netsys_socket)` | after `netsys_udp_open(0, ...)` |

Two things to get right:

1. **Store handles in `netsys_socket`, not `int`.** On 64-bit Windows `SOCKET` is a `UINT_PTR`; an
   `int` silently truncates it, and because it is unsigned, `fd >= 0` is always true.
2. **The Windows UDP connection-reset trap.** If a `sendto` provokes an ICMP port-unreachable, the
   *next* `recvfrom` on that socket fails with `WSAECONNRESET` instead of returning a waiting packet.
   A loop that treats any non-would-block error as fatal will drop the connection the first time a
   peer is briefly unreachable. Call `netsys_udp_ignore_conn_reset()` once after `socket()` and the
   problem disappears; `netsys_is_conn_reset()` is there for belt and braces.

Verified: loopback send/receive and a correct would-block classification on macOS; the same test
compiled and run on Ubuntu; and a clean `-Wall -Wextra -Wpedantic -Wshadow` cross-compile for
Windows with mingw-w64.

## Needs a change in src/main.c

These files are owned by another change in flight and were not touched here.

* **`platform_use_base_dir()` should be the first statement of `main()`.** In a `HOLLOW_PORTABLE`
  build `main()` reads `assets/settings.txt` *before* `platform_init` runs, so that one file is
  looked for relative to the shell's working directory instead of the executable's. Everything
  loaded after `platform_init` is fine. One line:

      int main(int argc, char **argv) {
          platform_use_base_dir();   // declared in platform.h; no-op unless HOLLOW_PORTABLE
          ...

* **`src/net.c` should include `src/net_sys.h`** and drop its own `<sys/socket.h>`, `<unistd.h>`,
  `<fcntl.h>`, `<netdb.h>`, `<arpa/inet.h>` block. Concretely: `NetSocket.fd` changes from `int` to
  `netsys_socket`, every `fd >= 0` / `fd = -1` becomes `netsys_valid(fd)` /
  `fd = NETSYS_INVALID_SOCKET`, `close(fd)` becomes `netsys_close(fd)`, the `fcntl` pair becomes
  `netsys_set_nonblocking(fd)`, and `errno == EAGAIN || errno == EWOULDBLOCK` becomes
  `netsys_would_block(netsys_errno())`. `netsys_udp_open()` does the whole open/options/bind
  sequence if that is simpler. Without this the game does not link on Windows.

## Known warnings

Clang on macOS is clean for the files changed here. GCC on Linux is stricter and reports, in
pre-existing code: `-Wmisleading-indentation` in `gfx.c`, `leveled.c`, `terrain.c`, `builder.c`,
`model.c`, `terrain_io.c`; `-Wformat-truncation` in `battle.c`, `charmodel.c`, `leveled.c`,
`sprite.c`, `builder.c`; two `-Wpedantic` pointer-qualifier notes in `game.c` and `props.c`; and
unused parameters in `gfx.c` and `debug.c`. None are errors.

The two `-Wimplicit-function-declaration` diagnostics GCC found -- `sscanf` in `platform.c` and
`snprintf` in `uifx.c`, both missing `<stdio.h>` that Apple's headers had been providing by accident
-- were real bugs. They were warnings under GCC 13 on Ubuntu and hard **errors** under GCC 16 for
mingw, so they blocked the Windows build outright. Fixed, plus the same include added defensively
to `audio.c`.

## What still has to be tested on real hardware

1. **Windows on a real GPU** — done in CI under WARP as of 2026-09-11: the window opens, the D3D12
   device comes up, all sixteen DXIL shaders load, all fourteen pipelines create, and the island is
   drawn and checked for 90 frames. What is left is a graphics driver: a Windows machine with a card
   in it, running the published zip, confirming that it launches and draws. A tester tried on
   2026-09-10 and got the stack overflow that is now fixed; nobody has run it since.
2. **Windows audio** — the CI run uses `HOLLOW_SILENT=1` and the runner has no recording device, so
   WASAPI playback and capture are still unexercised on Windows.
3. **Windows sockets** — `net_sys.h` compiles for Windows but has never bound a socket there.
4. **Linux x86-64 with a GPU** — the container build has no GPU, so the Vulkan swapchain, gamepad
   hotplug and audio backends are unexercised on real Linux.
5. **Steam Deck** — see above.
6. **A real cross-platform session** — Mac host with a Windows client, and the reverse.
