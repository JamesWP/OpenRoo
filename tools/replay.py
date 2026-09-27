#!/usr/bin/env python3
"""Decode and inspect a Ka'roo input recording (KAROO_RECORD).

Format is documented at the top of src/testing/record.cpp.

    python3 tools/replay.py run.rec              # summary
    python3 tools/replay.py run.rec --frames 20  # first 20 frames with input
    python3 tools/replay.py a.rec b.rec          # diff two recordings
"""
import argparse, struct, sys

HEADER_SIZE = 80
MAGIC = b"KROO"


def load(path):
    with open(path, "rb") as f:
        blob = f.read()
    if len(blob) < HEADER_SIZE or blob[:4] != MAGIC:
        sys.exit("%s: not a Ka'roo recording" % path)
    ver, = struct.unpack_from("<I", blob, 4)
    dt,  = struct.unpack_from("<d", blob, 8)
    seed, flags = struct.unpack_from("<II", blob, 16)
    label = blob[24:80].split(b"\0")[0].decode("ascii", "replace")
    hdr = dict(version=ver, dt=dt, seed=seed, seed_set=bool(flags & 1), label=label)

    frames, off = [], HEADER_SIZE
    while off + 262 <= len(blob):
        idx, = struct.unpack_from("<I", blob, off)
        state = blob[off + 4]
        keys = blob[off + 5:off + 261]
        n = blob[off + 261]
        off += 262
        if off + 2 * n > len(blob):
            print("warning: truncated async block at frame %d" % idx, file=sys.stderr)
            break
        async_ = [(blob[off + 2 * i], blob[off + 2 * i + 1]) for i in range(n)]
        off += 2 * n
        frames.append((idx, state, keys, async_))
    if off != len(blob):
        print("warning: %d trailing bytes" % (len(blob) - off), file=sys.stderr)
    return hdr, frames


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
    keyed = sum(1 for f in frames if pressed(f[2]))
    asyncs = sum(len(f[3]) for f in frames)
    print("  %d frames, index %d..%d%s"
          % (len(frames), idxs[0], idxs[-1],
             "" if not gaps else "  (%d non-consecutive steps)" % len(gaps)))
    print("  %d frames with a key held, %d GetAsyncKeyState queries"
          % (keyed, asyncs))
    states = sorted({f[1] for f in frames})
    print("  game_state values seen: %s" % states)
    vkeys = sorted({a[0] for f in frames for a in f[3]})
    print("  vkeys queried: %s" % (["0x%02X" % v for v in vkeys] or "none"))
    return hdr, frames


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rec", nargs="+")
    ap.add_argument("--frames", type=int, default=0,
                    help="print the first N frames that carry input")
    args = ap.parse_args()

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
        for idx, state, keys, async_ in frames:
            p, a = pressed(keys), async_
            if not p and not a:
                continue
            print("  f=%-6d state=%d keys=%s async=%s"
                  % (idx, state, p,
                     ["0x%02X:%d" % (v, d) for v, d in a] or "-"))
            shown += 1
            if shown >= args.frames:
                break
    return 0


if __name__ == "__main__":
    sys.exit(main())
