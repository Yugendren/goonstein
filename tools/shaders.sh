#!/usr/bin/env bash
# Compile Vulkan GLSL in shaders/ to SPIR-V (Vulkan/Linux/Deck), MSL (Metal/mac) and
# HLSL (source for DXIL on Windows) in assets/shaders/.
#
# Requires:
#   - glslc (brew install shaderc  |  apt install glslc / glslang-tools / shaderc)
#     falls back to glslangValidator if glslc is missing.
#   - spirv-cross (brew install spirv-cross  |  apt install spirv-cross)
#
# On macOS, if the full Metal toolchain (xcrun -sdk macosx metal / metallib) is
# available, also compiles the .msl into a .metallib. Set HOLLOW_SKIP_METALLIB=1
# to skip that step even when the toolchain is present.
#
# If `dxc` is on PATH, also emits .dxil (useful on Windows/WSL). DXIL cannot be
# produced on macOS/Linux by default because signing requires Microsoft's
# dxil.dll; use tools/shaders.ps1 on Windows to get signed DXIL from the
# committed .hlsl sources.
#
# Usage:
#   tools/shaders.sh                 rebuild all shaders in place
#   tools/shaders.sh --only lit.frag rebuild a single shader
#   tools/shaders.sh --check         rebuild into a temp dir and diff against
#                                     assets/shaders; exit non-zero if stale (CI)
set -euo pipefail

# ---------------------------------------------------------------------------
# Repo root, resolved from this script's own location so it works regardless
# of the caller's cwd.
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SHADER_SRC_DIR="$ROOT/shaders"

# ---------------------------------------------------------------------------
# Argument parsing.
# ---------------------------------------------------------------------------
ONLY_NAME=""
CHECK_MODE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --only)
            ONLY_NAME="${2:-}"
            if [[ -z "$ONLY_NAME" ]]; then
                echo "error: --only requires a shader file name (e.g. lit.frag)" >&2
                exit 1
            fi
            shift 2
            ;;
        --check)
            CHECK_MODE=1
            shift
            ;;
        -h|--help)
            sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "error: unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

# ---------------------------------------------------------------------------
# Tool discovery.
# ---------------------------------------------------------------------------
GLSLC=""
if command -v glslc >/dev/null 2>&1; then
    GLSLC="glslc"
elif command -v glslangValidator >/dev/null 2>&1; then
    : # fall back to glslangValidator, handled in compile_one
else
    cat >&2 <<'EOF'
error: no SPIR-V compiler found on PATH.
Install one of:
  glslc             brew install shaderc   |   apt install glslc  (from shaderc or glslang-tools)
  glslangValidator  (fallback)             |   apt install glslang-tools
EOF
    exit 1
fi

if ! command -v spirv-cross >/dev/null 2>&1; then
    cat >&2 <<'EOF'
error: spirv-cross not found on PATH.
Install it with:
  brew install spirv-cross   |   apt install spirv-cross
EOF
    exit 1
fi
SPIRV_CROSS="spirv-cross"

DXC=""
if command -v dxc >/dev/null 2>&1; then
    DXC="dxc"
fi

IS_MACOS=0
METAL_AVAILABLE=0
if [[ "$(uname -s)" == "Darwin" ]]; then
    IS_MACOS=1
    if [[ "${HOLLOW_SKIP_METALLIB:-0}" != "1" ]]; then
        if xcrun -sdk macosx -f metal >/dev/null 2>&1 && xcrun -sdk macosx -f metallib >/dev/null 2>&1; then
            METAL_AVAILABLE=1
        fi
    fi
fi

if [[ $IS_MACOS -eq 1 && $METAL_AVAILABLE -eq 0 && "${HOLLOW_SKIP_METALLIB:-0}" != "1" ]]; then
    echo "metallib: skipped (Metal toolchain not found — install Xcode, not just the Command Line Tools)"
fi

if [[ -z "$DXC" ]]; then
    echo "dxil: skipped (no dxc on PATH) — run tools/shaders.ps1 on Windows"
fi

# ---------------------------------------------------------------------------
# mktemp -d for .air intermediates, cleaned up on exit.
# ---------------------------------------------------------------------------
AIR_DIR="$(mktemp -d)"
cleanup() { rm -rf "$AIR_DIR"; }
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Compile one shader source into the given output directory. Prints a
# per-shader summary line of what was written.
# ---------------------------------------------------------------------------
compile_one() {
    local src="$1" out_dir="$2"
    local name spv msl hlsl profile air outputs
    name="$(basename "$src")"
    spv="$out_dir/$name.spv"
    msl="$out_dir/$name.msl"
    hlsl="$out_dir/$name.hlsl"
    outputs="spv"

    if [[ -n "$GLSLC" ]]; then
        glslc -O "$src" -o "$spv"
    else
        # glslangValidator has no direct equivalent of glslc's -O (perf opt);
        # -Os is its closest "optimise for size" flag.
        glslangValidator -V -Os "$src" -o "$spv"
    fi

    "$SPIRV_CROSS" --msl --msl-version 20100 --msl-decoration-binding "$spv" --output "$msl"
    outputs="$outputs msl"

    "$SPIRV_CROSS" --hlsl --shader-model 60 "$spv" --output "$hlsl"
    outputs="$outputs hlsl"

    if [[ $METAL_AVAILABLE -eq 1 ]]; then
        air="$AIR_DIR/$name.air"
        xcrun -sdk macosx metal -std=metal2.1 -o "$air" -c "$msl"
        xcrun -sdk macosx metallib "$air" -o "$out_dir/$name.metallib"
        outputs="$outputs metallib"
    fi

    if [[ -n "$DXC" ]]; then
        case "$name" in
            *.vert) profile="vs_6_0" ;;
            *.frag) profile="ps_6_0" ;;
            *)
                echo "error: cannot determine shader stage for dxc from name: $name" >&2
                exit 1
                ;;
        esac
        dxc -T "$profile" -E main -Fo "$out_dir/$name.dxil" "$hlsl"
        outputs="$outputs dxil"
    fi

    echo "  $name: $outputs"
}

# ---------------------------------------------------------------------------
# Collect the list of shader sources to build.
# ---------------------------------------------------------------------------
shader_list=()
while IFS= read -r -d '' f; do
    shader_list+=("$f")
done < <(find "$SHADER_SRC_DIR" -maxdepth 1 \( -name '*.vert' -o -name '*.frag' \) -print0 | sort -z)

if [[ -n "$ONLY_NAME" ]]; then
    match=""
    for f in "${shader_list[@]}"; do
        if [[ "$(basename "$f")" == "$ONLY_NAME" ]]; then
            match="$f"
            break
        fi
    done
    if [[ -z "$match" ]]; then
        echo "error: no such shader: $ONLY_NAME (looked in $SHADER_SRC_DIR)" >&2
        exit 1
    fi
    shader_list=("$match")
fi

if [[ ${#shader_list[@]} -eq 0 ]]; then
    echo "error: no *.vert / *.frag sources found in $SHADER_SRC_DIR" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# --check: build into a scratch dir, diff against assets/shaders, exit non-zero
# on any drift (missing, extra, or differing file). Never touches the repo.
# ---------------------------------------------------------------------------
if [[ $CHECK_MODE -eq 1 ]]; then
    CHECK_DIR="$(mktemp -d)"
    trap 'rm -rf "$AIR_DIR" "$CHECK_DIR"' EXIT
    mkdir -p "$CHECK_DIR/assets/shaders"

    for src in "${shader_list[@]}"; do
        compile_one "$src" "$CHECK_DIR/assets/shaders"
    done

    real_dir="$ROOT/assets/shaders"
    stale=0

    # Compare only the extensions we always produce deterministically
    # (spv/msl/hlsl/dxil). metallib is a binary blob whose bytes are not
    # guaranteed reproducible across toolchain versions, so it is excluded
    # from the staleness check.
    for src in "${shader_list[@]}"; do
        name="$(basename "$src")"
        for ext in spv msl hlsl dxil; do
            new_f="$CHECK_DIR/assets/shaders/$name.$ext"
            old_f="$real_dir/$name.$ext"
            if [[ -f "$new_f" && ! -f "$old_f" ]]; then
                echo "stale: $name.$ext is missing from assets/shaders"
                stale=1
            elif [[ -f "$new_f" && -f "$old_f" ]]; then
                if ! cmp -s "$new_f" "$old_f"; then
                    echo "stale: $name.$ext differs from assets/shaders"
                    stale=1
                fi
            fi
        done
    done

    if [[ $stale -ne 0 ]]; then
        echo "shaders --check: STALE (regenerate with tools/shaders.sh)"
        exit 1
    fi
    echo "shaders --check: up to date"
    exit 0
fi

# ---------------------------------------------------------------------------
# Normal build: write straight into assets/shaders.
# ---------------------------------------------------------------------------
OUT_DIR="$ROOT/assets/shaders"
mkdir -p "$OUT_DIR"

count=0
for src in "${shader_list[@]}"; do
    compile_one "$src" "$OUT_DIR"
    count=$((count + 1))
done

echo "shaders ok ($count shader$([[ $count -eq 1 ]] || echo s) built)"
