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
#       tools/build_aab.sh --store [--apk] -> a Play production build (MG_STORE=ON: no Developer section in Settings)
# Without --store the build is for internal testing (MG_STORE=OFF: Settings keeps its Developer section).
set -euo pipefail
cd "$(dirname "$0")/../platform/android"
export ANDROID_HOME="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
[[ -f keystore.properties ]] || { echo "platform/android/keystore.properties missing (see header of this script)" >&2; exit 1; }
[[ -f ../../config.local.h ]] || { echo "config.local.h missing: copy config.local.h.example and fill it in" >&2; exit 1; }
VERSION_CODE="${VERSION_CODE:-$(date +%s | cut -c1-9)}"
TASKS=(bundleRelease)
APK=0
STORE=OFF
for arg in "$@"; do
    case "$arg" in
        --apk) APK=1; TASKS+=(assembleRelease) ;;
        --store) STORE=ON ;;
        *) echo "usage: tools/build_aab.sh [--store] [--apk]" >&2; exit 2 ;;
    esac
done
# a release build fails at configure without a supplied content pack (MG_CONTENT_DIR; none lives in the repository)
./gradlew -PmgVersionCode="$VERSION_CODE" -PmgRequireRealContent=ON -PmgStore="$STORE" "${TASKS[@]}"
echo "AAB: $(pwd)/app/build/outputs/bundle/release/app-release.aab (versionCode $VERSION_CODE, MG_STORE=$STORE)"
[[ "$APK" == 1 ]] && echo "APK: $(pwd)/app/build/outputs/apk/release/app-release.apk"
if [[ "$STORE" == ON ]]; then
    echo "Upload the AAB in Play Console > Production (or a closed / open track) > Create new release."
else
    echo "Upload the AAB in Play Console > Testing > Internal testing > Create new release."
fi
