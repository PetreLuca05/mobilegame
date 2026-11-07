#!/usr/bin/env bash
# Build the debug APK, install on the connected device, launch and tail logs.
# Needs ANDROID_HOME (SDK with NDK r27 / ndk;27.3.13750724 and cmake;3.31.6).
set -euo pipefail
cd "$(dirname "$0")/../platform/android"
export ANDROID_HOME="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
./gradlew assembleDebug
APK=app/build/outputs/apk/debug/app-debug.apk
echo "APK: $(pwd)/$APK"
if ! command -v adb >/dev/null; then
    PATH="$ANDROID_HOME/platform-tools:$PATH"
fi
if [[ -z "$(adb devices | sed -n '2,$p' | grep -w device || true)" ]]; then
    echo "No Android device connected; APK built only." >&2
    exit 2
fi
adb install -r "$APK"
adb shell am start -n ro.docvex.mobilegame/.MainActivity
adb logcat -c
exec adb logcat -s SDL:V SDL/APP:V SDL/ERROR:V DEBUG:V AndroidRuntime:E
