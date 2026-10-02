#!/usr/bin/env python3
r"""Find reads of uninitialised heap memory, using a recording as the oracle.

    python3 tools/find_uninit.py enemyfactory
    python3 tools/find_uninit.py enemyfactory --fill 0xA5 --keep-build

A replay that passes only because fresh heap memory happens to hold zeros has
a latent bug.  This fills fresh CRT heap blocks with a poison byte
(src/app/heappoison.cpp) and, if the recording then fails, narrows the failure
to one allocation, then to the bytes in it that matter:

  1. Build with -DKAROO_HEAP_POISON=ON.
  2. Control: with no poison the recording must pass, or there is nothing to
     compare against.
  3. Poison every allocation.  If it still passes, there is nothing to find.
  4. Bisect on the allocation number.  The smallest N such that poisoning
     allocations [0, N) fails names the culprit, N-1.
  5. Re-run poisoning only that allocation, logging its size and the return
     addresses on the stack, and resolve them against the exe's symbols.
  6. Bisect on byte offset inside the block, down to a single byte, and check
     that poisoning with a second fill byte fails too.
  7. For the Game object, name the member that holds the byte.

This reports one culprit per run.  Fix it (usually by initialising it), then run
again: the next one is no longer hidden behind it.  A culprit that is only in
the state dump can hide a gameplay one, so the failing checks are printed at
each step.

A poison byte is not proof by itself: some code legitimately ignores memory it
did not write.  The evidence is that poisoning *this one byte* changes the
replay's result, and a second poison value changes it too.

The build is left with KAROO_HEAP_POISON off afterwards unless --keep-build.
"""

import argparse
import bisect
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(REPO, "build", "KarooOwn.exe")
PREFIX = "i686-w64-mingw32-"
LOG = "/tmp/find_uninit.log"
WINE_LOG = "Z:" + LOG.replace("/", "\\")


def sh(cmd, **kw):
    return subprocess.run(cmd, cwd=REPO, text=True, capture_output=True, **kw)


def build(poison):
    flag = "ON" if poison else "OFF"
    r = sh(["cmake", "-S", ".", "-B", "build",
            "-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake",
            "-DKAROO_HEAP_POISON=" + flag])
    if r.returncode == 0:
        r = sh(["cmake", "--build", "build", "-j8"])
    if r.returncode != 0:
        sys.exit("build failed:\n" + (r.stdout + r.stderr)[-2000:])


class Runner:
    def __init__(self, recording):
        self.recording = recording
        self.runs = 0

    def run(self, **poison):
        """One replay.  Returns the list of failing check lines, [] on a pass.
        A run that does not finish cleanly is reported as a failure too."""
        env = dict(os.environ)
        for k, v in poison.items():
            env["KAROO_POISON_" + k] = str(v)
        r = subprocess.run([sys.executable, "tools/replaytest.py", self.recording],
                           cwd=REPO, env=env, text=True, capture_output=True)
        self.runs += 1
        out = r.stdout
        fails = [l.strip() for l in out.splitlines() if l.strip().startswith(("FAIL:", "STUCK"))]
        if r.returncode != 0 and not fails:
            fails = ["replaytest did not finish: " + (r.stderr.strip().splitlines() or ["?"])[-1]]
        args = " ".join("%s=%s" % kv for kv in poison.items() if kv[0] not in ("ALLOC", "LOG"))
        print("    run %-3d %-4s %s" % (self.runs, "FAIL" if fails else "pass", args), flush=True)
        return fails


def first_failing_prefix(fails_for, lo, hi):
    """Smallest n in (lo, hi] such that fails_for(n); fails_for(lo) is False and
    fails_for(hi) is True."""
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if fails_for(mid):
            hi = mid
        else:
            lo = mid
    return hi


def text_range():
    out = sh([PREFIX + "objdump", "-h", EXE]).stdout
    m = re.search(r"\.text\s+([0-9a-f]+)\s+([0-9a-f]+)", out)
    return (int(m.group(2), 16), int(m.group(2), 16) + int(m.group(1), 16))


def symbolise(addrs):
    """Nearest preceding function symbol for each address."""
    syms = []
    for l in sh([PREFIX + "nm", "-n", "-C", EXE]).stdout.splitlines():
        p = l.split(None, 2)
        if len(p) == 3 and p[1] in "tTwW":
            syms.append((int(p[0], 16), p[2]))
    out = []
    for a in addrs:
        i = bisect.bisect(syms, (a, "￿")) - 1
        out.append("0x%x  %s+0x%x" % (a, syms[i][1], a - syms[i][0]) if i >= 0 else hex(a))
    return out


def allocation_site(lines):
    """The first frame beyond the allocator wrappers and operator new."""
    for i, l in enumerate(lines):
        if "__wrap_" in l:
            rest = [x for x in lines[i + 1:] if "operator new" not in x]
            return rest[0] if rest else None
    return None


def callers_of(index, runner, fill):
    if os.path.exists(LOG):
        os.remove(LOG)
    runner.run(ALLOC=fill, FROM=index, TO=index + 1, LOG=WINE_LOG)
    if not os.path.exists(LOG):
        return None, []
    f = open(LOG).read().split()
    size = int(re.search(r"size=(\d+)", " ".join(f)).group(1))
    lo, hi = text_range()
    addrs = [int(a, 16) for a in f[4:] if lo <= int(a, 16) < hi]
    return size, addrs


def member_of_game(offset):
    """Name the Game member holding `offset`, by compiling offsetof for each
    data member declared in src/core/game.h."""
    src = open(os.path.join(REPO, "src/core/game.h")).read().split("\n")
    start = next((i for i, l in enumerate(src) if l.startswith("private:") and
                  any("field_04_" in x for x in src[i:i + 8])), None)
    if start is None:
        return None
    names = []
    for l in src[start:]:
        if l.startswith("};"):
            break
        l = re.sub(r"/\*.*?\*/|//.*", "", l)
        m = re.match(r"^\s{4}(?!static|typedef|using|friend|return)[A-Za-z_][\w:<>, \*]*?"
                     r"[\s\*&]+(\w+)\s*(\[[^\]]*\])*\s*(=[^;]*)?;\s*$", l)
        if m and "(" not in l:
            names.append(m.group(1))
    tu = '#define private public\n#include "game.h"\n#include <stddef.h>\n' + "".join(
        'extern "C" const unsigned off_%s = offsetof(Game,%s);\n' % (n, n) for n in names)
    tmp = "/tmp/find_uninit_off.cpp"
    open(tmp, "w").write(tu)
    app = os.path.join(REPO, "build", "src", "app")
    r = subprocess.run([PREFIX + "g++", "-w", "@CMakeFiles/app.dir/includes_CXX.rsp",
                        "-I" + os.path.join(REPO, "src/core"), "-S", tmp, "-o", "/tmp/find_uninit_off.s"],
                       cwd=app, text=True, capture_output=True)
    if r.returncode != 0:
        return None
    offs = sorted((int(m.group(2)), m.group(1)) for m in re.finditer(
        r"_?off_(\w+):\s*\n\s*\.long\s+(\d+)", open("/tmp/find_uninit_off.s").read()))
    before = [o for o in offs if o[0] <= offset]
    if not before:
        return None
    base, name = before[-1]
    return "Game::%s + %d   (a member of %s starts at Game+%d)" % (name, offset - base, name, base)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("recording", help="a recording name from tests/manifest.json")
    ap.add_argument("--fill", default="0xCD", help="poison byte (default 0xCD)")
    ap.add_argument("--second-fill", default="0xA5", help="byte for the confirming run")
    ap.add_argument("--keep-build", action="store_true", help="leave the build poison-enabled")
    ap.add_argument("--no-build", action="store_true", help="the build already has KAROO_HEAP_POISON=ON")
    a = ap.parse_args()
    fill, fill2 = int(a.fill, 0), int(a.second_fill, 0)

    if not a.no_build:
        print("building with KAROO_HEAP_POISON=ON ...", flush=True)
        build(True)
    try:
        r = Runner(a.recording)
        if not os.path.exists(EXE):
            sys.exit("no %s" % EXE)

        print("1. control: no poison")
        if r.run():
            sys.exit("   the recording fails without poison; fix that first")

        print("2. poison every allocation with 0x%02x" % fill)
        every = r.run(ALLOC=fill)
        if not every:
            print("   still passes: nothing found")
            return 0
        for l in every:
            print("   " + l)

        # How many allocations were there?  A poisoned run past the end is
        # the same as an unbounded one, so bisect on a bound that is certainly
        # beyond them.
        print("3. bisect on allocation number")
        top = 1
        while r.run(ALLOC=fill, FROM=0, TO=top) == [] and top < (1 << 24):
            top *= 2
        n = first_failing_prefix(lambda m: bool(r.run(ALLOC=fill, FROM=0, TO=m)), 0, top)
        index = n - 1
        print("   allocation #%d" % index)
        alone = r.run(ALLOC=fill, FROM=index, TO=index + 1)
        if not alone:
            print("   poisoning allocation #%d alone does not fail: the failure needs more than one\n"
                  "   allocation (or depends on the heap layout).  Stopping." % index)
            return 2
        print("   poisoned alone, it fails with:")
        for l in alone:
            print("     " + l)

        print("4. where it is allocated")
        size, addrs = callers_of(index, r, fill)
        if size is None:
            print("   no log written (is the log path reachable from the game?)")
        else:
            print("   size %d (0x%x) bytes; return addresses on the stack, innermost first:" % (size, size))
            for s in symbolise(addrs):
                print("     " + s)
        if size is None:
            return 2

        print("5. bisect on byte offset within the block")

        def fails_range(lo, hi):
            return bool(r.run(ALLOC=fill, FROM=index, TO=index + 1, OFF_FROM=lo, OFF_TO=hi))
        if not fails_range(0, size):
            print("   zero-filled with poison on the whole block, but it passes: not reproducible here")
            return 2
        end = first_failing_prefix(lambda m: fails_range(0, m), 0, size)
        lo, hi = 0, end                  # largest start such that [start, end) still fails
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if fails_range(mid, end):
                lo = mid
            else:
                hi = mid
        off = lo
        print("   byte at offset %d (0x%x) of the block" % (off, off))

        print("6. confirm with a different poison byte")
        confirm = r.run(ALLOC=fill2, FROM=index, TO=index + 1, OFF_FROM=off, OFF_TO=off + 1)
        control = r.run(ALLOC=fill, FROM=index, TO=index + 1, OFF_FROM=off + 1, OFF_TO=size)
        print("   poison 0x%02x on that byte only: %s" % (fill2, "FAILS" if confirm else "passes (?)"))
        print("   poison 0x%02x on everything else in the block: %s" %
              (fill, "passes" if not control else "FAILS (another byte matters too)"))
        for l in confirm:
            print("     " + l)

        print("\nresult")
        print("  recording %s, allocation #%d, offset %d (0x%x)" % (a.recording, index, off, off))
        site = allocation_site(symbolise(addrs))
        if site:
            print("  allocated at " + site)
        if size == 0x527000:
            m = member_of_game(off)
            if m:
                print("  member: " + m)
        return 0
    finally:
        if not a.no_build and not a.keep_build:
            print("restoring build with KAROO_HEAP_POISON=OFF ...", flush=True)
            build(False)


if __name__ == "__main__":
    sys.exit(main())
