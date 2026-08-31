#!/usr/bin/env python3
"""Classify a Ka'roo run as clean, the known crash, or something new.

See CRASH.md.  Exit codes are the point of this script:

    0  clean run — the shutdown sequence completed
    1  the KNOWN crash — guard-page fault in ddraw pack_strided_data
    2  a DIFFERENT crash — no clean shutdown, but the fingerprint does not
       match.  This is new information; read the output.
    3  could not tell (logs missing or truncated)

Usage:
    python3 tools/crashcheck.py                  # current logs
    python3 tools/crashcheck.py --gen 3          # rolled generation .3
    python3 tools/crashcheck.py --quiet          # exit code only

Because the bug is intermittent, a single clean run proves nothing.  To measure
a crash rate, loop it and count:

    for i in $(seq 20); do
        bash launch.sh --skip-launcher --auto-exit 40 >/dev/null 2>&1
        python3 tools/crashcheck.py --quiet; echo "run $i -> $?"
    done
"""
import argparse, os, re, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# A clean shutdown always writes all three, in this order.
SHUTDOWN_MARKERS = [
    "ProgCtrl::WriteBindings: ok",
    "CDM::stopAndClose done",
    "FaktMovie::teardown",
]

# Fingerprint of the known crash (CRASH.md section 2).
FINGERPRINT_EIP = "7b109321"
FINGERPRINT = [
    ("unhandled guard-page exception at the known address",
     re.compile(r"Unhandled exception: 0x80000001 in wow64 32-bit code \(0x7b109321\)", re.I)),
    ("exception code is EXCEPTION_GUARD_PAGE, not an access violation",
     re.compile(r"code=80000001 \(EXCEPTION_GUARD_PAGE\)", re.I)),
    ("fault inside ddraw pack_strided_data",
     re.compile(r"pack_strided_data", re.I)),
]


def read(path):
    if not os.path.exists(path):
        return None
    with open(path, "rb") as f:
        return f.read().decode("utf-8", "replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gen", type=int, default=0,
                    help="rolled log generation (1-5); 0 = current")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    suffix = "" if args.gen == 0 else ".%d" % args.gen
    hooks_path = os.path.join(REPO, "karoo_hooks.log" + suffix)
    steam_path = os.path.join(REPO, "steam-123456.log" + suffix)
    hooks, steam = read(hooks_path), read(steam_path)

    def say(*a):
        if not args.quiet:
            print(*a)

    if hooks is None:
        say("UNKNOWN: %s missing — cannot classify" % hooks_path)
        return 3

    # --- 1. did the run shut down cleanly? ---
    missing = [m for m in SHUTDOWN_MARKERS if m not in hooks]
    guards = hooks.count("=== GUARD PAGE ===")

    if not missing:
        say("CLEAN: shutdown sequence complete (%d guard-page lines)" % guards)
        if guards:
            say("  note: guard-page lines in a CLEAN run are unexpected;")
            say("        every clean run observed so far had zero.")
        return 0

    say("CRASH: run did not shut down cleanly")
    for m in missing:
        say("  missing marker: %s" % m)
    say("  last log line: %s" % (hooks.rstrip().splitlines() or ["<empty>"])[-1][:120])

    # --- 2. is it the known one? ---
    if steam is None:
        say("UNKNOWN: %s missing — cannot fingerprint" % steam_path)
        return 3

    matched, failed = [], []
    for label, rx in FINGERPRINT:
        (matched if rx.search(steam) else failed).append(label)

    for label in matched:
        say("  [match] %s" % label)
    for label in failed:
        say("  [MISS ] %s" % label)

    if not failed:
        say("VERDICT: the KNOWN crash (see CRASH.md) — guard-page fault at 0x%s"
            % FINGERPRINT_EIP)
        return 1

    say("VERDICT: a DIFFERENT crash — fingerprint does not match.")
    say("         This is new information; do not file it as the known bug.")
    for rx in (r"Unhandled exception[^\n]*", r"code=[0-9a-fA-F]{8} \([A-Z_]+\)"):
        hits = re.findall(rx, steam)
        if hits:
            say("         seen instead: %s" % hits[-1][:120])
    return 2


if __name__ == "__main__":
    sys.exit(main())
