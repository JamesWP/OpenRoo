#!/usr/bin/env python3
"""make_macos_app.py -- package a macOS build as OpenRoo.app.

    python3 tools/make_macos_app.py --exe build/OpenRoo --out build/OpenRoo.app

(the macos-app CMake target runs it).  The bundle holds:

    Contents/Info.plist
    Contents/MacOS/OpenRoo          the entry point: a script that prepares the
                                    run directory and starts the game in it
    Contents/MacOS/OpenRoo-bin      the game
    Contents/Resources/OpenRoo.icns from data/openroo.ico
    Contents/Resources/game/        game/, the data imported from your copy

The game reads and writes everything relative to its working directory, as it
does under launch.sh in run/.  An app may not write inside itself, so the
working directory is ~/Library/Application Support/OpenRoo: openroo.ini,
SavedGames/, highscores/ and the logs, with a symlink to each read-only data
entry in the bundle's game/.

game/ is copied with APFS clones (cp -c), which share the blocks and cost
no space; on another file system it is an ordinary copy.  The game links
Homebrew's SDL3 and FFmpeg where they are installed, so the bundle runs on
a Mac that has them.
"""
import argparse
import os
import plistlib
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUNDLE_ID = "io.github.jameswp.openroo"

# Contents/MacOS/OpenRoo.  What it links and skips matches launch.sh.
ENTRY = r'''#!/bin/bash
# Open'Roo's entry point: the game runs in ~/Library/Application Support/OpenRoo
# (see tools/make_macos_app.py).
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
data="$(cd "$here/../Resources/game" && pwd)"
run="$HOME/Library/Application Support/OpenRoo"
mkdir -p "$run/SavedGames" "$run/highscores"
for entry in "$data"/*; do
  name="$(basename "$entry")"
  case "$name" in
    SavedGames|highscores|ProgableControl.sav|Karoo.exe|res) continue ;;
  esac
  # Re-pointed every launch: the app may have moved.
  ln -sfn "$entry" "$run/$name"
done
cd "$run"
for log in karoo_hooks.log steam-123456.log; do
  [[ -f "$log" ]] && mv -f "$log" "$log.1"
done
# Finder used to pass a -psn_ argument; the game would take it for a game file.
args=()
for a in "$@"; do
  case "$a" in -psn_*) ;; *) args+=("$a") ;; esac
done
exec "$here/OpenRoo-bin" ${args[@]+"${args[@]}"}
'''


def ico_rgba(path):
    """The largest 32-bit image in an .ico, as (size, top-down RGBA)."""
    d = open(path, "rb").read()
    _, _, n = struct.unpack_from("<HHH", d, 0)
    best = None
    for i in range(n):
        w, _, _, _, _, bpp, _, off = struct.unpack_from("<BBBBHHII", d, 6 + 16 * i)
        w = w or 256
        if bpp == 32 and (best is None or w > best[0]):
            best = (w, off)
    if best is None:
        sys.exit("%s: no 32-bit image" % path)
    size, off = best
    hdr, = struct.unpack_from("<I", d, off)
    px = d[off + hdr: off + hdr + size * size * 4]   # bottom-up BGRA
    out = bytearray()
    for y in reversed(range(size)):
        row = px[y * size * 4:(y + 1) * size * 4]
        for x in range(size):
            b, g, r, a = row[x * 4:x * 4 + 4]
            out += bytes((r, g, b, a))
    return size, bytes(out)


def scaled_png(size, rgba, want):
    """rgba (size x size) scaled to want x want, nearest neighbour, as a PNG:
    the icon is pixel art, and stays sharp."""
    raw = bytearray()
    for y in range(want):
        sy = y * size // want
        raw.append(0)  # filter: none
        for x in range(want):
            sx = x * size // want
            i = (sy * size + sx) * 4
            raw += rgba[i:i + 4]

    def chunk(kind, data):
        c = kind + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", want, want, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def make_icns(ico, out):
    size, rgba = ico_rgba(ico)
    with tempfile.TemporaryDirectory() as tmp:
        iconset = os.path.join(tmp, "OpenRoo.iconset")
        os.mkdir(iconset)
        for base in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                name = "icon_%dx%d%s.png" % (base, base, "@2x" if scale == 2 else "")
                with open(os.path.join(iconset, name), "wb") as f:
                    f.write(scaled_png(size, rgba, base * scale))
        subprocess.run(["iconutil", "-c", "icns", iconset, "-o", out], check=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--exe", required=True, help="the built OpenRoo")
    ap.add_argument("--out", required=True, help="the .app to write")
    ap.add_argument("--game", default=os.path.join(REPO, "game"),
                    help="the imported game data (default: game/)")
    ap.add_argument("--version", default="0.0.0")
    a = ap.parse_args()

    if not os.path.isdir(a.game):
        sys.exit("%s missing: run tools/import_assets.py --from <your Ka'roo> "
                 "(or tools/gen_assets.py for placeholders) first" % a.game)

    # Built afresh beside the old one, then swapped in, so a failed run leaves
    # the last good app.
    tmp = a.out + ".tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    macos = os.path.join(tmp, "Contents", "MacOS")
    res = os.path.join(tmp, "Contents", "Resources")
    os.makedirs(macos)
    os.makedirs(res)

    with open(os.path.join(tmp, "Contents", "Info.plist"), "wb") as f:
        plistlib.dump({
            "CFBundleDevelopmentRegion": "en",
            "CFBundleDisplayName": "Open'Roo",
            "CFBundleExecutable": "OpenRoo",
            "CFBundleIconFile": "OpenRoo",
            "CFBundleIdentifier": BUNDLE_ID,
            "CFBundleInfoDictionaryVersion": "6.0",
            "CFBundleName": "Open'Roo",
            "CFBundlePackageType": "APPL",
            "CFBundleShortVersionString": a.version,
            "CFBundleVersion": a.version,
            "LSApplicationCategoryType": "public.app-category.puzzle-games",
            "NSHighResolutionCapable": True,
        }, f)

    entry = os.path.join(macos, "OpenRoo")
    with open(entry, "w") as f:
        f.write(ENTRY)
    os.chmod(entry, 0o755)
    # A new file, not one written over: macOS caches a binary's code
    # signature by file (see launch.sh).
    shutil.copy2(a.exe, os.path.join(macos, "OpenRoo-bin"))
    make_icns(os.path.join(REPO, "data", "openroo.ico"), os.path.join(res, "OpenRoo.icns"))

    # -c: APFS clones.  Falls back to a plain copy where cloning is refused.
    game = os.path.join(res, "game")
    if subprocess.run(["cp", "-cR", a.game, game]).returncode != 0:
        shutil.rmtree(game, ignore_errors=True)
        subprocess.run(["cp", "-R", a.game, game], check=True)

    shutil.rmtree(a.out, ignore_errors=True)
    os.rename(tmp, a.out)
    # Tell Finder and the Dock the icon may have changed.
    os.utime(a.out)
    print("wrote %s" % a.out)


if __name__ == "__main__":
    main()
