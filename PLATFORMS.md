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
| Ubuntu / SteamOS on x86-64 | untested | untested | same code path as the arm64 container; needs real hardware |
| Steam Deck | untested | untested | see below |
| Windows x86-64, MinGW | verified | untested | cross-compiled from macOS with mingw-w64; links `goonstein.exe` |
| Windows, MSVC | untested | untested | no Windows machine here; CMake written from the docs, CI covers it |

"Verified" means a command was run on this machine and its output checked. Everything else is
honest guesswork until someone runs it.

## Build

The first configure fetches and builds SDL3 from source (pinned to `release-3.4.16`), which takes a
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
executable. Only two things in the game's own code stood in the way, both of them `sscanf`/
`snprintf` used without `<stdio.h>` -- a warning under GCC on Linux and a hard error under GCC 16
on Windows -- and both are fixed. The binary has never been *run* on Windows.

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

1. **Windows, MSVC** — configure, build and run. The MinGW cross-compile exercises the same
   `if(WIN32)` CMake branch, but MSVC's compiler flags (`/W4 /fp:precise /utf-8`), the Visual Studio
   generator and the static-SDL3 runtime pairing have never been executed.
2. **Running on Windows at all** — the cross-compiled `goonstein.exe` links but has not been
   launched, so D3D12 device creation, DXIL loading, the window and the audio backend are unproven.
3. **Windows sockets** — `net_sys.h` compiles for Windows but has never bound a socket there.
4. **Linux x86-64 with a GPU** — the container build has no GPU, so the Vulkan swapchain, gamepad
   hotplug and audio backends are unexercised on real Linux.
5. **Steam Deck** — see above.
6. **A real cross-platform session** — Mac host with a Windows client, and the reverse.
