#!/usr/bin/env python3
"""Ka'roo level table and save-slot editor.

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
"""

import argparse
import os
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

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
