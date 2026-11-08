#!/usr/bin/env bash
# Compile shaders/*.{vert,frag}.hlsl to SPIR-V (Vulkan) and MSL (Metal).
# Outputs go to shaders/compiled/{spv,msl} and are committed.
#
# Preferred: SDL_shadercross (`shadercross` on PATH, or SHADERCROSS=/path).
# Fallback:  glslangValidator (HLSL -> SPIR-V) + spirv-cross (SPIR-V -> MSL),
#            producing the same SDL binding layout as shadercross
#            (apt install glslang-tools spirv-cross / brew install glslang spirv-cross).
set -euo pipefail
cd "$(dirname "$0")/.."

SRC=shaders
OUT=shaders/compiled
MSL_VERSION=2.1.0
mkdir -p "$OUT/spv" "$OUT/msl"

SC="${SHADERCROSS:-$(command -v shadercross || true)}"
if [[ -n "$SC" ]]; then
    echo "using shadercross: $SC"
else
    command -v glslangValidator >/dev/null || { echo "need shadercross or glslangValidator"; exit 1; }
    command -v spirv-cross >/dev/null || { echo "need shadercross or spirv-cross"; exit 1; }
    echo "shadercross not found; using glslangValidator + spirv-cross"
fi

for f in "$SRC"/*.vert.hlsl "$SRC"/*.frag.hlsl; do
    name=$(basename "$f" .hlsl) # e.g. mesh.vert
    case "$name" in
        *.vert) stage=vertex; gstage=vert; entry=VSMain ;;
        *.frag) stage=fragment; gstage=frag; entry=PSMain ;;
    esac
    spv="$OUT/spv/$name.spv"
    msl="$OUT/msl/$name.msl"
    if [[ -n "$SC" ]]; then
        "$SC" "$f" -s HLSL -d SPIRV -t "$stage" -e "$entry" -I "$SRC" -o "$spv"
        "$SC" "$f" -s HLSL -d MSL -t "$stage" -e "$entry" -I "$SRC" --msl-version "$MSL_VERSION" -o "$msl"
    else
        glslangValidator -D -V -S "$gstage" -e "$entry" -I"$SRC" -o "$spv" "$f" >/dev/null
        # MSL only: min16float becomes a real half (the SPIR-V stays 32-bit, no Float16 device feature needed)
        glslangValidator -D -V --hlsl-enable-16bit-types -S "$gstage" -e "$entry" -I"$SRC" -o "$spv.16" "$f" >/dev/null
        spirv-cross "$spv.16" --msl --msl-version 20100 --msl-decoration-binding --output "$msl"
        rm -f "$spv.16"
    fi
    echo "  $name -> spv, msl"
done
