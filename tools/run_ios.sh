#!/usr/bin/env bash
# Generate the Xcode project, build, install and launch on a connected iPhone.
#   MG_IOS_TEAM_ID=ABCDE12345 tools/run_ios.sh          build + install + launch
#   MG_IOS_TEAM_ID=ABCDE12345 tools/run_ios.sh --open   just generate and open Xcode
# Needs Xcode 15+ (xcrun devicectl) and a device with Developer Mode enabled.
set -euo pipefail
cd "$(dirname "$0")/.."
TEAM="${MG_IOS_TEAM_ID:-}"
CONFIG="${CONFIG:-Debug}"
BUILD=build/ios
if [[ -z "$TEAM" ]]; then
    echo "Set MG_IOS_TEAM_ID to your Apple developer team ID (Xcode > Settings > Accounts)." >&2
    exit 1
fi
cmake -S . -B "$BUILD" -G Xcode \
    -DMG_PLATFORM=ios \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DMG_IOS_TEAM_ID="$TEAM"
if [[ "${1:-}" == "--open" ]]; then
    open "$BUILD/mobilegame.xcodeproj"
    exit 0
fi
xcodebuild -project "$BUILD/mobilegame.xcodeproj" -scheme mobilegame -configuration "$CONFIG" \
    -destination 'generic/platform=iOS' -allowProvisioningUpdates build | xcpretty 2>/dev/null || \
xcodebuild -project "$BUILD/mobilegame.xcodeproj" -scheme mobilegame -configuration "$CONFIG" \
    -destination 'generic/platform=iOS' -allowProvisioningUpdates build -quiet
APP=$(find "$BUILD" -path "*${CONFIG}-iphoneos/Mobilegame.app" -maxdepth 3 | head -1)
[[ -d "$APP" ]] || { echo "built app not found under $BUILD" >&2; exit 1; }

JSON=$(mktemp)
xcrun devicectl list devices --json-output "$JSON" >/dev/null
DEVICE=$(python3 - "$JSON" <<'PY'
import json, sys
devs = json.load(open(sys.argv[1]))["result"]["devices"]
for d in devs:
    hw = d.get("hardwareProperties", {})
    conn = d.get("connectionProperties", {})
    if hw.get("platform") == "iOS" and conn.get("tunnelState") != "unavailable":
        print(d["identifier"]); break
PY
)
rm -f "$JSON"
if [[ -z "$DEVICE" ]]; then
    echo "No connected iPhone found. Built: $APP" >&2
    exit 2
fi
xcrun devicectl device install app --device "$DEVICE" "$APP"
xcrun devicectl device process launch --device "$DEVICE" --terminate-existing ro.docvex.mobilegame
