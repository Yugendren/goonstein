#!/usr/bin/env bash
# Compile Vulkan GLSL in shaders/ to SPIR-V (Vulkan/Linux/Deck), MSL (Metal/mac) and
# HLSL (source for DXIL on Windows) in assets/shaders/.
# Requires: glslc (brew install shaderc), spirv-cross (brew install spirv-cross).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p assets/shaders
for src in shaders/*.vert shaders/*.frag; do
    name=$(basename "$src")
    glslc -O "$src" -o "assets/shaders/$name.spv"
    spirv-cross --msl --msl-version 20100 --msl-decoration-binding "assets/shaders/$name.spv" --output "assets/shaders/$name.msl"
    spirv-cross --hlsl --shader-model 60 "assets/shaders/$name.spv" --output "assets/shaders/$name.hlsl"
    echo "  $name"
done
echo "shaders ok"
