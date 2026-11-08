#!/usr/bin/env bash
# Generate the Xcode project, build, install and launch on a connected iPhone/iPad.
#   MG_IOS_TEAM_ID=ABCDE12345 tools/run_ios.sh          build + install + launch
#   MG_IOS_TEAM_ID=ABCDE12345 tools/run_ios.sh --open   just generate and open Xcode
#   MG_IOS_DEVICE=ipad ...                              only that device: iphone, ipad, or a
#                                                       devicectl identifier (default: prefer an iPhone)
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
    -destination 'generic/platform=iOS' -allowProvisioningUpdates -quiet build
APP=$(find "$BUILD" -path "*${CONFIG}-iphoneos/Mobilegame.app" -maxdepth 3 | head -1)
[[ -d "$APP" ]] || { echo "built app not found under $BUILD" >&2; exit 1; }

JSON=$(mktemp)
xcrun devicectl list devices --json-output "$JSON" >/dev/null
DEVICE=$(python3 - "$JSON" "${MG_IOS_DEVICE:-}" <<'PY'
import json, sys
devs = json.load(open(sys.argv[1]))["result"]["devices"]
want = sys.argv[2]
ok = [d for d in devs
      if d.get("hardwareProperties", {}).get("platform") == "iOS"
      and d.get("connectionProperties", {}).get("tunnelState") != "unavailable"]
kind = lambda d: d.get("hardwareProperties", {}).get("deviceType", "").lower()
if not want:  # the game targets phones: prefer an iPhone, else any device
    ok.sort(key=lambda d: kind(d) != "iphone")
elif want.lower() in ("iphone", "ipad"):
    ok = [d for d in ok if kind(d) == want.lower()]
else:
    ok = [d for d in ok if d["identifier"] == want]
if ok:
    print(ok[0]["identifier"])
PY
)
rm -f "$JSON"
if [[ -z "$DEVICE" ]]; then
    echo "No connected device found (MG_IOS_DEVICE=${MG_IOS_DEVICE:-any}). Built: $APP" >&2
    exit 2
fi
xcrun devicectl device install app --device "$DEVICE" "$APP"
xcrun devicectl device process launch --device "$DEVICE" --terminate-existing ro.docvex.mobilegame
