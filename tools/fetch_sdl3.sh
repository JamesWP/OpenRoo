#!/usr/bin/env bash
# Downloads the SDL3 MinGW development package into third_party/SDL3, where
# CMakeLists.txt looks for it (override with -DSDL3_ROOT=<dir>).  The package
# holds only an import library, so SDL3.dll ships beside the executable.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=3.4.18
DEST=third_party/SDL3
[[ -f "$DEST/x86_64-w64-mingw32/lib/libSDL3.dll.a" ]] && { echo "SDL3 $VERSION already in $DEST"; exit 0; }
mkdir -p "$DEST"
curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-$VERSION/SDL3-devel-$VERSION-mingw.tar.gz" \
    | tar xz --strip-components=1 -C "$DEST"
echo "SDL3 $VERSION unpacked in $DEST"
