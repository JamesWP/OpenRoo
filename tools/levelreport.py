#!/usr/bin/env python3
r"""Ka'roo level-report regression harness.

The game ships its own all-levels dump.  Game::LoadSounds (0x0041A280) polls
GetAsyncKeyState(VK_L) three times just before it acquires the fixed sound
buffers; if L is down it calls WriteLevelReport (0x0041B991), which walks every
level in the game file --- SetCurrentLevelName, OpenLevelFile,
SetupLevelObjects, CalculateLevelScore --- and writes two text files into the
game directory:

    LevelReport.txt   per-level object counts (crystals, fields, enemies,
                      elevators, bombs, ...), par time and cumulative score,
                      one row per level, plus the totals at the end
    ScriptTexts.txt   every instruction-script text, level by level

That is much broader coverage than any replay recording.  A replay visits one
level; this loads and constructs all 80, so a regression in level parsing,
object setup or score calculation shows up as a diff in a text file rather than
as a crash nobody reproduces.  It is fast, too: the dump itself takes about
nine seconds.

    python3 tools/levelreport.py            # run and compare to the baseline
    python3 tools/levelreport.py --bless    # re-baseline (say so in the commit)
    python3 tools/levelreport.py --keep     # leave the outputs in the game dir

Exit code is the point: 0 = both files match the baseline, 1 = they do not.

How the run is driven
---------------------
KAROO_LEVEL_REPORT=1 makes karoo_hooks.dll answer exactly the three VK_L
queries LoadSounds makes (levelreport.cpp), then --- once the report is
written --- drive the menu's Quit node so the game shuts down on its own.  It
has to be a real quit, not a kill: the "GAME: level report created" log line is
written *before* the two files are fclose'd, so terminating the process on that
line can truncate the output.  Waiting for the game's own exit is also what
lets its normal shutdown path run.

What the run touches, and what is put back
------------------------------------------
Beyond the two report files the run rewrites `highscores/` (WriteLevelReport
ends by saving the high-score table, and leaves a `jj.hsc.hsc` behind) and the
game rewrites `SavedGames/` on exit.  None of that is part of the test, and all
of it is committed repo state, so the harness snapshots both directories before
launching and restores them afterwards --- including on a crash or a Ctrl-C.
"""

import argparse
import filecmp
import os
import shutil
import signal
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(REPO, "tests", "levelreport")

# The two files WriteLevelReport produces, relative to the game directory.
OUTPUTS = ["LevelReport.txt", "ScriptTexts.txt"]

# Directories the run mutates as a side effect and that must be put back.
SIDE_EFFECTS = ["highscores", "SavedGames"]

# Wall-clock budget.  The dump takes ~9s; the rest is startup, the menu walk
# and Proton's own teardown.  --auto-exit is the in-game backstop underneath.
BUDGET_SECS = 180
AUTO_EXIT_SECS = 150


# -- side-effect containment ----------------------------------------------

class Sandbox:
    """Snapshot the directories the run rewrites, and restore them after.

    Restoring is unconditional: a run that crashes half way through still
    leaves highscores/ rewritten, and a dirty tree after a *test* is a nasty
    surprise to hand someone.
    """

    def __init__(self, names):
        self.names = [n for n in names if os.path.isdir(os.path.join(REPO, n))]
        self.tmp = None

    def __enter__(self):
        self.tmp = os.path.join(REPO, ".levelreport-sandbox")
        shutil.rmtree(self.tmp, ignore_errors=True)
        os.makedirs(self.tmp)
        for n in self.names:
            shutil.copytree(os.path.join(REPO, n), os.path.join(self.tmp, n))
        return self

    def __exit__(self, *exc):
        for n in self.names:
            dst = os.path.join(REPO, n)
            shutil.rmtree(dst, ignore_errors=True)
            shutil.copytree(os.path.join(self.tmp, n), dst)
        shutil.rmtree(self.tmp, ignore_errors=True)
        return False


# -- the run ---------------------------------------------------------------

def preflight():
    problems = []
    if not os.path.exists(os.path.join(REPO, "karoo_hooks.dll")):
        problems.append("karoo_hooks.dll missing - run build.sh")
    if not os.path.exists(os.path.join(REPO, "Karoo.exe")):
        problems.append("Karoo.exe missing - run patch.py")
    return problems


def wait_for_quiet(timeout=30):
    """A previous Karoo.exe still running would wedge this launch."""
    for _ in range(timeout):
        r = subprocess.run(["pgrep", "-f", "Karoo.exe"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode != 0:
            time.sleep(2)              # let wineserver finish behind it
            return True
        time.sleep(1)
    print("  ! a previous Karoo.exe is still running; the launch may wedge")
    return False


def launch():
    """Run the game with the trigger set.  Returns True if it exited by itself."""
    for f in OUTPUTS:
        try:
            os.unlink(os.path.join(REPO, f))
        except FileNotFoundError:
            pass

    env = dict(os.environ)
    env["KAROO_LEVEL_REPORT"] = "1"
    # The report is not a timed run and draws nothing worth watching; unpin the
    # frame rate from the display refresh so the menu walk is not vsync-paced.
    env.setdefault("vblank_mode", "0")
    env.setdefault("MESA_VK_WSI_PRESENT_MODE", "immediate")
    env.setdefault("__GL_SYNC_TO_VBLANK", "0")

    cmd = [os.path.join(REPO, "launch.sh"), "--skip-launcher",
           "--auto-exit", str(AUTO_EXIT_SECS)]
    print("  launching: KAROO_LEVEL_REPORT=1 %s" % " ".join(cmd[1:]))
    proc = subprocess.Popen(cmd, cwd=REPO, env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            start_new_session=True)
    # launch.sh exits non-zero however the run went (the game's WinMain returns
    # the posted close code), so its return code is deliberately ignored: the
    # two output files are the oracle.
    try:
        proc.wait(timeout=BUDGET_SECS)
        return True
    except subprocess.TimeoutExpired:
        pass

    print("  TIMEOUT: no exit after %ds - killing the run.  The report may be "
          "truncated; treat a diff below as unproven." % BUDGET_SECS)
    for sig in (signal.SIGTERM, signal.SIGKILL):
        try:
            os.killpg(os.getpgid(proc.pid), sig)
        except (ProcessLookupError, PermissionError):
            break
        try:
            proc.wait(timeout=10)
            break
        except subprocess.TimeoutExpired:
            continue
    subprocess.run(["pkill", "-f", "Karoo.exe"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    wait_for_quiet()
    return False


def triggered():
    """Did the DLL actually deliver the trigger, and did the game accept it?

    Without this a run that never reached LoadSounds -- a launcher that did not
    dismiss, a missing display -- would compare two stale files and pass.
    """
    ok_dll = ok_game = False
    try:
        with open(os.path.join(REPO, "karoo_hooks.log"), errors="replace") as fh:
            ok_dll = "levelreport: trigger delivered" in fh.read()
    except OSError:
        pass
    try:
        with open(os.path.join(REPO, "JJ.log"), errors="replace") as fh:
            ok_game = "level report created" in fh.read()
    except OSError:
        pass
    return ok_dll, ok_game


# -- comparison ------------------------------------------------------------

def diff(name):
    """Return a short unified diff of baseline vs produced, or None if equal."""
    got = os.path.join(REPO, name)
    want = os.path.join(BASELINE, name)
    if not os.path.exists(got):
        return "  %s: not produced by the run" % name
    if not os.path.exists(want):
        return "  %s: no baseline - run --bless" % name
    if filecmp.cmp(want, got, shallow=False):
        return None
    r = subprocess.run(["diff", "-u", "--label", "baseline/" + name,
                        "--label", "run/" + name, want, got],
                       capture_output=True, text=True)
    lines = r.stdout.splitlines()
    head = "\n".join("  " + ln for ln in lines[:60])
    if len(lines) > 60:
        head += "\n  ... %d more diff lines" % (len(lines) - 60)
    return "  %s: DIFFERS (%d diff lines)\n%s" % (name, len(lines), head)


def bless():
    os.makedirs(BASELINE, exist_ok=True)
    for f in OUTPUTS:
        src = os.path.join(REPO, f)
        if not os.path.exists(src):
            print("  ! %s was not produced; not blessing" % f)
            return 1
        shutil.copyfile(src, os.path.join(BASELINE, f))
        print("  blessed %s (%d bytes)" % (f, os.path.getsize(src)))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--bless", action="store_true",
                    help="re-baseline tests/levelreport/ from this run")
    ap.add_argument("--keep", action="store_true",
                    help="leave the produced files in the game directory")
    ap.add_argument("--no-run", action="store_true",
                    help="compare the files already in the game directory")
    args = ap.parse_args()

    problems = preflight()
    if problems:
        for p in problems:
            print("ERROR: " + p)
        return 1

    print("level report: all-levels dump via the game's own VK_L trigger")

    if args.no_run:
        rc = bless() if args.bless else 0
        if not args.bless:
            rc = report_diffs()
        return rc

    wait_for_quiet()
    with Sandbox(SIDE_EFFECTS):
        clean_exit = launch()

    ok_dll, ok_game = triggered()
    if not ok_dll:
        print("FAIL: the DLL never delivered the trigger - no "
              "'levelreport: trigger delivered' in karoo_hooks.log.  The run "
              "did not reach Game::LoadSounds.")
        return 1
    if not ok_game:
        print("FAIL: the trigger was delivered but the game never logged "
              "'level report created' in JJ.log.")
        return 1
    print("  trigger delivered, report written%s" %
          ("" if clean_exit else " (but the run had to be killed)"))

    rc = bless() if args.bless else report_diffs()

    if not args.keep and not args.bless:
        for f in OUTPUTS:
            try:
                os.unlink(os.path.join(REPO, f))
            except FileNotFoundError:
                pass
    return rc


def report_diffs():
    bad = [d for d in (diff(f) for f in OUTPUTS) if d]
    if bad:
        print("FAIL: the level report changed")
        for d in bad:
            print(d)
        return 1
    print("PASS: both files match tests/levelreport/")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
