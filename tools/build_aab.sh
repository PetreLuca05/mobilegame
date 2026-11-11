#!/usr/bin/env bash
# Build a signed release Android App Bundle for Google Play internal testing.
#
# One-time: create an upload key and platform/android/keystore.properties
#   keytool -genkeypair -v -keystore ~/mobilegame-upload.jks -alias upload \
#           -keyalg RSA -keysize 4096 -validity 10000
#   cat > platform/android/keystore.properties <<EOT
#   storeFile=/Users/you/mobilegame-upload.jks
#   storePassword=...
#   keyAlias=upload
#   keyPassword=...
#   EOT
# (keystore.properties and *.jks are git-ignored - back them up!)
#
# Then: tools/build_aab.sh        -> app-release.aab for Play Console > Testing > Internal testing
#       tools/build_aab.sh --apk  -> also a signed release APK for direct installs
set -euo pipefail
cd "$(dirname "$0")/../platform/android"
export ANDROID_HOME="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
[[ -f keystore.properties ]] || { echo "platform/android/keystore.properties missing (see header of this script)" >&2; exit 1; }
[[ -f ../../config.local.h ]] || { echo "config.local.h missing: copy config.local.h.example and fill it in" >&2; exit 1; }
VERSION_CODE="${VERSION_CODE:-$(date +%s | cut -c1-9)}"
TASKS=(bundleRelease)
[[ "${1:-}" == "--apk" ]] && TASKS+=(assembleRelease)
# BOOK-1: the release build fails at configure without the real pack (content_private/gastro/gastro.mgpack)
./gradlew -PmgVersionCode="$VERSION_CODE" -PmgRequireRealContent=ON "${TASKS[@]}"
echo "AAB: $(pwd)/app/build/outputs/bundle/release/app-release.aab (versionCode $VERSION_CODE)"
[[ "${1:-}" == "--apk" ]] && echo "APK: $(pwd)/app/build/outputs/apk/release/app-release.apk"
echo "Upload the AAB in Play Console > Testing > Internal testing > Create new release."
