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

For automated tests, do not re-derive a slot with `set` — snapshot the whole
SavedGames/ directory into a *fixture* and restore it byte for byte before each
run.  See "Save fixtures" below and REPLAY_PLAN.md Stage E.

    python3 tools/karoosave.py write --slot 4 --match BombStart --name TEST
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


# ── Save fixtures ─────────────────────────────────────────────────────────
#
# A recording that navigates the menu to "load slot N" only lands on the level
# it was recorded against if SavedGames/ holds exactly the state it held then.
# So a recording does not describe its save state, it **carries** it, in a
# fixture directory named by tests/manifest.json.
#
# A fixture is a single declarative FIXTURE file: for each save file, the
# decoded SaveSlot fields, plus the SHA-256 of the bytes those fields must
# produce.  `restore` SYNTHESISES each file from the fields (build_slot) and
# then checks the result against the recorded hash, failing hard on any
# mismatch.  Nothing is copied.
#
# The hash is the whole point of the design.  Generating from fields makes a
# fixture readable and diffable, but it also makes it depend on this tool: a
# bug or a changed default in build_slot would silently alter what every
# recording loads, and the symptom would be a replay diverging thousands of
# frames later rather than an obvious tooling error.  The hash turns that into
# a hard error at restore time, before the game is ever launched.  Do not
# "fix" a hash mismatch by re-snapshotting until you know which side is wrong.
#
# JJ.GAM is NOT part of a fixture — it is committed game data, shared by every
# fixture.  But a slot stores a level *index* into it, so a JJ.GAM edit would
# silently repoint every fixture at a different level.  Its hash is therefore
# recorded and checked on restore.
#
# Version 3 keeps only what a recording needs from a slot: name, level and
# lives.  Score, completion, time and the unused tail are written as a real
# save writes them for a fresh game (0, in use 1), so no played history is
# carried; an empty slot is the word `empty`.  The sha stays: it is the hash
# of bytes this tool generates, and it is what catches a changed build_slot.
# `convert` rewrites a v2 fixture as v3, zeroing the dropped fields.
#
# Filenames are recorded per slot rather than derived.  The shipped set has
# slot 3 on disk as "JJ3.sav", uppercase, with no lowercase counterpart: Wine
# finds it case-insensitively, but this tool must reproduce the name it saw.

FIXTURE_FILE = "FIXTURE"
FIXTURE_VERSION = 3
# The fields a v3 fixture stores; the rest take TRIMMED values.
KEPT_FIELDS = ("level", "lives")
TRIMMED = dict(score=0, completion=0, time=0, in_use=1, unused=0)
SAVE_GLOBS = ("jj*.sav", "JJ*.sav", "GAM.DAT")


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256(path):
    with open(path, "rb") as fh:
        return sha256_bytes(fh.read())


def save_files(saves_dir):
    """Every file the game reads out of SavedGames/, sorted, basenames only."""
    found = set()
    for pat in SAVE_GLOBS:
        for p in glob.glob(os.path.join(saves_dir, pat)):
            if os.path.isfile(p):
                found.add(os.path.basename(p))
    return sorted(found)


class Fixture:
    """A parsed FIXTURE spec.

    slots   [{index, file, sha, name, <SLOT_FIELDS>}]  synthesised records
    blobs   [{file, sha, size}]                        non-slot files, empty only
    legacy  {name: sha}                                v1 fixture, bytes stored
    """

    def __init__(self, gam_sha=None, slots=None, blobs=None, legacy=None):
        self.version = None
        self.gam_sha = gam_sha
        self.slots = slots or []
        self.blobs = blobs or []
        self.legacy = legacy or {}

    def filenames(self):
        return ([s["file"] for s in self.slots] + [b["file"] for b in self.blobs]
                + sorted(self.legacy))

    def generate(self):
        """{filename: enciphered bytes} for every file this fixture declares.

        Raises ValueError naming the file if a generated record does not match
        the hash the fixture recorded for it.
        """
        out = {}
        for s in self.slots:
            plain = build_slot(s["name"], **{f: s[f] for f, _, _ in SLOT_FIELDS})
            data = encipher(bytes(plain), SAV_KEY)
            got = sha256_bytes(data)
            if got != s["sha"]:
                raise ValueError(
                    "%s: the fields in the fixture generate bytes that do not "
                    "match the recorded hash\n"
                    "    fixture sha: %s\n  generated sha: %s\n"
                    "  Either the fields were edited without re-snapshotting, "
                    "or build_slot no longer encodes a slot the same way.\n"
                    "  Work out which before touching the fixture."
                    % (s["file"], s["sha"], got))
            out[s["file"]] = data
        for b in self.blobs:
            data = b"\x00" * b["size"]
            got = sha256_bytes(data)
            if got != b["sha"]:
                raise ValueError("%s: %d zero bytes hash to %s, fixture says %s"
                                 % (b["file"], b["size"], got, b["sha"]))
            out[b["file"]] = data
        return out


def _parse_kv(rest):
    """`k=v k=v ... name="quoted"` -> dict of strings."""
    out, i, n = {}, 0, len(rest)
    while i < n:
        while i < n and rest[i].isspace():
            i += 1
        if i >= n:
            break
        eq = rest.index("=", i)
        key = rest[i:eq].strip()
        i = eq + 1
        if i < n and rest[i] == '"':
            j = i + 1
            buf = []
            while rest[j] != '"':
                if rest[j] == "\\":
                    j += 1
                buf.append(rest[j])
                j += 1
            out[key] = "".join(buf)
            i = j + 1
        else:
            j = i
            while j < n and not rest[j].isspace():
                j += 1
            out[key] = rest[i:j]
            i = j
    return out


def read_fixture(fixture_dir):
    """Parse a fixture directory's FIXTURE file -> Fixture."""
    path = os.path.join(fixture_dir, FIXTURE_FILE)
    if not os.path.exists(path):
        sys.exit("%s: not a save fixture (no %s)" % (fixture_dir, FIXTURE_FILE))

    fx = Fixture()
    for lineno, line in enumerate(open(path), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        key, _, rest = line.partition(" ")
        rest = rest.strip()
        try:
            if key == "version":
                fx.version = int(rest)
                if fx.version not in (2, FIXTURE_VERSION):
                    sys.exit("%s: fixture version %s, this tool reads 2 and %d"
                             % (path, rest, FIXTURE_VERSION))
            elif key == "jj.gam":
                fx.gam_sha = rest
            elif key == "slot":
                idx, _, rest = rest.partition(" ")
                fname, _, rest = rest.strip().partition(" ")
                rest = rest.strip()
                empty = rest.endswith(" empty")
                kv = _parse_kv(rest[:-len(" empty")] if empty else rest)
                slot = {"index": int(idx), "file": fname, "sha": kv["sha"]}
                if empty:
                    slot["name"] = EMPTY_NAME
                    slot.update(level=0, lives=0, score=0, completion=0,
                                time=0, in_use=0, unused=0)
                else:
                    slot["name"] = kv["name"]
                    for field, _, _ in SLOT_FIELDS:
                        if field in kv:
                            slot[field] = int(kv[field])
                        elif fx.version >= 3 and field in TRIMMED:
                            slot[field] = TRIMMED[field]
                        else:
                            raise KeyError(field)
                fx.slots.append(slot)
            elif key == "blob":
                fname, _, rest = rest.partition(" ")
                kv = _parse_kv(rest.strip())
                size = int(kv["size"])
                if size != 0:
                    sys.exit("%s:%d: blob %s is %d bytes; only empty non-slot "
                             "files can be generated" % (path, lineno, fname, size))
                fx.blobs.append({"file": fname, "sha": kv["sha"], "size": size})
            elif key == "file":
                # v1 fixture: `file <sha> <name>`, bytes stored alongside.
                digest, _, name = rest.partition(" ")
                fx.legacy[name.strip()] = digest
        except (KeyError, ValueError, IndexError) as e:
            sys.exit("%s:%d: malformed %s line (%s)" % (path, lineno, key, e))

    if fx.legacy and (fx.slots or fx.blobs):
        sys.exit("%s: mixes v1 `file` lines with v2 `slot`/`blob` lines" % path)
    return fx


def trim_slot(s):
    """Zero what a v3 fixture does not store, and re-hash the result."""
    if s["in_use"]:
        s.update(TRIMMED)
    s["sha"] = sha256_bytes(encipher(bytes(
        build_slot(s["name"], **{f: s[f] for f, _, _ in SLOT_FIELDS})), SAV_KEY))


def write_fixture(fixture, gam_sha, slots, blobs, names):
    out = [
        "# Ka'roo save fixture — generated, then hash-checked.",
        "#",
        "# `restore` SYNTHESISES each file below from its fields and refuses to",
        "# continue if the bytes do not hash to the recorded sha. Editing a field",
        "# without re-snapshotting is a hard error, not a silent change.",
        "#",
        "#   python3 tools/karoosave.py restore %s" % fixture,
        "",
        "version %d" % FIXTURE_VERSION,
        "jj.gam %s" % gam_sha,
        "",
    ]
    for s in slots:
        lvl = s["level"]
        out.append("# slot %d — %s"
                   % (s["index"],
                      "<empty>" if not s["in_use"] else
                      (names[lvl] if lvl < len(names) else "<level out of range>")))
        if not s["in_use"]:
            out.append("slot %d %s sha=%s empty" % (s["index"], s["file"], s["sha"]))
            continue
        out.append('slot %d %s sha=%s name="%s" %s'
                   % (s["index"], s["file"], s["sha"],
                      s["name"].replace("\\", "\\\\").replace('"', '\\"'),
                      " ".join("%s=%d" % (f, s[f]) for f in KEPT_FIELDS)))
    if blobs:
        out.append("")
        out.append("# Non-slot files. Only empty ones can be generated.")
        for b in blobs:
            out.append("blob %s sha=%s size=%d" % (b["file"], b["sha"], b["size"]))
    with open(os.path.join(fixture, FIXTURE_FILE), "w") as fh:
        fh.write("\n".join(out) + "\n")


def cmd_convert(args):
    """Rewrite a v2 fixture as v3: drop, and zero, the played-history fields."""
    fx = read_fixture(args.fixture)
    if fx.legacy:
        sys.exit("%s: a v1 fixture; snapshot it first" % args.fixture)
    for s in fx.slots:
        if s["in_use"] == 0 and (s["name"], s["level"], s["lives"]) != (EMPTY_NAME, 0, 0):
            sys.exit("%s: %s is not in use but is not the empty slot"
                     % (args.fixture, s["file"]))
        trim_slot(s)
    write_fixture(args.fixture, fx.gam_sha, fx.slots, fx.blobs, load_levels(args.gam))
    read_fixture(args.fixture).generate()
    print("converted %s to version %d" % (args.fixture, FIXTURE_VERSION))


def cmd_snapshot(args):
    names = load_levels(args.gam)
    files = save_files(args.saves)
    if not files:
        sys.exit("%s: no save files to snapshot" % args.saves)

    os.makedirs(args.fixture, exist_ok=True)
    slots, blobs = [], []

    for name in sorted(files):
        raw = open(os.path.join(args.saves, name), "rb").read()
        if len(raw) == SLOT_SIZE:
            plain = bytearray(decipher(raw, SAV_KEY))
            entry = {"file": name, "sha": sha256_bytes(raw),
                     "name": slot_name(plain)}
            entry.update(unpack_slot(plain))
            # Slot index comes from the filename: jjN.sav / JJN.sav.
            digits = "".join(c for c in os.path.splitext(name)[0] if c.isdigit())
            entry["index"] = int(digits) if digits else len(slots)
            slots.append(entry)
        elif len(raw) == 0:
            blobs.append({"file": name, "sha": sha256_bytes(raw), "size": 0})
        else:
            sys.exit("%s: %s is %d bytes — neither a %d-byte slot nor empty, so "
                     "it cannot be generated. This fixture format only supports "
                     "save slots." % (args.fixture, name, len(raw), SLOT_SIZE))

    slots.sort(key=lambda s: s["index"])
    played = [s["file"] for s in slots if s["in_use"] and any(
        s[f] != v for f, v in TRIMMED.items())]
    if played and not args.trim:
        sys.exit("%s: %s carry score/completion/time/unused; a fixture stores "
                 "only name, level and lives.  Pass --trim to zero them (and "
                 "check the recording still passes)." % (args.fixture, ", ".join(played)))
    for s in slots:
        trim_slot(s)
    write_fixture(args.fixture, sha256(args.gam), slots, blobs, names)

    # A v1 fixture kept the bytes alongside; a v2 one must not, or `restore`
    # would look reproducible while stale copies sat there being ignored.
    stale = save_files(args.fixture)
    for n in stale:
        os.remove(os.path.join(args.fixture, n))

    # Prove the spec just written regenerates exactly what was read.
    fx = read_fixture(args.fixture)
    try:
        gen = fx.generate()
    except ValueError as e:
        sys.exit("ERROR: fixture does not round-trip: %s" % e)
    for name in files:
        if name in played:
            continue
        if gen[name] != open(os.path.join(args.saves, name), "rb").read():
            sys.exit("ERROR: %s regenerates to different bytes" % name)

    print("snapshot -> %s  (%d slot(s), %d blob(s)%s)"
          % (args.fixture, len(slots), len(blobs),
             ", removed %d stored copy(s)" % len(stale) if stale else ""))
    for s in slots:
        lvl = s["level"]
        lname = names[lvl] if lvl < len(names) else "<out of range>"
        print("   %-10s name=%-12r in_use=%d level=%-3d %s"
              % (s["file"], s["name"], s["in_use"], lvl, lname))
    print("   regenerates byte-for-byte from the fields above%s"
          % (" (trimmed: %s)" % ", ".join(played) if played else ""))


def cmd_restore(args):
    fx = read_fixture(args.fixture)
    if not fx.filenames():
        sys.exit("%s: fixture names no files" % args.fixture)

    have = sha256(args.gam)
    if fx.gam_sha and have != fx.gam_sha:
        msg = ("JJ.GAM has changed since this fixture was taken\n"
               "  fixture: %s\n  current: %s\n"
               "Save slots store a level *index* into JJ.GAM, so the restored "
               "slots may now point at different levels.\n"
               "Re-record the fixture, or pass --force if you know the table is "
               "compatible." % (fx.gam_sha, have))
        if not args.force:
            sys.exit("ERROR: " + msg)
        print("WARNING: " + msg)

    if fx.legacy:
        # v1 fixture: bytes stored alongside. Copy them, as before.
        contents = {}
        for name, digest in sorted(fx.legacy.items()):
            src = os.path.join(args.fixture, name)
            if not os.path.exists(src):
                sys.exit("%s: fixture is incomplete, %s is missing"
                         % (args.fixture, name))
            data = open(src, "rb").read()
            if sha256_bytes(data) != digest:
                sys.exit("%s: %s does not match its recorded hash — fixture is "
                         "corrupt" % (args.fixture, name))
            contents[name] = data
        how = "copied, v1 fixture"
    else:
        try:
            contents = fx.generate()
        except ValueError as e:
            sys.exit("ERROR: %s" % e)
        how = "generated + hash-checked"

    os.makedirs(args.saves, exist_ok=True)

    # Remove any save the fixture does not name: a leftover slot from an
    # earlier test would still be visible in the menu and could be the one a
    # recording's keypresses land on.
    removed = [n for n in save_files(args.saves) if n not in contents]
    for name in removed:
        os.remove(os.path.join(args.saves, name))

    for name, data in sorted(contents.items()):
        with open(os.path.join(args.saves, name), "wb") as fh:
            fh.write(data)

    print("restored %d file(s) (%s) from %s -> %s%s"
          % (len(contents), how, args.fixture, args.saves,
             "" if not removed else "  (removed %s)" % ", ".join(removed)))


def cmd_verify(args):
    """Is SavedGames/ currently exactly what the fixture says? (exit 1 if not)"""
    fx = read_fixture(args.fixture)
    bad = []
    if fx.gam_sha and sha256(args.gam) != fx.gam_sha:
        bad.append("JJ.GAM differs from the fixture")

    if fx.legacy:
        expected = dict(fx.legacy)
    else:
        try:
            expected = {n: sha256_bytes(d) for n, d in fx.generate().items()}
        except ValueError as e:
            print("MISMATCH against %s:\n  %s" % (args.fixture, e))
            return 1

    for name, digest in sorted(expected.items()):
        live = os.path.join(args.saves, name)
        if not os.path.exists(live):
            bad.append("%s is missing" % name)
        elif sha256(live) != digest:
            bad.append("%s differs" % name)
    for name in save_files(args.saves):
        if name not in expected:
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

    p = sub.add_parser("snapshot",
                       help="copy SavedGames/ into a reusable save fixture")
    p.add_argument("fixture", help="directory to write, e.g. tests/saves/bombstart")
    p.add_argument("--trim", action="store_true",
                   help="zero score/completion/time/unused instead of refusing")
    p.set_defaults(func=cmd_snapshot)

    p = sub.add_parser("convert",
                       help="rewrite a version-2 fixture as version 3 (trimmed)")
    p.add_argument("fixture")
    p.set_defaults(func=cmd_convert)

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
