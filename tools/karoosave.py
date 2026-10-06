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

jjN.sav  --  one 42-byte SaveSlot record per file, fully decoded
    Writer  SaveSlotTable::WriteAllSaveSlotFiles        @ 0x43b3d0
    Reader  SaveSlotTable::LoadSaveFile                 @ 0x43b4a0
    Empty   SaveSlotTable::InitialiseEmptySaveSlotTable @ 0x43b560
    Save    Game::StoreGameStateIntoSaveSlot            @ 0x419de0
    Load    Game::RestoreGameStateFromSaveSlot          @ 0x419e50

    off   size  field                    live Game field
    ----  ----  -----------------------  ---------------------------------
    0x00  20    szSlotName               menu text; ".........." = empty
    0x14  1     bLevelIndex              Game+0x173583, index into JJ.GAM
    0x15  1     bLivesRemaining          Game+0x175402 (low byte)
    0x16  4     dwTotalScore             Game+0x1753f5, running total
    0x1a  4     dwCompletionNumerator    Game+0x1752a1
    0x1e  4     dwElapsedGameTime        (int)Game+0x170a44, the double
                                         game-time accumulator
    0x22  4     dwSlotInUse              1 = loadable; 0 = menu ignores it
    0x26  4     dwUnusedTail             never read or written by the game

    The in-memory table is Game+0x170a7c: a byte slot count at +0x30 then
    SaveSlot[count] at +0x31, so slot N field F is Game+0x170aad + N*0x2a + F.
    Game::Load sets the count to 6.

    bLevelIndex asymmetry, straight from the two routines: the *saver* stores
    Game+0x173583 + 1 (you save having finished level N, so the slot names the
    level to resume at), while the *loader* copies the byte back unchanged.
    So the value in the file is the level that will be played -- write the
    JJ.GAM index you want directly, no +/-1.

    dwCompletionNumerator is the only field whose meaning is not pinned down.
    In the shipped binary Game+0x1752a1 is written in exactly two places:
    zeroed by SetupLevelObjects @0x416fe9, and restored here.  Nothing
    increments it.  It is read once, in GameTick @0x4151ba, as the numerator
    of the level-completion percentage (clamped to 100 at Game+0x170a64) that
    CalculateLevelScore @0x41a760 turns into a bonus.  So the game itself
    always saves 0 there during a real playthrough, and 0 is what this tool
    writes.  (The nonzero values in the shipped jj0/jj1/jj2 slots -- 86, 145,
    76 -- have no code path that could have produced them in this build.)

    dwUnusedTail has no reference anywhere in the binary; it is written as 0.

Because every field is decoded, a slot can be synthesised from nothing:

    python3 tools/karoosave.py write --slot 0 --match 'Egypt\\Race' --name TEST

The game reads all slots once at startup, so edit while it is NOT running.

A test recording carries the SavedGames/ files it starts from (see
tools/replaytest.py record), so build them with `write --all` first.
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
OFF_LIVES = 0x15
OFF_SCORE = 0x16
OFF_COMPLETION = 0x1A
OFF_TIME = 0x1E
OFF_IN_USE = 0x22
OFF_UNUSED = 0x26
EMPTY_NAME = ".........."

# Field order must match the struct above; used for pack/unpack and display.
SLOT_FIELDS = (
    ("level", OFF_LEVEL, 1),
    ("lives", OFF_LIVES, 1),
    ("score", OFF_SCORE, 4),
    ("completion", OFF_COMPLETION, 4),
    ("time", OFF_TIME, 4),
    ("in_use", OFF_IN_USE, 4),
    ("unused", OFF_UNUSED, 4),
)

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


def build_slot(name, level, lives=3, score=0, completion=0, time=0, in_use=1,
               unused=0):
    """A complete 42-byte slot record, built from nothing.

    Every field is decoded (see the module docstring), so there is no need to
    copy an existing slot to keep unknown bytes at a value the game accepts.
    """
    plain = bytearray(SLOT_SIZE)
    set_slot_name(plain, name)
    values = dict(level=level, lives=lives, score=score, completion=completion,
                  time=time, in_use=in_use, unused=unused)
    for field, off, size in SLOT_FIELDS:
        v = values[field]
        if not 0 <= v < (1 << (8 * size)):
            raise ValueError("%s=%d does not fit in %d byte(s)" % (field, v, size))
        plain[off:off + size] = v.to_bytes(size, "little")
    return plain


def empty_slot():
    """What InitialiseEmptySaveSlotTable @0x43b560 produces, minus its garbage.

    The game leaves dwTotalScore and dwUnusedTail untouched when it empties a
    slot; we zero them, which the menu cannot distinguish since it stops at
    dwSlotInUse == 0.
    """
    return build_slot(EMPTY_NAME, level=0, lives=0, in_use=0)


def unpack_slot(plain):
    return {field: int.from_bytes(plain[off:off + size], "little")
            for field, off, size in SLOT_FIELDS}


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


def resolve_level(names, level, match):
    """--level N or --match SUBSTRING -> a validated level index."""
    if level is None:
        hits = [i for i, n in enumerate(names) if match.lower() in n.lower()]
        if not hits:
            sys.exit("no level matches %r" % match)
        if len(hits) > 1:
            print("ambiguous %r, matches:" % match, file=sys.stderr)
            for i in hits:
                print("  %3d  %s" % (i, names[i]), file=sys.stderr)
            sys.exit(1)
        level = hits[0]
    if not 0 <= level < len(names):
        sys.exit("level %d out of range 0..%d" % (level, len(names) - 1))
    return level


def cmd_show(args):
    names = load_levels(args.gam)
    hdr = ("FILE", "NAME", "USE", "LVL", "LIVES", "SCORE", "TIME", "LEVEL NAME")
    print("%-22s %-12s %3s %4s %5s %8s %10s  %s" % hdr)
    for slot in range(args.slots):
        path = slot_path(args.saves, slot)
        if not os.path.exists(path):
            print("%-22s <missing>" % path)
            continue
        plain = read_slot(path)
        f = unpack_slot(plain)
        name = slot_name(plain)
        lname = names[f["level"]] if f["level"] < len(names) else "<out of range>"
        print("%-22s %-12s %3d %4d %5d %8d %10d  %s"
              % (path, "<empty>" if name == EMPTY_NAME else name,
                 f["in_use"], f["level"], f["lives"], f["score"], f["time"],
                 lname))
        if args.verbose:
            print("%-22s   completion=%d unused=0x%08x"
                  % ("", f["completion"], f["unused"]))


def cmd_write(args):
    """Synthesise save files from scratch -- no existing slot is read."""
    names = load_levels(args.gam)
    level = resolve_level(names, args.level, args.match)

    os.makedirs(args.saves, exist_ok=True)
    target = build_slot(args.name, level=level, lives=args.lives,
                        score=args.score, time=args.time)

    # With --all the directory must be exactly what we wrote: a leftover save
    # from an earlier test (or a differently-cased duplicate such as JJ3.sav)
    # is still visible to the game and could be the slot a recording lands on.
    removed = []
    if args.all:
        keep = {os.path.basename(slot_path(args.saves, n)) for n in range(args.slots)}
        for name in save_files(args.saves):
            if name not in keep and name != "GAM.DAT":
                os.remove(os.path.join(args.saves, name))
                removed.append(name)

    written = []
    for n in range(args.slots):
        if n != args.slot and not args.all:
            continue
        plain = target if n == args.slot else empty_slot()
        write_slot(slot_path(args.saves, n), plain)
        written.append(n)

    for n in written:
        f = unpack_slot(read_slot(slot_path(args.saves, n)))
        lname = names[f["level"]] if f["level"] < len(names) else "<out of range>"
        print("%s: %s" % (slot_path(args.saves, n),
                          "<empty>" if f["in_use"] == 0 else
                          "name=%r level=%d (%s) lives=%d score=%d time=%d"
                          % (slot_name(read_slot(slot_path(args.saves, n))),
                             f["level"], lname, f["lives"], f["score"], f["time"])))
    if removed:
        print("removed %s (not part of a %d-slot set)"
              % (", ".join(removed), args.slots))
    if not args.all:
        print("NOTE: only slot %d was written; the other slots are whatever was\n"
              "      there before. Pass --all to rewrite the whole directory."
              % args.slot)
    print("Start the game, pick slot %d, and it should load %s."
          % (args.slot, names[level]))


def cmd_set(args):
    names = load_levels(args.gam)
    level = resolve_level(names, args.level, args.match)

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
    print("Start the game, pick this save slot, and it should load that level.")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--gam", default=os.path.join(REPO, "game", "JJ.GAM"))
    ap.add_argument("--saves", default=os.path.join(REPO, "run", "SavedGames"))
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("levels", help="list every level with its index")
    p.add_argument("filter", nargs="?", help="only show names containing this")
    p.set_defaults(func=cmd_levels)

    p = sub.add_parser("show", help="show every decoded field of each slot")
    p.add_argument("--slots", type=int, default=6)
    p.add_argument("-v", "--verbose", action="store_true",
                   help="also show completion and the unused tail dword")
    p.set_defaults(func=cmd_show)

    p = sub.add_parser("write",
                       help="build save files from scratch (no slot is copied)")
    p.add_argument("--slot", type=int, default=0, help="slot to populate, 0..5")
    p.add_argument("--slots", type=int, default=6,
                   help="how many slots the game expects (Game::Load sets 6)")
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--level", type=int, help="level index")
    g.add_argument("--match", help="substring of the level name")
    p.add_argument("--name", default="TEST", help="slot name shown in the menu")
    p.add_argument("--lives", type=int, default=3)
    p.add_argument("--score", type=int, default=0)
    p.add_argument("--time", type=int, default=0,
                   help="elapsed game-time accumulator")
    p.add_argument("--all", action="store_true",
                   help="also write every other slot as an empty slot, so the "
                        "whole SavedGames/ directory is reproducible")
    p.set_defaults(func=cmd_write)

    p = sub.add_parser("set", help="point a save slot at a level")
    p.add_argument("slot", type=int, help="save slot 0..5")
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--level", type=int, help="level index")
    g.add_argument("--match", help="substring of the level name, e.g. Egypt\\\\Race")
    p.add_argument("--name", help="also set the slot name")
    p.add_argument("--seed-from", type=int, metavar="SLOT",
                   help="DEPRECATED: copy this slot wholesale first. Every "
                        "field is decoded now, so `write` is the better tool")
    p.add_argument("--no-backup", action="store_true")
    p.set_defaults(func=cmd_set)





    args = ap.parse_args()
    sys.exit(args.func(args) or 0)


if __name__ == "__main__":
    main()
