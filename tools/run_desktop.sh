#!/usr/bin/env bash
# Build and run the desktop debug target. Extra args go to the game,
# e.g. tools/run_desktop.sh --quit-after 10
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD=build/desktop
GEN=()
command -v ninja >/dev/null && GEN=(-G Ninja)
cmake -S . -B "$BUILD" "${GEN[@]}" -DMG_PLATFORM=desktop -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
cmake --build "$BUILD" --parallel
exec "$BUILD/mobilegame" "$@"
