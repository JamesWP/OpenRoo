#!/usr/bin/env python3
"""Decode and inspect a Ka'roo input recording (KAROO_RECORD).

Format is documented at the top of src/testing/record.cpp.

    python3 tools/replay.py run.rec              # summary
    python3 tools/replay.py run.rec --frames 20  # first 20 frames with input
    python3 tools/replay.py a.rec b.rec          # diff two recordings
    python3 tools/replay.py --convert-v3 a.rec   # rewrite a version 3 recording as 4
"""
import argparse, glob, os, struct, sys

KEYS = 512  # one byte per SDL scancode, 0x80 while held
MAGIC = b"KROO"
VERSION = 4


def _uvarint(blob, off):
    v = shift = 0
    while True:
        b = blob[off]
        off += 1
        v |= (b & 0x7f) << shift
        if not b & 0x80:
            return v, off
        shift += 7


def _put_uvarint(out, v):
    while v >= 0x80:
        out.append(v & 0x7f | 0x80)
        v >>= 7
    out.append(v)


def encode(hdr, frames):
    """hdr is as load() returns; frames are (idx, keys) tuples."""
    saves = hdr.get("saves", {})
    label = hdr["label"].encode("ascii", "replace")[:255]
    out = bytearray(MAGIC)
    out += struct.pack("<BBdIB", VERSION, 1 if hdr["seed_set"] else 0,
                       hdr["dt"], hdr["seed"], len(label))
    out += label
    _put_uvarint(out, len(saves))
    for name in sorted(saves):
        _put_uvarint(out, len(name))
        out += name.encode("ascii")
        _put_uvarint(out, len(saves[name]))
        out += saves[name]
    prev_idx, prev_keys = 0, bytes(KEYS)
    for idx, keys in frames:
        _put_uvarint(out, idx - prev_idx)
        changed = [(i, keys[i]) for i in range(KEYS) if keys[i] != prev_keys[i]]
        out.append(1 if changed else 0)
        if changed:
            _put_uvarint(out, len(changed))
            last = 0
            for i, v in changed:
                _put_uvarint(out, i - last)
                out.append(v)
                last = i
        prev_idx, prev_keys = idx, bytes(keys)
    return bytes(out)


def _header(blob, path, version):
    if len(blob) < 19 or blob[:4] != MAGIC:
        sys.exit("%s: not a Ka'roo recording" % path)
    ver, flags, dt, seed, n = struct.unpack_from("<BBdIB", blob, 4)
    if ver != version:
        sys.exit("%s: version %d, expected version %d" % (path, ver, version))
    label = blob[19:19 + n].decode("ascii", "replace")
    hdr = dict(version=ver, dt=dt, seed=seed, seed_set=bool(flags & 1), label=label)

    off = 19 + n
    cnt, off = _uvarint(blob, off)
    saves = {}
    for _ in range(cnt):
        ln, off = _uvarint(blob, off)
        name = blob[off:off + ln].decode("ascii")
        off += ln
        size, off = _uvarint(blob, off)
        saves[name] = blob[off:off + size]
        off += size
    hdr["saves"] = saves
    return hdr, off


def load(path):
    """(header, [(frame index, key array)]): the array is what is held from
    that frame on, 0x80 per held scancode."""
    with open(path, "rb") as f:
        blob = f.read()
    hdr, off = _header(blob, path, VERSION)
    frames = []
    idx, keys = 0, bytearray(KEYS)
    while off < len(blob):
        d, off = _uvarint(blob, off)
        idx += d
        fl = blob[off]
        off += 1
        if fl & 1:
            cnt, off = _uvarint(blob, off)
            k = 0
            for _ in range(cnt):
                d, off = _uvarint(blob, off)
                k += d
                keys[k] = blob[off]
                off += 1
        frames.append((idx, bytes(keys)))
    return hdr, frames


def convert_v3(path):
    """Rewrite a version 3 recording as version 4.

    Version 3 recorded what the game asked for: the key array, on the frames
    its control dispatch ran, and the answer to each single-key poll.  The
    keys held on a frame are the array's, and any key a poll found down."""
    with open(path, "rb") as f:
        blob = f.read()
    hdr, off = _header(blob, path, 3)
    frames = []
    idx, keys = 0, bytearray(KEYS)
    while off < len(blob):
        d, off = _uvarint(blob, off)
        idx += d
        fl = blob[off]
        off += 1
        if fl & 1:
            off += 1  # the game state
        if fl & 2:
            cnt, off = _uvarint(blob, off)
            k = 0
            for _ in range(cnt):
                d, off = _uvarint(blob, off)
                k += d
                keys[k] = blob[off]
                off += 1
        held = bytearray(keys)
        if fl & 4:
            cnt, off = _uvarint(blob, off)
            for _ in range(cnt):
                v, off = _uvarint(blob, off)
                if v & 1:
                    held[v >> 1] = 0x80
        frames.append((idx, bytes(held)))
    hdr["version"] = VERSION
    with open(path, "wb") as f:
        f.write(encode(hdr, frames))
    return len(frames)


SAVE_GLOBS = ("jj*.sav", "JJ*.sav", "GAM.DAT")


def save_files(saves_dir):
    """Every file the game reads out of SavedGames/, sorted, basenames only."""
    found = set()
    for pat in SAVE_GLOBS:
        for p in glob.glob(os.path.join(saves_dir, pat)):
            if os.path.isfile(p):
                found.add(os.path.basename(p))
    return sorted(found)


def read_saves(saves_dir):
    """{name: bytes} for the save state in SavedGames/."""
    out = {}
    for name in save_files(saves_dir):
        with open(os.path.join(saves_dir, name), "rb") as f:
            out[name] = f.read()
    return out


def restore_saves(hdr, saves_dir):
    """Make SavedGames/ hold exactly the recording's bundled saves."""
    os.makedirs(saves_dir, exist_ok=True)
    for name in save_files(saves_dir):
        if name not in hdr["saves"]:  # a leftover slot would show in the menu
            os.remove(os.path.join(saves_dir, name))
    for name, data in hdr["saves"].items():
        with open(os.path.join(saves_dir, name), "wb") as f:
            f.write(data)


def pressed(keys):
    return [i for i, v in enumerate(keys) if v & 0x80]


def summarise(path):
    hdr, frames = load(path)
    print("%s" % path)
    print("  version %d   dt=%.9f   seed=%u%s   label=%r"
          % (hdr["version"], hdr["dt"], hdr["seed"],
             "" if hdr["seed_set"] else " (UNSET)", hdr["label"]))
    if not frames:
        print("  no frames")
        return hdr, frames
    idxs = [f[0] for f in frames]
    gaps = [b - a for a, b in zip(idxs, idxs[1:]) if b - a != 1]
    keyed = sum(1 for f in frames if pressed(f[1]))
    print("  %d frames, index %d..%d%s"
          % (len(frames), idxs[0], idxs[-1],
             "" if not gaps else "  (%d non-consecutive steps)" % len(gaps)))
    print("  %d frames with a key held" % keyed)
    seen = sorted({k for f in frames for k in pressed(f[1])})
    print("  scancodes seen held: %s" % (seen or "none"))
    return hdr, frames


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rec", nargs="+")
    ap.add_argument("--frames", type=int, default=0,
                    help="print the first N frames that carry input")
    ap.add_argument("--convert-v3", action="store_true",
                    help="rewrite version 3 recordings, in place, as version 4")
    args = ap.parse_args()

    if args.convert_v3:
        for path in args.rec:
            print("%s: %d frames" % (path, convert_v3(path)))
        return 0

    if len(args.rec) == 2:
        (ha, fa), (hb, fb) = load(args.rec[0]), load(args.rec[1])
        print("%s: %d frames | %s: %d frames"
              % (args.rec[0], len(fa), args.rec[1], len(fb)))
        if ha != hb:
            print("headers differ:\n  %r\n  %r" % (ha, hb))
        for i, (x, y) in enumerate(zip(fa, fb)):
            if x != y:
                print("first differing frame at position %d: idx %d vs %d" % (i, x[0], y[0]))
                return 1
        if len(fa) != len(fb):
            print("identical for %d common frames, then lengths differ" % min(len(fa), len(fb)))
            return 1
        print("recordings are IDENTICAL")
        return 0

    hdr, frames = summarise(args.rec[0])
    if args.frames:
        shown = 0
        for idx, keys in frames:
            p = pressed(keys)
            if not p:
                continue
            print("  f=%-6d keys=%s" % (idx, p))
            shown += 1
            if shown >= args.frames:
                break
    return 0


if __name__ == "__main__":
    sys.exit(main())
