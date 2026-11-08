#!/usr/bin/env bash
# Builds exactly what is being committed (the index, not the working tree) for desktop.
# Any compile error or warning in our code fails the commit.
set -u
cd "$(git rev-parse --show-toplevel)"
ROOT=$PWD
SRC=build/precommit_src
BLD=build/precommit
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
git checkout-index -a --prefix="$TMP/"
mkdir -p "$SRC"
# only files that really changed get a new timestamp, so the build stays incremental
rsync -rc --delete --exclude third_party --exclude config.local.h "$TMP/" "$SRC/"
ln -sfn "$ROOT/third_party" "$SRC/third_party"
[[ -f config.local.h ]] && cp -p config.local.h "$SRC/config.local.h"
if ! cmake -S "$SRC" -B "$BLD" -G Ninja -DMG_PLATFORM=desktop > "$TMP/configure.log" 2>&1; then
    tail -20 "$TMP/configure.log" >&2
    echo "pre-commit: cmake configure failed" >&2
    exit 1
fi
if ! cmake --build "$BLD" > "$TMP/build.log" 2>&1; then
    grep -E "error|FAILED" "$TMP/build.log" | head -30 >&2
    echo "pre-commit: the build failed, nothing committed" >&2
    exit 1
fi
if grep -E "warning:" "$TMP/build.log" | grep -v third_party | grep -q .; then
    grep -E "warning:" "$TMP/build.log" | grep -v third_party | head -30 >&2
    echo "pre-commit: warnings in our code, nothing committed" >&2
    exit 1
fi
echo "pre-commit: desktop build is green"
