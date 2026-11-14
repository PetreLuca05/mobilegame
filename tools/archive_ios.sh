#!/usr/bin/env bash
# Archive a Release build and upload it to App Store Connect / TestFlight.
#
#   MG_IOS_TEAM_ID=ABCDE12345 tools/archive_ios.sh            # archive + export .ipa to build/ios/export
#   MG_IOS_TEAM_ID=ABCDE12345 tools/archive_ios.sh --upload   # archive + upload to TestFlight
#   MG_IOS_TEAM_ID=ABCDE12345 tools/archive_ios.sh --store [--upload]  # App Store production build (MG_STORE=ON:
#                                                     # no Developer section in Settings), in build/ios_store
# Without --store the archive is a TestFlight build (MG_STORE=OFF: Settings keeps its Developer section).
#
# Upload needs an App Store Connect API key (Users and Access > Integrations):
#   ASC_KEY_ID=XXXXXXXXXX ASC_ISSUER_ID=uuid ASC_KEY_PATH=~/AuthKey_XXXXXXXXXX.p8
# The app record (bundle id ro.docvex.mobilegame) must exist in App Store Connect.
set -euo pipefail
cd "$(dirname "$0")/.."
UPLOAD=0
STORE=OFF
for arg in "$@"; do
    case "$arg" in
        --upload) UPLOAD=1 ;;
        --store) STORE=ON ;;
        *) echo "usage: tools/archive_ios.sh [--store] [--upload]" >&2; exit 2 ;;
    esac
done
TEAM="${MG_IOS_TEAM_ID:?set MG_IOS_TEAM_ID to your Apple developer team ID}"
BUILD=build/ios
[[ "$STORE" == ON ]] && BUILD=build/ios_store # its own cache: a store build never leaks MG_STORE into build/ios
ARCHIVE="$BUILD/Mobilegame.xcarchive"
EXPORT="$BUILD/export"
DEST=export
AUTH=()
if [[ "$UPLOAD" == 1 ]]; then
    DEST=upload
    : "${ASC_KEY_ID:?set ASC_KEY_ID}" "${ASC_ISSUER_ID:?set ASC_ISSUER_ID}" "${ASC_KEY_PATH:?set ASC_KEY_PATH}"
    AUTH=(-authenticationKeyPath "$ASC_KEY_PATH" -authenticationKeyID "$ASC_KEY_ID"
          -authenticationKeyIssuerID "$ASC_ISSUER_ID")
fi
[[ -f config.local.h ]] || { echo "config.local.h missing: copy config.local.h.example and fill it in" >&2; exit 1; }

# a store build needs a supplied content pack (MG_REQUIRE_REAL_CONTENT fails at configure without one);
# MG_STORE is always explicit: ON only for --store (App Store), OFF for TestFlight
cmake -S . -B "$BUILD" -G Xcode -DMG_PLATFORM=ios -DCMAKE_SYSTEM_NAME=iOS -DMG_PROF=OFF \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 -DCMAKE_OSX_ARCHITECTURES=arm64 -DMG_IOS_TEAM_ID="$TEAM" \
    -DMG_REQUIRE_REAL_CONTENT=ON -DMG_CONTENT_DIR="${MG_CONTENT_DIR:-}" -DMG_STORE="$STORE"
xcodebuild -project "$BUILD/mobilegame.xcodeproj" -scheme mobilegame -configuration Release \
    -destination 'generic/platform=iOS' -archivePath "$ARCHIVE" -allowProvisioningUpdates "${AUTH[@]}" \
    archive
sed -e "s/@TEAM_ID@/$TEAM/" -e "s/@DESTINATION@/$DEST/" platform/ios/ExportOptions.plist.in > "$BUILD/ExportOptions.plist"
xcodebuild -exportArchive -archivePath "$ARCHIVE" -exportPath "$EXPORT" \
    -exportOptionsPlist "$BUILD/ExportOptions.plist" -allowProvisioningUpdates "${AUTH[@]}"
if [[ "$DEST" == upload ]]; then
    echo "Uploaded (MG_STORE=$STORE). The build appears in App Store Connect > TestFlight after processing (~10-30 min)."
else
    echo "IPA: $EXPORT (MG_STORE=$STORE)"
fi
