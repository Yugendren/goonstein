# Bumped from 3.24 -> 3.25 so CMakePresets.json can use schema version 6
# (LINUX variable also needs 3.25).
cmake_minimum_required(VERSION 3.25)
project(goonstein VERSION 0.0.1 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Load assets from ./assets next to the executable instead of the absolute
# source-tree path. Used for packaged/installed builds. The matching chdir
# lives in src/platform.c under #ifdef HOLLOW_PORTABLE.
option(HOLLOW_PORTABLE "Load assets from ./assets next to the working directory (packaged builds)" OFF)

# Only single-config generators (Makefiles, Ninja) need a default build type;
# multi-config generators (Visual Studio, Xcode) pick the config at build time.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR LINUX)
  find_package(Threads REQUIRED)
endif()

# --- SDL3, fetched and pinned so every platform and CI builds the same thing.
include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG        release-3.4.16
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(SDL3)

# --- libopus (BSD 3-clause), for proximity voice chat. Pinned like SDL so CI and all three
# platforms build the same codec. Everything optional is off: no programs, no tests, no install
# rules (they would land in our CPack payload), no DRED/OSCE (those are the only parts of opus
# 1.5 that want a model download, i.e. an external tool at configure time), no custom modes.
# Nothing here needs perl or any other external tool -- checked against opus/CMakeLists.txt.
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_CUSTOM_MODES OFF CACHE BOOL "" FORCE)
set(OPUS_DRED OFF CACHE BOOL "" FORCE)
set(OPUS_OSCE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  opus
  GIT_REPOSITORY https://github.com/xiph/opus.git
  GIT_TAG        v1.5.2
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(opus)

# --- Game
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS src/*.c)
add_executable(${PROJECT_NAME} ${GAME_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3 Opus::opus)

if(MSVC)
  # /fp:precise is MSVC's default; stated explicitly so it survives someone
  # later flipping optimization flags. /utf-8 keeps narrow-string literals in
  # UTF-8 regardless of the build machine's locale. _CRT_SECURE_NO_WARNINGS
  # silences the deprecation warnings on snprintf/sscanf/etc that MSVC's CRT
  # emits in favor of its own _s variants, which we don't use.
  # /wd4505 = "unreferenced local function has been removed": MSVC's version of the
  # clang/GCC __attribute__((unused)) marker we put on deliberately-kept dead helpers.
  # It is reported at end of translation unit, so a #pragma around the function cannot
  # silence it and it has to be turned off here.
  target_compile_options(${PROJECT_NAME} PRIVATE /W4 /wd4505 /fp:precise /utf-8)
  target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  # -ffp-contract=off disables FMA contraction so float results match across
  # compilers/platforms. This does NOT give us full cross-platform
  # determinism (that would also need strict IEEE math, no vectorizer
  # reassociation, etc) -- it isn't required because the netcode host is
  # authoritative and clients just render whatever state it sends.
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -ffp-contract=off)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
  $<$<CONFIG:Debug>:HOLLOW_DEBUG=1>)

if(HOLLOW_PORTABLE)
  target_compile_definitions(${PROJECT_NAME} PRIVATE
    HOLLOW_ASSET_DIR="assets"
    HOLLOW_PORTABLE=1)
else()
  target_compile_definitions(${PROJECT_NAME} PRIVATE
    HOLLOW_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
endif()

# --- Platform link needs
if(WIN32)
  # ws2_32: netcode uses Winsock. winmm: used for timing. iphlpapi: GetAdaptersAddresses
  # (the LAN address list).
  target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 winmm iphlpapi)
  # Windows reserves 1 MB of stack for the main thread; macOS and Linux give it 8. That single
  # number is the whole reason a build that runs here died on a tester's machine: level_load and
  # the hero hot-reload each build their result in a local before committing it, and those locals
  # are 620 kB and 1.2 MB of struct. Those two are on the heap now, but the next big local should
  # not be a Windows-only crash discovered by a tester, so ask for the same 8 MB everyone else has.
  if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE /STACK:8388608)
  else()
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--stack,8388608)
  endif()
  # FALSE keeps a console attached so `--frames N` CI runs can print to
  # stdout. Flip to TRUE for a shipping/consumer build to drop the console
  # window.
  set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE FALSE)
  if(MINGW)
    # -static is the one that matters: without it the exe imports libwinpthread-1.dll, which is
    # not part of Windows and is not bundled by install()/CPack, so a packaged build would fail
    # to launch on any machine without MSYS2 installed. Verified with objdump -p on a
    # cross-compiled goonstein.exe.
    target_link_options(${PROJECT_NAME} PRIVATE -static -static-libgcc)
    if(CMAKE_CXX_COMPILER_LOADED)
      target_link_options(${PROJECT_NAME} PRIVATE -static-libstdc++)
    endif()
  endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR LINUX)
  target_link_libraries(${PROJECT_NAME} PRIVATE m ${CMAKE_DL_LIBS} Threads::Threads)
endif()

# Put the binary next to the assets dir layout we ship with.
set_target_properties(${PROJECT_NAME} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- Install / packaging
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
# Assets sit next to the binary so a HOLLOW_PORTABLE build run from bin/
# finds ./assets. Candidates to exclude later: *.blend, models/import (raw
# CAD/import sources not needed at runtime).
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/ DESTINATION bin/assets)

if(APPLE)
  set(CPACK_GENERATOR "TGZ")
elseif(WIN32)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()
set(CPACK_PACKAGE_NAME "goonstein")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
include(CPack)

# Convenience: `cmake --build . --target dist` installs into build/dist
# without touching a system prefix.
add_custom_target(dist
  COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${CMAKE_BINARY_DIR}/dist
  COMMENT "Installing into ${CMAKE_BINARY_DIR}/dist")
add_dependencies(dist ${PROJECT_NAME})   # never lay out a stale binary

# Content gate: verify DESIGN.md hard rules
add_custom_target(content_gate
  COMMAND bash ${CMAKE_SOURCE_DIR}/tools/content_gate.sh
  COMMENT "Running content gate: verifying DESIGN.md hard rules")
