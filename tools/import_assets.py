#!/usr/bin/env python3
"""import_assets.py -- fill game/ with the game data, from your own copy of Ka'roo.

The repository carries no game data.  assets/manifest.json lists every file
the game reads, by the path the game opens it under, with its size and
SHA-256.  This tool finds each one in the sources you give it, checks the
hash, and writes it under game/.  The original executable is the one
exception: it is read only for its resources (dialogs, icon, window shape),
which are extracted to game/res/; the exe itself is never copied.  game/
ends up holding only the read-only data the game and the build need.

    python3 tools/import_assets.py --from KaRoo.zip            # a zip of an install
    python3 tools/import_assets.py --from /path/to/KaRoo       # an install directory
    python3 tools/import_assets.py --from A --from B           # several, first wins
    python3 tools/import_assets.py --check                     # verify game/ only
    python3 tools/import_assets.py manifest <install-dir>      # regenerate the manifest

Sources are searched in the order given, and the first file whose hash
matches wins.  A source is a directory or a .zip; a zip whose entries all sit
under one top-level folder is read from inside that folder.  To import from a
CD image, mount it and pass the mount point.  Name matching is
case-insensitive, as it is for the game under Windows.

Manifest entries:

    path      where the file goes under game/, and the name the game opens
    sha256    the only version accepted
    size      bytes
    class     "required" -- the game needs it
              "optional" -- the game runs without it (CD music, intro video)
    from      other names the file may have in a source, tried after `path`
              (a cracked install keeps the original exe as Karoo.exe.bak)
    source_only  true: found and hash-checked in a source, never written
              to game/ (the original exe)
    extracted_from  the source_only entry this file is cut from, not
              found in a source itself (game/res/*.bin, the exe's resources)

Sources are a list so that a later source of *our own* replacement files can
override entries by path; only installs of the original game exist today.

Never overwrites a file in game/ whose hash differs from the manifest unless
--force is given.  Exits non-zero if any required file is missing or wrong.
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_rsrc  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(REPO, "assets", "manifest.json")
GAME = os.path.join(REPO, "game")


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def norm(path):
    return path.replace("\\", "/").strip("/").lower()


class DirSource:
    def __init__(self, root):
        self.name = root
        self.index = {}
        for dirpath, _, files in os.walk(root):
            for f in files:
                full = os.path.join(dirpath, f)
                self.index.setdefault(norm(os.path.relpath(full, root)), full)

    def read(self, key):
        with open(self.index[key], "rb") as fh:
            return fh.read()


class ZipSource:
    def __init__(self, path):
        self.name = path
        self.zip = zipfile.ZipFile(path)
        names = [i.filename for i in self.zip.infolist() if not i.is_dir()]
        tops = {n.split("/", 1)[0] for n in names}
        strip = len(tops) == 1 and all("/" in n for n in names)
        self.index = {}
        for n in names:
            rel = n.split("/", 1)[1] if strip else n
            self.index.setdefault(norm(rel), n)

    def read(self, key):
        return self.zip.read(self.index[key])


def open_source(path):
    if os.path.isdir(path):
        return DirSource(path)
    if zipfile.is_zipfile(path):
        return ZipSource(path)
    sys.exit("not a directory or a zip: %s" % path)


def load_manifest():
    with open(MANIFEST) as fh:
        return json.load(fh)


def find(entry, sources):
    """The first candidate in any source whose hash matches, else None.

    Returns (data, where) on success; the mismatches seen on the way are
    returned too, so a wrong version is reported as wrong, not as missing.
    """
    wrong = []
    for src in sources:
        for name in [entry["path"]] + entry.get("from", []):
            key = norm(name)
            if key not in src.index:
                continue
            data = src.read(key)
            if sha256_bytes(data) == entry["sha256"]:
                return data, "%s:%s" % (src.name, name), wrong
            wrong.append("%s:%s" % (src.name, name))
    return None, None, wrong


def extracted(entry, parent, sources, derived):
    """An entry cut from a source_only file: find the parent in the sources
    (once), extract its resources, and hash-check this one."""
    key = parent["path"]
    if key not in derived:
        data, where, wrong = find(parent, sources)
        derived[key] = (extract_rsrc.extract(data) if data is not None else None,
                        where, wrong)
    blobs, where, wrong = derived[key]
    if blobs is None:
        return None, None, wrong
    stem = os.path.splitext(os.path.basename(entry["path"]))[0]
    data = blobs.get(stem)
    if data is None or sha256_bytes(data) != entry["sha256"]:
        return None, None, ["%s (extracted)" % where]
    return data, "%s -> %s" % (where, stem), []


def cmd_import(args):
    m = load_manifest()
    sources = [open_source(s) for s in args.sources]
    counts = {"written": 0, "present": 0, "missing": 0, "wrong": 0, "blocked": 0}
    failed_required = []
    derived = {}       # source_only path -> {stem: bytes}, filled on demand
    by_path = {e["path"]: e for e in m["files"]}
    for e in m["files"]:
        if e.get("source_only"):
            stale = os.path.join(GAME, *e["path"].split("/"))
            if os.path.exists(stale) and sha256_file(stale) == e["sha256"]:
                os.remove(stale)       # an earlier import copied it; not wanted
                print("  removed  %s (only its resources are kept)" % e["path"])
            continue
        dst = os.path.join(GAME, *e["path"].split("/"))
        if os.path.exists(dst):
            if sha256_file(dst) == e["sha256"]:
                counts["present"] += 1
                continue
            if not args.force:
                counts["blocked"] += 1
                print("  BLOCKED  %s: game/ holds a different version (--force to replace)"
                      % e["path"])
                if e["class"] == "required":
                    failed_required.append(e["path"])
                continue
        if e.get("extracted_from"):
            data, where, wrong = extracted(e, by_path[e["extracted_from"]],
                                           sources, derived)
        else:
            data, where, wrong = find(e, sources)
        if data is None:
            kind = "wrong" if wrong else "missing"
            counts[kind] += 1
            if e["class"] == "required" or args.verbose:
                print("  %-8s %s (%s)%s" % (kind.upper(), e["path"], e["class"],
                      "" if not wrong else " -- hash differs in " + ", ".join(wrong)))
            if e["class"] == "required":
                failed_required.append(e["path"])
            continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as fh:
            fh.write(data)
        counts["written"] += 1
        if args.verbose:
            print("  wrote    %s <- %s" % (e["path"], where))
    print("game/: %(written)d written, %(present)d already present, %(missing)d missing, "
          "%(wrong)d wrong version, %(blocked)d blocked" % counts)
    if failed_required:
        print("FAIL: %d required file(s) not imported" % len(failed_required))
        return 1
    return 0


def cmd_check(args):
    m = load_manifest()
    bad = 0
    for e in m["files"]:
        if e.get("source_only"):
            continue
        dst = os.path.join(GAME, *e["path"].split("/"))
        if not os.path.exists(dst):
            state = "missing"
        elif sha256_file(dst) != e["sha256"]:
            state = "wrong"
        else:
            continue
        if e["class"] == "required":
            bad += 1
            print("  %-8s %s" % (state.upper(), e["path"]))
        elif args.verbose:
            print("  %-8s %s (optional)" % (state, e["path"]))
    print("game/: %s" % ("OK" if not bad else "%d required file(s) missing or wrong" % bad))
    return 1 if bad else 0


# The manifest generator.  Which files belong, and their class, is decided
# here, once; the manifest is the reviewed output.

DATA_DIRS = ["bitmaps", "CDTracks", "fonts", "InstructionScripts",
             "Level3DExtraObjects", "Levels", "models", "textures", "themes",
             "video", "waves"]
DATA_FILES = ["ENGLISH.FIS", "JJ.GAM", "ProgableControl.sav"]
OPTIONAL_PREFIXES = ["CDTracks/Track ", "video/"]
# The original executable: never run, only read for its resources.  An
# install that has been cracked keeps it as Karoo.exe.bak.
EXE = {"path": "Karoo.exe", "from": ["Karoo.exe.orig", "Karoo.exe.bak"],
       "local": "Karoo.exe.orig"}


def cmd_manifest(args):
    root = args.install
    entries = []

    def add(rel, local=None, extra=None):
        full = os.path.join(root, local or rel)
        e = {"path": rel, "sha256": sha256_file(full),
             "size": os.path.getsize(full),
             "class": "optional" if any(rel.startswith(p) for p in OPTIONAL_PREFIXES)
                      else "required"}
        e.update(extra or {})
        entries.append(e)

    for d in DATA_DIRS:
        for dirpath, _, files in os.walk(os.path.join(root, d)):
            for f in files:
                add(os.path.relpath(os.path.join(dirpath, f), root).replace(os.sep, "/"))
    for f in DATA_FILES:
        add(f)
    add(EXE["path"], EXE["local"], {"from": EXE["from"], "source_only": True})
    with open(os.path.join(root, EXE["local"]), "rb") as fh:
        for stem, blob in extract_rsrc.extract(fh.read()).items():
            entries.append({"path": "res/%s.bin" % stem, "sha256": sha256_bytes(blob),
                            "size": len(blob), "class": "required",
                            "extracted_from": EXE["path"]})
    entries.sort(key=lambda e: e["path"].lower())
    with open(MANIFEST, "w") as fh:
        json.dump({"version": 1, "files": entries}, fh, indent=1)
        fh.write("\n")
    print("%s: %d files" % (os.path.relpath(MANIFEST, REPO), len(entries)))
    return 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "manifest":
        ap = argparse.ArgumentParser(prog="import_assets.py manifest")
        ap.add_argument("_cmd")
        ap.add_argument("install", help="an install directory to describe")
        return cmd_manifest(ap.parse_args())
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--from", dest="sources", action="append", default=[],
                    help="a source directory or zip (repeatable; first match wins)")
    ap.add_argument("--check", action="store_true", help="verify game/ only")
    ap.add_argument("--force", action="store_true",
                    help="replace files in game/ whose hash differs")
    ap.add_argument("--verbose", "-v", action="store_true")
    args = ap.parse_args()
    if args.check:
        return cmd_check(args)
    if not args.sources:
        ap.error("give at least one --from, or --check")
    return cmd_import(args)


if __name__ == "__main__":
    sys.exit(main())
