#!/usr/bin/env python3
"""Record a long playthrough as a series of recordings, one per sitting.

    python3 tools/playthrough.py [series]

A recording cannot span a game exit, so a playthrough is a series of parts,
`<series>-01`, `<series>-02`, ...  Each part bundles the save files it starts
from, so part 2 starts from exactly where part 1 left the saves and each part
replays on its own.  This script drives one sitting:

  1. asks whether to start a new series (empty SavedGames, a new game) or
     continue one (SavedGames restored to where the last part ended),
  2. asks what you plan to do this sitting,
  3. launches the game recording (via `replaytest.py record`),
  4. after you quit, snapshots the saves for the next sitting, asks how it
     went, catalogues the part in tests/manifest.json, and offers to commit.

The end-of-sitting saves are kept in run/playthroughs/<series>/<NN>/ (not in
git); the next part's recording bundles them into its header.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import replaytest as rt          # noqa: E402
import replay as recfmt          # noqa: E402

SNAPSHOTS = os.path.join(rt.REPO, "run", "playthroughs")


def ask(prompt, default=None):
    suffix = " [%s]" % default if default else ""
    try:
        ans = input("%s%s: " % (prompt, suffix)).strip()
    except EOFError:
        sys.exit("\nno input")
    return ans or default or ""


def yes(prompt, default=True):
    ans = ask("%s (%s)" % (prompt, "Y/n" if default else "y/N")).lower()
    return default if not ans else ans.startswith("y")


def parts_of(m, series):
    return sorted((r for r in m["recordings"] if r.get("playthrough") == series),
                  key=lambda r: r["name"])


def existing_series(m):
    return sorted({r["playthrough"] for r in m["recordings"] if r.get("playthrough")})


def clear_saves():
    os.makedirs(rt.SAVES_DIR, exist_ok=True)
    for name in recfmt.save_files(rt.SAVES_DIR):
        os.remove(os.path.join(rt.SAVES_DIR, name))


def restore_snapshot(snap):
    clear_saves()
    for name in os.listdir(snap):
        shutil.copy2(os.path.join(snap, name), rt.SAVES_DIR)


def snapshot_saves(snap):
    shutil.rmtree(snap, ignore_errors=True)
    os.makedirs(snap)
    for name in recfmt.save_files(rt.SAVES_DIR):
        shutil.copy2(os.path.join(rt.SAVES_DIR, name), snap)


def git(*args):
    return subprocess.run(["git", *args], cwd=rt.REPO).returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("series", nargs="?", help="series name (asked for if omitted)")
    args = ap.parse_args()

    m = rt.load_manifest()
    known = existing_series(m)

    series = args.series
    if not series:
        if known:
            print("Playthroughs:")
            for s in known:
                print("  %s (%d part(s))" % (s, len(parts_of(m, s))))
            series = ask("Series to continue, or a new name")
        else:
            series = ask("Name for the new playthrough")
    if not re.fullmatch(r"[A-Za-z0-9_-]+", series or ""):
        sys.exit("series names are letters, digits, - and _")

    parts = parts_of(m, series)
    num = len(parts) + 1
    name = "%s-%02d" % (series, num)

    if parts:
        snap = os.path.join(SNAPSHOTS, series, "%02d" % (num - 1))
        print("\nContinuing %s: this will be part %d, %s." % (series, num, name))
        print("Last part: %s" % parts[-1].get("description", ""))
        if os.path.isdir(snap):
            restore_snapshot(snap)
            print("SavedGames restored to the end of part %d." % (num - 1))
        else:
            print("WARNING: no saved end state for part %d (recorded on another "
                  "machine?)." % (num - 1))
            if not yes("Use whatever SavedGames holds now?", False):
                sys.exit("aborted")
        print("\nLoad your save from the game menu to carry on.")
    else:
        print("\nNew playthrough %s, part 1: SavedGames will be emptied "
              "(run/SavedGames only; the game files are untouched)." % series)
        if not yes("Go ahead?"):
            sys.exit("aborted")
        clear_saves()
        print("\nStart a new game from the menu.")

    plan = ask("\nWhat do you plan to do this sitting",
               "continue the playthrough" if parts else "start a new game")
    print()

    rt.build_once()
    ns = argparse.Namespace(name=name, force=False, dt=None, seed=None,
                            level=None, description=plan, scenario=None)
    rt.cmd_record(ns)          # launches the game; returns after you quit

    snapshot_saves(os.path.join(SNAPSHOTS, series, "%02d" % num))

    # cmd_record catalogued the part; add the series and what actually happened.
    m = rt.load_manifest()
    entry = next(r for r in m["recordings"] if r["name"] == name)
    outcome = ask("\nHow did it go (what you actually did)", plan)
    entry["playthrough"] = series
    entry["description"] = "%s part %d: %s" % (series, num, outcome)
    entry["scenario"] = plan
    entry.pop("level", None)
    rt.save_manifest(m)

    print("\nPart %d of %s recorded." % (num, series))
    if yes("Commit it?"):
        rec = os.path.relpath(os.path.join(rt.RECORDINGS, entry["file"]), rt.REPO)
        git("add", rec, os.path.relpath(rt.MANIFEST, rt.REPO))
        msg = ask("Commit message", "Record %s part %d: %s" % (series, num, outcome))
        git("commit", "-m", msg)
    print("Run again with `python3 tools/playthrough.py %s` for the next sitting." % series)


if __name__ == "__main__":
    main()
