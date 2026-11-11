#!/usr/bin/env bash
# CI on Linux (BOOK-1 decision 6): build the desktop target on the dummy content pack, run ctest and the headless
# desktop autotests (Xvfb + Mesa lavapipe). Used by .github/workflows/linux.yml and locally through Docker:
#   docker run --rm -v "$PWD":/src -w /src ubuntu:24.04 bash tools/ci_linux.sh --deps
# --deps installs the packages first (a bare container); the build goes to build/linux (or $BUILD).
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build/linux}
mkdir -p "$(dirname "$BUILD")" # a fresh checkout has no build/

if [[ "${1:-}" == "--deps" ]]; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get -o Acquire::Retries=5 update -qq
    apt-get -o Acquire::Retries=5 install -y -qq --no-install-recommends build-essential cmake ninja-build python3 git pkg-config \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev libxkbcommon-dev \
        libwayland-dev wayland-protocols libegl1-mesa-dev libgl1-mesa-dev libvulkan-dev mesa-vulkan-drivers \
        libasound2-dev libpulse-dev libdbus-1-dev libudev-dev xvfb xauth > /dev/null
fi

# the public repository and CI only ever see the dummy pack
if [[ -d content_private ]]; then
    echo "ci_linux: content_private/ is present; CI builds on the dummy pack only" >&2
fi

cmake -S . -B "$BUILD" -G Ninja -DMG_PLATFORM=desktop -DCMAKE_BUILD_TYPE=Release > "$BUILD.configure.log" 2>&1 ||
    { cat "$BUILD.configure.log"; exit 1; }
cmake --build "$BUILD" 2>&1 | tee "$BUILD.build.log" | grep -E "warning:|error:" || true
if grep -E "warning:|error:" "$BUILD.build.log" | grep -v third_party | grep -q .; then
    echo "ci_linux: the build has warnings or errors" >&2
    exit 1
fi
ctest --test-dir "$BUILD" --output-on-failure

# headless autotests: a virtual display; Mesa's lavapipe gives SDL GPU a Vulkan device
Xvfb :99 -screen 0 1280x800x24 > /dev/null 2>&1 &
XVFB=$!
trap 'kill $XVFB 2>/dev/null || true' EXIT
export DISPLAY=:99
sleep 1
fail=0
# a fresh machine starts on the Welcome screen: onboard a profile first, then everything runs on it
for t in onboarding touch layout ui text stickdir; do
    extra=(--profile ci)
    [[ $t == stickdir ]] && extra+=(--spawn -12,0.1,10)
    if timeout 1000 "$BUILD/mobilegame" --autotest "$t" "${extra[@]}" --quit-after 900 > "$BUILD.autotest_$t.log" 2>&1 &&
        grep -q "AUTOTEST PASSED" "$BUILD.autotest_$t.log"; then
        echo "PASS $t"
    else
        echo "FAIL $t: $(grep -E 'FAILED|ERROR' "$BUILD.autotest_$t.log" | head -2)"
        fail=1
    fi
done
exit $fail
