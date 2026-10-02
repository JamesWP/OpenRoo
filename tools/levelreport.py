#!/usr/bin/env python3
r"""Ka'roo level-report regression harness.

The game ships its own all-levels dump.  Game::LoadSounds polls
GetAsyncKeyState(VK_L) three times just before it acquires the fixed sound
buffers; if L is down it calls WriteLevelReport, which walks every
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

Exit code is the point: 0 = the run matches the baseline, 1 = it does not.

The baseline: our canonical form, hashed
----------------------------------------
The game's text is not stored in Open'Roo.  The harness parses the three
outputs into a canonical form of its own -- one JSON record per level (the
report row under our field names, the level name, the script texts) and one
per .leo file (its records) -- and `tests/levelreport.json` keeps only a
SHA-256 of each record plus the counts that make a failure readable without
the data (the numeric report columns; objects and entries per .leo file).
On a mismatch the harness names the level or .leo file and the counts that
moved.

The full text diff is extra detail, shown when a local text baseline exists:
`tests/levelreport/` in the private repo, or `run/levelreport-baseline/`,
which `--baseline` writes from a run you trust (a known-good build).  A
differing text baseline also fails the run.

    python3 tools/levelreport.py --baseline          # keep this run's text locally
    python3 tools/levelreport.py --from DIR --bless  # hash outputs already in DIR

How the run is driven
---------------------
KAROO_LEVEL_REPORT=1 makes KarooOwn.exe answer exactly the three VK_L
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
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUN = os.path.join(REPO, "run")     # launch.sh runs the game here
HASHES = os.path.join(REPO, "tests", "levelreport.json")
# Local text baselines, first found wins.  Neither is needed by the gate.
TEXT_BASELINES = [os.path.join(REPO, "tests", "levelreport"),
                  os.path.join(RUN, "levelreport-baseline")]

# The two files WriteLevelReport produces, relative to the game directory,
# plus LeoRecords.txt: our KAROO_LEO_RECDUMP of every .leo record the run
# builds -- the oracle for the ParseExtraObjectEntry replacement, captured
# from the original handler (extraobjects.cpp recDump).
LEO_DUMP = "LeoRecords.txt"
OUTPUTS = ["LevelReport.txt", "ScriptTexts.txt", LEO_DUMP]

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
        self.names = [n for n in names if os.path.isdir(os.path.join(RUN, n))]
        self.tmp = None

    def __enter__(self):
        self.tmp = os.path.join(RUN, ".levelreport-sandbox")
        shutil.rmtree(self.tmp, ignore_errors=True)
        os.makedirs(self.tmp)
        for n in self.names:
            shutil.copytree(os.path.join(RUN, n), os.path.join(self.tmp, n))
        return self

    def __exit__(self, *exc):
        for n in self.names:
            dst = os.path.join(RUN, n)
            shutil.rmtree(dst, ignore_errors=True)
            shutil.copytree(os.path.join(self.tmp, n), dst)
        shutil.rmtree(self.tmp, ignore_errors=True)
        return False


# -- the run ---------------------------------------------------------------

def preflight():
    problems = []
    if not os.path.exists(os.path.join(REPO, "build", "KarooOwn.exe")):
        problems.append("KarooOwn.exe missing - run make")
    return problems


def wait_for_quiet(timeout=30):
    """A previous Karoo.exe still running would wedge this launch."""
    for _ in range(timeout):
        r = subprocess.run(["pgrep", "-f", r"Karoo(Own)?\.exe"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode != 0:
            time.sleep(2)              # let wineserver finish behind it
            return True
        time.sleep(1)
    print("  ! a previous Karoo.exe is still running; the launch may wedge")
    return False


def launch(headless=True):
    """Run the game with the trigger set.  Returns True if it exited by itself."""
    for f in OUTPUTS:
        try:
            os.unlink(os.path.join(RUN, f))
        except FileNotFoundError:
            pass

    env = dict(os.environ)
    env["KAROO_LEVEL_REPORT"] = "1"
    env["KAROO_LEO_RECDUMP"] = "Z:" + os.path.join(RUN, LEO_DUMP).replace("/", "\\")
    # The report is not a timed run and draws nothing worth watching; unpin the
    # frame rate from the display refresh so the menu walk is not vsync-paced.
    env.setdefault("vblank_mode", "0")
    env.setdefault("MESA_VK_WSI_PRESENT_MODE", "immediate")
    env.setdefault("__GL_SYNC_TO_VBLANK", "0")

    # --headless is the default, exactly as in tools/replaytest.py: no window,
    # no display needed and nothing takes focus, which is what makes this
    # runnable from a background job.  It is not merely --skip-launcher with
    # the window hidden -- DirectDraw is replaced by the in-DLL null device
    # (src/d3d/nullddraw.cpp), so no driver is involved at all.  The report
    # is produced by the game's own WriteLevelReport and draws nothing anyone
    # needs to watch, so there is no reason to want a display by default;
    # --no-headless falls back to --skip-launcher when you do.
    cmd = [os.path.join(REPO, "launch.sh"),
           "--headless" if headless else "--skip-launcher",
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
    subprocess.run(["pkill", "-f", r"Karoo(Own)?\.exe"],
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
        with open(os.path.join(RUN, "karoo_hooks.log"), errors="replace") as fh:
            ok_dll = "levelreport: trigger delivered" in fh.read()
    except OSError:
        pass
    try:
        with open(os.path.join(RUN, "karoo_hooks.log"), errors="replace") as fh:
            ok_game = "level report created" in fh.read()
    except OSError:
        pass
    return ok_dll, ok_game


# -- comparison ------------------------------------------------------------

# The report's columns, in file order, under our names.  The row ends with
# the level's file name, which is not a count.  Each row carries one more
# number than the game's header labels: the level's time-bonus item count,
# between Bomb and Time (src/level/reportwriter.cpp).  "Time" is the time
# bonus, half the level file's time limit; "IS" is the script's line count.
REPORT_FIELDS = [
    "level", "world", "bonus", "script_lines", "leo_objects",
    "catchers", "throwers", "crystals", "hidden_crystals", "needed_crystals",
    "hidden_lives", "fields", "obstacles", "elevators", "platforms",
    "destructibles", "glue", "jumps", "teleporters", "slides", "ice",
    "bridges", "lives", "freeze", "protect", "paraglide", "speed", "bombs", "time_bonus_items",
    "time_bonus", "cumulative_score", "file"]


def _lines(path):
    with open(path, "rb") as f:
        return f.read().decode("latin-1").replace("\r\n", "\n").split("\n")


def _cell(v):
    v = v.strip()
    if v == "":
        return None
    return int(v) if re.fullmatch(r"-?\d+", v) else v


def parse_report(path):
    """LevelReport.txt -> ({level: row dict}, summary dict)."""
    rows, summary, rule = {}, {}, 0
    for ln in _lines(path):
        if ln.startswith("-----"):
            rule += 1
            continue
        if rule == 1:
            cells = ln.split("\t")
            if cells and cells[-1] == "":
                cells.pop()
            row = dict(zip(REPORT_FIELDS, map(_cell, cells)))
            if len(cells) != len(REPORT_FIELDS):
                raise ValueError("report row with %d cells: %r" % (len(cells), ln))
            row["bonus"] = row["bonus"] == "X"
            rows[row["level"]] = row
        elif rule == 2:
            m = re.match(r"(Testscores|Texts in Scripts|Splines in Scripts):(\d+)", ln)
            if m:
                summary[m.group(1).lower().replace(" ", "_")] = int(m.group(2))
            elif ln.strip():
                summary["totals"] = [_cell(c) for c in ln.split("\t") if c.strip()]
        elif rule == 0:
            m = re.match(r"(gamefile|Levels):(\S+)", ln)
            if m:
                summary[m.group(1).lower()] = _cell(m.group(2))
    return rows, summary


def parse_scripts(path):
    """ScriptTexts.txt -> {level: {"file", "name", "texts": [...]}}."""
    levels, cur, text = {}, None, None
    for ln in _lines(path) + ["*****"]:
        if ln.startswith("*****") or re.match(r"\d+\. Text:$", ln):
            if text is not None:
                cur["texts"].append("\n".join(text).strip("\n"))
            text = [] if not ln.startswith("*****") else None
            continue
        m = re.match(r"\*\* Level (\d+)\s+Filename:(.*?)\s*$", ln)
        if m:
            cur = levels[int(m.group(1))] = {"file": m.group(2), "name": None,
                                             "texts": []}
            continue
        m = re.match(r"\*\* Levelname: ?(.*?)\s*$", ln)
        if m:
            cur["name"] = m.group(1)
        elif text is not None:
            text.append(ln.rstrip())
    return levels


def parse_leo(path):
    """LeoRecords.txt -> {file: {"objects", "entries", "records": [...]}}."""
    files, cur = {}, None
    for ln in _lines(path):
        m = re.match(r"== (\S+) objects=(\d+) entries=(\d+)", ln)
        if m:
            cur = files[m.group(1).replace("\\", "/")] = {
                "objects": int(m.group(2)), "entries": int(m.group(3)),
                "records": []}
        elif ln.startswith("["):
            cur["records"].append(ln.split(None, 1)[1] if " " in ln else "")
        elif ln.startswith("    ") and cur["records"]:
            cur["records"][-1] += " " + ln.strip()
    return files


def _sha(obj):
    blob = json.dumps(obj, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(blob).hexdigest()


def canonical(src):
    """The three outputs in DIR -> the stored form: hashes plus counts."""
    rows, summary = parse_report(os.path.join(src, "LevelReport.txt"))
    scripts = parse_scripts(os.path.join(src, "ScriptTexts.txt"))
    leo = parse_leo(os.path.join(src, LEO_DUMP))
    if set(rows) != set(scripts):
        raise ValueError("report and script texts list different levels")
    out = {"summary": {"sha256": _sha(summary),
                       "counts": {k: v for k, v in summary.items()
                                  if k in ("levels", "testscores",
                                           "texts_in_scripts",
                                           "splines_in_scripts")}},
           "levels": {}, "leo": {}}
    for n in sorted(rows):
        rec = {"report": rows[n], "script": scripts[n]}
        counts = {k: v for k, v in rows[n].items()
                  if isinstance(v, int) and not isinstance(v, bool)}
        counts["script_texts_parsed"] = len(scripts[n]["texts"])
        out["levels"][str(n)] = {"file": rows[n]["file"].replace("\\", "/"),
                                 "sha256": _sha(rec), "counts": counts}
    for f in sorted(leo):
        out["leo"][f] = {"sha256": _sha(leo[f]),
                         "counts": {"objects": leo[f]["objects"],
                                    "entries": leo[f]["entries"],
                                    "records": len(leo[f]["records"])}}
    return out


def compare_hashes(src):
    """Return a list of failure lines; empty when the run matches."""
    if not os.path.exists(HASHES):
        return ["  %s missing - run --bless" % os.path.relpath(HASHES, REPO)]
    try:
        got = canonical(src)
    except (OSError, ValueError, KeyError, TypeError) as e:
        return ["  outputs do not parse: %s" % e]
    with open(HASHES) as f:
        want = json.load(f)
    bad = []

    def one(label, w, g):
        if g is None:
            bad.append("  %s: missing from the run" % label)
        elif w is None:
            bad.append("  %s: not in the baseline" % label)
        elif w["sha256"] != g["sha256"]:
            moved = ["%s %s->%s" % (k, w["counts"].get(k), g["counts"].get(k))
                     for k in sorted(set(w["counts"]) | set(g["counts"]))
                     if w["counts"].get(k) != g["counts"].get(k)]
            bad.append("  %s: DIFFERS (%s)" % (
                label, ", ".join(moved) if moved else "counts equal; content moved"))

    one("summary", want["summary"], got["summary"])
    for sect, name in (("levels", "level %s"), ("leo", "%s")):
        for k in sorted(set(want[sect]) | set(got[sect]),
                        key=lambda k: (len(k), k)):
            w, g = want[sect].get(k), got[sect].get(k)
            label = name % k
            if sect == "levels":
                label += " (%s)" % (w or g)["file"]
            one(label, w, g)
    return bad


def text_baseline():
    for d in TEXT_BASELINES:
        if all(os.path.exists(os.path.join(d, f)) for f in OUTPUTS):
            return d
    return None


def diff(name, src, base):
    """Return a short unified diff of baseline vs produced, or None if equal."""
    got = os.path.join(src, name)
    want = os.path.join(base, name)
    if not os.path.exists(got):
        return "  %s: not produced by the run" % name
    if filecmp.cmp(want, got, shallow=False):
        return None
    r = subprocess.run(["diff", "-u", "--label", os.path.relpath(want, REPO),
                        "--label", os.path.relpath(got, REPO), want, got],
                       capture_output=True, text=True)
    lines = r.stdout.splitlines()
    head = "\n".join("  " + ln for ln in lines[:60])
    if len(lines) > 60:
        head += "\n  ... %d more diff lines" % (len(lines) - 60)
    return "  %s: DIFFERS (%d diff lines)\n%s" % (name, len(lines), head)


def normalise_leo_dump():
    """Make the dump's per-file headers repo-relative, so the baseline does
    not depend on where the checkout lives."""
    path = os.path.join(RUN, LEO_DUMP)
    if not os.path.exists(path):
        return
    with open(path, "rb") as f:
        data = f.read()
    data = re.sub(rb"(?m)^== .*?\\Level3DExtraObjects", b"== Level3DExtraObjects", data)
    with open(path, "wb") as f:
        f.write(data)


def copy_outputs(src, dst):
    for f in OUTPUTS:
        if not os.path.exists(os.path.join(src, f)):
            print("  ! %s was not produced; not copying" % f)
            return 1
    os.makedirs(dst, exist_ok=True)
    for f in OUTPUTS:
        if os.path.abspath(src) != os.path.abspath(dst):
            shutil.copyfile(os.path.join(src, f), os.path.join(dst, f))
        print("  %s -> %s" % (f, os.path.relpath(dst, REPO)))
    return 0


def bless(src):
    """Write tests/levelreport.json; refresh the private text baseline too,
    where this checkout has one."""
    try:
        out = canonical(src)
    except (OSError, ValueError, KeyError, TypeError) as e:
        print("  ! outputs do not parse (%s); not blessing" % e)
        return 1
    with open(HASHES, "w") as f:
        json.dump(out, f, indent=1, sort_keys=True)
        f.write("\n")
    print("  blessed %s (%d levels, %d .leo files)" % (
        os.path.relpath(HASHES, REPO), len(out["levels"]), len(out["leo"])))
    if os.path.isdir(TEXT_BASELINES[0]):
        return copy_outputs(src, TEXT_BASELINES[0])
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--bless", action="store_true",
                    help="re-baseline tests/levelreport/ from this run")
    ap.add_argument("--keep", action="store_true",
                    help="leave the produced files in the game directory")
    ap.add_argument("--no-run", action="store_true",
                    help="compare the files already in run/")
    ap.add_argument("--from", dest="src", metavar="DIR",
                    help="compare (or bless) the outputs in DIR; implies --no-run")
    ap.add_argument("--baseline", action="store_true",
                    help="after the run, keep its text in run/levelreport-baseline/ "
                         "for local diffs")
    ap.add_argument("--headless", dest="headless", action="store_true",
                    default=True,
                    help="run with no display at all (the default)")
    ap.add_argument("--no-headless", dest="headless", action="store_false",
                    help="render to a real window instead (--skip-launcher)")
    args = ap.parse_args()

    problems = [] if (args.no_run or args.src) else preflight()
    if problems:
        for p in problems:
            print("ERROR: " + p)
        return 1

    print("level report: all-levels dump via the game's own VK_L trigger")

    if args.no_run or args.src:
        src = os.path.abspath(args.src) if args.src else RUN
        return bless(src) if args.bless else report_diffs(src)

    wait_for_quiet()
    with Sandbox(SIDE_EFFECTS):
        clean_exit = launch(headless=args.headless)

    ok_dll, ok_game = triggered()
    if not ok_dll:
        print("FAIL: the DLL never delivered the trigger - no "
              "'levelreport: trigger delivered' in karoo_hooks.log.  The run "
              "did not reach Game::LoadSounds.")
        return 1
    if not ok_game:
        print("FAIL: the trigger was delivered but the game never logged "
              "'level report created' in karoo_hooks.log.")
        return 1
    print("  trigger delivered, report written%s" %
          ("" if clean_exit else " (but the run had to be killed)"))

    normalise_leo_dump()
    if args.baseline:
        rc = copy_outputs(RUN, TEXT_BASELINES[1])
    else:
        rc = bless(RUN) if args.bless else report_diffs(RUN)

    if not args.keep and not args.bless and not args.baseline:
        for f in OUTPUTS:
            try:
                os.unlink(os.path.join(RUN, f))
            except FileNotFoundError:
                pass
    return rc


def report_diffs(src):
    bad = compare_hashes(src)
    base = text_baseline()
    if base and os.path.abspath(base) != os.path.abspath(src):
        bad += [d for d in (diff(f, src, base) for f in OUTPUTS) if d]
    if bad:
        print("FAIL: the level report changed")
        for d in bad:
            print(d)
        return 1
    print("PASS: every level and .leo file matches %s%s" % (
        os.path.relpath(HASHES, REPO),
        "; text matches %s" % os.path.relpath(base, REPO) if base else ""))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
