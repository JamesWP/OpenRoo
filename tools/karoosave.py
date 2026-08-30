#!/usr/bin/env python3
r"""Ka'roo level table and save-slot editor.

Lets you jump a save slot straight to any level, so a given level can be
loaded for testing without playing up to it.

Both file formats are obfuscated with a per-byte additive cipher (the game
passes the key as a char literal to its load/save routines):

  JJ.GAM              key 5     — the level-name table
  SavedGames/jjN.sav  key 0x37  — one 42-byte save slot per file

JJ.GAM  (Game::SetCurrentLevelName @ 0x4186b0, Game::Load @ 0x4145c0)
    [0..2]  header
    [3]     level count            -> Game+0x4215e
    [4+]    count x 256-byte NUL-terminated level name
                                   -> Game+0x3215e + n*0x100
    A level name is a path under Levels/, e.g. "Forest\\Start".

jjN.sav (LoadSaveFile @ 0x43b4cc, writer @ 0x43b3d0, init @ 0x43b560)
    +0x00  char name[20]     slot name, "..........": empty slot
    +0x14  byte level        index into the JJ.GAM table   <- what we edit
    +0x15  byte              progress counter (bonus levels?), not decoded
    +0x16  dword             not decoded
    +0x1a  dword             not decoded
    +0x1e  dword             not decoded (looks like score)
    +0x22  dword             "slot in use" — 1 in every loadable slot, and
                             explicitly zeroed by the empty-slot initialiser
                             at 0x43b560. A slot with 0 here is ignored by
                             the menu: pressing Enter does nothing.
    +0x26  dword             not decoded

Both the level field and the in-use flag are CONFIRMED IN GAME (2026-08-30):
slot 4, seeded from slot 3 so that only the name and +0x14 differed, loaded
Egypt\Race exactly as set. They were originally derived from correspondence
across the shipped slots plus the empty-slot initialiser, not from the
reading code.

The remaining fields are still undecoded, so --seed-from stays the reliable
way to prepare a slot: it copies a known-good slot wholesale and changes only
the level and name, leaving every unknown field at a value the game has
already accepted.

The game reads all slots once at startup, so edit while it is NOT running.

For automated tests, do not re-derive a slot with `set` — snapshot the whole
SavedGames/ directory into a *fixture* and restore it byte for byte before each
run.  See "Save fixtures" below and REPLAY_PLAN.md Stage E.

    python3 tools/karoosave.py set 4 --match BombStart --seed-from 2
    python3 tools/karoosave.py snapshot tests/saves/bombstart
    python3 tools/karoosave.py restore  tests/saves/bombstart
"""

import argparse
import glob
import hashlib
import os
import shutil
import sys

GAM_KEY = 5
SAV_KEY = 0x37
SLOT_SIZE = 0x2A
NAME_LEN = 0x14
OFF_LEVEL = 0x14
OFF_IN_USE = 0x22
EMPTY_NAME = ".........."

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def decipher(data, key):
    return bytes((b - key) & 0xFF for b in data)


def encipher(data, key):
    return bytes((b + key) & 0xFF for b in data)


def load_levels(gam_path):
    """Return the list of level names from JJ.GAM, in index order."""
    with open(gam_path, "rb") as fh:
        d = decipher(fh.read(), GAM_KEY)
    count = d[3]
    names = []
    for i in range(count):
        entry = d[4 + i * 256: 4 + (i + 1) * 256]
        names.append(entry.split(b"\x00")[0].decode("latin1"))
    return names


def slot_path(saves_dir, slot):
    return os.path.join(saves_dir, "jj%d.sav" % slot)


def read_slot(path):
    with open(path, "rb") as fh:
        raw = fh.read()
    if len(raw) != SLOT_SIZE:
        raise ValueError("%s: expected %d bytes, got %d" % (path, SLOT_SIZE, len(raw)))
    return bytearray(decipher(raw, SAV_KEY))


def write_slot(path, plain):
    if len(plain) != SLOT_SIZE:
        raise ValueError("slot must be %d bytes" % SLOT_SIZE)
    with open(path, "wb") as fh:
        fh.write(encipher(bytes(plain), SAV_KEY))


def slot_name(plain):
    return plain[:NAME_LEN].split(b"\x00")[0].decode("latin1")


def set_slot_name(plain, name):
    encoded = name.encode("latin1")[:NAME_LEN - 1]
    plain[:NAME_LEN] = encoded.ljust(NAME_LEN, b"\x00")


def cmd_levels(args):
    names = load_levels(args.gam)
    print("%d levels in %s\n" % (len(names), args.gam))
    for i, n in enumerate(names):
        if args.filter and args.filter.lower() not in n.lower():
            continue
        print("%3d  %s" % (i, n))


def cmd_show(args):
    names = load_levels(args.gam)
    print("%-22s %-12s %-4s %s" % ("FILE", "NAME", "LVL", "LEVEL NAME"))
    for slot in range(args.slots):
        path = slot_path(args.saves, slot)
        if not os.path.exists(path):
            print("%-22s <missing>" % path)
            continue
        plain = read_slot(path)
        name = slot_name(plain)
        lvl = plain[OFF_LEVEL]
        lname = names[lvl] if lvl < len(names) else "<out of range>"
        if name == EMPTY_NAME:
            name = "<empty>"
        print("%-22s %-12s %-4d %s" % (path, name, lvl, lname))


def cmd_set(args):
    names = load_levels(args.gam)

    level = args.level
    if level is None:
        matches = [i for i, n in enumerate(names) if args.match.lower() in n.lower()]
        if not matches:
            sys.exit("no level matches %r" % args.match)
        if len(matches) > 1:
            print("ambiguous %r, matches:" % args.match, file=sys.stderr)
            for i in matches:
                print("  %3d  %s" % (i, names[i]), file=sys.stderr)
            sys.exit(1)
        level = matches[0]

    if not 0 <= level < len(names):
        sys.exit("level %d out of range 0..%d" % (level, len(names) - 1))

    path = slot_path(args.saves, args.slot)
    plain = read_slot(path)

    if args.seed_from is not None:
        if args.seed_from == args.slot:
            sys.exit("--seed-from must name a different slot")
        src = slot_path(args.saves, args.seed_from)
        seed = read_slot(src)
        if seed[OFF_IN_USE] == 0:
            sys.exit("slot %d is itself unused (in-use flag is 0); "
                     "seed from a slot the game can already load" % args.seed_from)
        print("seeding from %s (level %d, name %r)"
              % (src, seed[OFF_LEVEL], slot_name(seed)))
        plain = seed

    if not args.no_backup:
        backup = path + ".bak"
        if not os.path.exists(backup):
            with open(path, "rb") as src, open(backup, "wb") as dst:
                dst.write(src.read())
            print("backed up %s -> %s" % (path, backup))

    old = plain[OFF_LEVEL]
    plain[OFF_LEVEL] = level
    if slot_name(plain) == EMPTY_NAME or args.name:
        set_slot_name(plain, args.name or "TEST")

    was_unused = plain[OFF_IN_USE] == 0
    if was_unused:
        plain[OFF_IN_USE] = 1
        print("slot was marked unused; setting the in-use flag at +0x%02x" % OFF_IN_USE)

    write_slot(path, plain)

    old_name = names[old] if old < len(names) else "<out of range>"
    print("%s: level %d (%s) -> %d (%s)  slot name %r"
          % (path, old, old_name, level, names[level], slot_name(plain)))
    if was_unused and args.seed_from is None:
        print("NOTE: this slot was empty and several record fields are still\n"
              "      undecoded. If the menu will not load it, re-run with\n"
              "      --seed-from <a slot that loads>.")
    print("Start the game, pick this save slot, and it should load that level.")


# ── Save fixtures ─────────────────────────────────────────────────────────
#
# A recording that navigates the menu to "load slot N" only lands on the level
# it was recorded against if slot N holds exactly the bytes it held then.
# `set --seed-from` gets you there by hand, but it is not reproducible: it
# depends on which slot you seeded from and on whatever that slot happened to
# contain that day.
#
# A *fixture* removes the guesswork by storing the bytes themselves.  It is a
# directory holding a byte-for-byte copy of every file in SavedGames/ plus a
# FIXTURE manifest recording each file's SHA-256 and, for context, the decoded
# slot table.  `restore` copies it back verbatim and deletes any save file the
# fixture does not name, so no stale slot can survive into the run.
#
# This is deliberately the "copy a known-good record" approach CLAUDE.md asks
# for: it needs none of the still-undecoded save fields to be understood, and
# it stays correct if they are later decoded differently.
#
# JJ.GAM is NOT part of the fixture — it is committed game data, shared by every
# fixture.  But a slot stores a level *index* into it, so a JJ.GAM edit would
# silently repoint every fixture at a different level.  Its hash is therefore
# recorded and checked on restore.

FIXTURE_FILE = "FIXTURE"
SAVE_GLOBS = ("jj*.sav", "JJ*.sav", "GAM.DAT")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def save_files(saves_dir):
    """Every file the game reads out of SavedGames/, sorted, basenames only."""
    found = set()
    for pat in SAVE_GLOBS:
        for p in glob.glob(os.path.join(saves_dir, pat)):
            if os.path.isfile(p):
                found.add(os.path.basename(p))
    return sorted(found)


def read_fixture(fixture_dir):
    """Parse a fixture's FIXTURE manifest -> (gam_sha or None, {name: sha})."""
    path = os.path.join(fixture_dir, FIXTURE_FILE)
    if not os.path.exists(path):
        sys.exit("%s: not a save fixture (no %s)" % (fixture_dir, FIXTURE_FILE))
    gam, files = None, {}
    for line in open(path):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        key, _, val = line.partition(" ")
        if key == "jj.gam":
            gam = val.strip()
        elif key == "file":
            digest, _, name = val.strip().partition(" ")
            files[name.strip()] = digest
    return gam, files


def cmd_snapshot(args):
    names = load_levels(args.gam)
    files = save_files(args.saves)
    if not files:
        sys.exit("%s: no save files to snapshot" % args.saves)

    os.makedirs(args.fixture, exist_ok=True)
    slots = []
    lines = ["# Ka'roo save fixture — restore with:",
             "#  python3 tools/karoosave.py restore %s" % args.fixture,
             "# Created from %s" % args.saves,
             "",
             "jj.gam %s" % sha256(args.gam),
             ""]

    for name in files:
        src = os.path.join(args.saves, name)
        shutil.copyfile(src, os.path.join(args.fixture, name))
        lines.append("file %s %s" % (sha256(src), name))

    lines.append("")
    lines.append("# Decoded slot table at snapshot time (context only; the bytes above")
    lines.append("# are what is restored):")
    for name in files:
        if not name.lower().startswith("jj") or not name.lower().endswith(".sav"):
            continue
        try:
            plain = read_slot(os.path.join(args.saves, name))
        except ValueError as e:
            slots.append("#   %-10s <unreadable: %s>" % (name, e))
            continue
        lvl = plain[OFF_LEVEL]
        lname = names[lvl] if lvl < len(names) else "<out of range>"
        slots.append("#   %-10s name=%-12r in_use=%d level=%-3d %s"
                    % (name, slot_name(plain), plain[OFF_IN_USE], lvl, lname))

    with open(os.path.join(args.fixture, FIXTURE_FILE), "w") as fh:
        fh.write("\n".join(lines + slots) + "\n")

    print("snapshot -> %s  (%d files)" % (args.fixture, len(files)))
    for line in slots:
        print(" " + line[1:])


def cmd_restore(args):
    gam_sha, files = read_fixture(args.fixture)
    if not files:
        sys.exit("%s: fixture names no files" % args.fixture)

    have = sha256(args.gam)
    if gam_sha and have != gam_sha:
        msg = ("JJ.GAM has changed since this fixture was taken\n"
               "  fixture: %s\n  current: %s\n"
               "Save slots store a level *index* into JJ.GAM, so the restored "
               "slots may now point at different levels.\n"
               "Re-record the fixture, or pass --force if you know the table is "
               "compatible." % (gam_sha, have))
        if not args.force:
            sys.exit("ERROR: " + msg)
        print("WARNING: " + msg)

    os.makedirs(args.saves, exist_ok=True)

    # Remove any save the fixture does not name: a leftover slot from an
    # earlier test would still be visible in the menu and could be the one a
    # recording's keypresses land on.
    removed = [n for n in save_files(args.saves) if n not in files]
    for name in removed:
        os.remove(os.path.join(args.saves, name))

    for name, digest in sorted(files.items()):
        src = os.path.join(args.fixture, name)
        if not os.path.exists(src):
            sys.exit("%s: fixture is incomplete, %s is missing" % (args.fixture, name))
        if sha256(src) != digest:
            sys.exit("%s: %s does not match its recorded hash — fixture is corrupt"
                     % (args.fixture, name))
        shutil.copyfile(src, os.path.join(args.saves, name))

    print("restored %d file(s) from %s -> %s%s"
          % (len(files), args.fixture, args.saves,
             "" if not removed else "  (removed %s)" % ", ".join(removed)))


def cmd_verify(args):
    """Is SavedGames/ currently exactly what the fixture says? (exit 1 if not)"""
    gam_sha, files = read_fixture(args.fixture)
    bad = []
    if gam_sha and sha256(args.gam) != gam_sha:
        bad.append("JJ.GAM differs from the fixture")
    for name, digest in sorted(files.items()):
        live = os.path.join(args.saves, name)
        if not os.path.exists(live):
            bad.append("%s is missing" % name)
        elif sha256(live) != digest:
            bad.append("%s differs" % name)
    for name in save_files(args.saves):
        if name not in files:
            bad.append("%s is present but not in the fixture" % name)
    if bad:
        print("MISMATCH against %s:" % args.fixture)
        for b in bad:
            print("  %s" % b)
        return 1
    print("SavedGames matches %s" % args.fixture)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gam", default=os.path.join(REPO, "JJ.GAM"))
    ap.add_argument("--saves", default=os.path.join(REPO, "SavedGames"))
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("levels", help="list every level with its index")
    p.add_argument("filter", nargs="?", help="only show names containing this")
    p.set_defaults(func=cmd_levels)

    p = sub.add_parser("show", help="show each save slot's current level")
    p.add_argument("--slots", type=int, default=6)
    p.set_defaults(func=cmd_show)

    p = sub.add_parser("set", help="point a save slot at a level")
    p.add_argument("slot", type=int, help="save slot 0..5")
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--level", type=int, help="level index")
    g.add_argument("--match", help="substring of the level name, e.g. Egypt\\\\Race")
    p.add_argument("--name", help="also set the slot name")
    p.add_argument("--seed-from", type=int, metavar="SLOT",
                   help="copy this loadable slot wholesale first, so every "
                        "undecoded field keeps a value the game accepts")
    p.add_argument("--no-backup", action="store_true")
    p.set_defaults(func=cmd_set)

    p = sub.add_parser("snapshot",
                       help="copy SavedGames/ into a reusable save fixture")
    p.add_argument("fixture", help="directory to write, e.g. tests/saves/bombstart")
    p.set_defaults(func=cmd_snapshot)

    p = sub.add_parser("restore",
                       help="restore SavedGames/ from a fixture, byte for byte")
    p.add_argument("fixture")
    p.add_argument("--force", action="store_true",
                   help="restore even if JJ.GAM has changed since the snapshot")
    p.set_defaults(func=cmd_restore)

    p = sub.add_parser("verify",
                       help="check SavedGames/ still matches a fixture (exit 1 if not)")
    p.add_argument("fixture")
    p.set_defaults(func=cmd_verify)

    args = ap.parse_args()
    sys.exit(args.func(args) or 0)


if __name__ == "__main__":
    main()
