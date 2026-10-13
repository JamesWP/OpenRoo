#!/usr/bin/env python3
"""backtest.py -- run the bot over levels, headless, and compare the results.

    python3 tools/bot/backtest.py run --policy wander               # every level
    python3 tools/bot/backtest.py run --policy wander --levels 0-9 'Water\\Water01'
    python3 tools/bot/backtest.py run --policy wander --seeds 1,2,3 -j 4 --label try2
    python3 tools/bot/backtest.py compare RUN_A RUN_B               # or one run: against the baseline
    python3 tools/bot/backtest.py bless RUN                         # the run becomes the baseline
    python3 tools/bot/backtest.py check-determinism --policy wander --levels 0-4
    python3 tools/bot/backtest.py levels                            # the JJ.GAM levels
    python3 tools/bot/backtest.py run --policy collect --watch --levels 'Water\\Water01'  # on screen

A run is one process per (level, seed): the game with KAROO_BOT, started
straight in the level (KAROO_START_LEVEL), on the fixed clock, headless and
unthrottled.  Each plays one life and writes a summary (src/bot/botrun.h).
Each process gets its own working directory, laid out as launch.sh lays out
run/, so parallel runs share nothing they write.

Results go to run/backtest/<date>-<sha>-<label>/: run.json (what was run),
results.json (every summary), and per level and seed its summary, state dump,
recording, action log (src/bot/botrun.h) and game log, and with --trace its BotView trace (src/bot/botview.h).  RUN arguments are such a directory, or its name.  The
baseline is tests/bot/baseline.json: the blessed outcome of each level and
seed for one policy.

The design is docs/BOT_DESIGN.md, section 4.
"""
import argparse
import concurrent.futures
import datetime
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GAME = os.path.join(REPO, "game")
RESULTS = os.path.join(REPO, "run", "backtest")
BASELINE = os.path.join(REPO, "tests", "bot", "baseline.json")

sys.path.insert(0, os.path.join(REPO, "tools"))
import karoosave  # noqa: E402  -- the JJ.GAM level table

# The replay suite's clock and seed (tests/manifest.json defaults).
DT = "0.016667"
SEEDS = "12345"

# Headless and unthrottled, as tools/replaytest.py runs a replay.
HEADLESS_ENV = {
    "KAROO_HEADLESS": "1",
    "KAROO_SKIP_LAUNCHER": "1",
    "KAROO_RENDER_FX": "nodraw",
    "KAROO_FLIP_FX": "noblt",
    "KAROO_NOVSYNC": "1",
    "KAROO_SLOWFRAME_MS": "0",
}

# What a --watch run takes from the caller's environment to reach the display.
WATCH_PASS = ("DISPLAY", "XAUTHORITY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR",
              "DBUS_SESSION_BUS_ADDRESS")

# An outcome counts as a pass only when it is this.
PASS = "complete"


# ── levels ────────────────────────────────────────────────────────────────

def all_levels():
    return karoosave.load_levels(os.path.join(GAME, "JJ.GAM"))


def select_levels(specs):
    """JJ.GAM indices from index ranges ("0-9"), indices and names."""
    names = all_levels()
    if not specs:
        return list(range(len(names)))
    lower = [n.lower() for n in names]
    out = []
    for spec in specs:
        if "-" in spec and spec.replace("-", "").isdigit():
            a, b = spec.split("-")
            out.extend(range(int(a), int(b) + 1))
        elif spec.isdigit():
            out.append(int(spec))
        elif spec.lower() in lower:
            out.append(lower.index(spec.lower()))
        else:
            sys.exit("no level %r (see: backtest.py levels)" % spec)
    bad = [i for i in out if not 0 <= i < len(names)]
    if bad:
        sys.exit("no level index %s; there are %d" % (bad, len(names)))
    return sorted(set(out))


# ── one game ──────────────────────────────────────────────────────────────

def build(build_dir):
    r = subprocess.run(["cmake", "--build", build_dir, "-j%d" % os.cpu_count()],
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    if r.returncode:
        sys.exit("build failed:\n" + r.stderr[-4000:])
    exe = os.path.join(build_dir, "OpenRoo")
    if not os.path.exists(exe):
        sys.exit("%s missing: backtest runs the Linux build" % exe)
    return exe


def make_workdir(path, exe):
    """A working directory as launch.sh makes run/: links to the game data,
    the executable, and the harness's openroo.ini."""
    os.makedirs(os.path.join(path, "SavedGames"), exist_ok=True)
    os.makedirs(os.path.join(path, "highscores"), exist_ok=True)
    for name in os.listdir(GAME):
        if name in ("SavedGames", "highscores", "ProgableControl.sav", "Karoo.exe", "res"):
            continue
        dst = os.path.join(path, name)
        if not os.path.lexists(dst):
            os.symlink(os.path.join(GAME, name), dst)
    shutil.copy(os.path.join(REPO, "data", "openroo.ini.default"),
                os.path.join(path, "openroo.ini"))
    shutil.copy2(exe, os.path.join(path, "OpenRoo"))


def play(job):
    """Plays one (level, seed); returns its summary, with the outcome "crash"
    or "no-summary" if the game left none."""
    exe, out, level, name, seed, policy, extra_env, timeout, trace, watch = job
    work = os.path.join(out, "work")
    make_workdir(work, exe)
    env = {"HOME": os.environ.get("HOME", ""), "PATH": os.environ.get("PATH", "")}
    if watch:
        # On screen, paced by the display: vsync stays on and each frame is
        # one fixed step, so at 60 Hz the bot plays at the game's own speed.
        for name_ in WATCH_PASS:
            if name_ in os.environ:
                env[name_] = os.environ[name_]
        env["KAROO_SKIP_LAUNCHER"] = "1"
    else:
        env.update(HEADLESS_ENV)
    env.update({
        "KAROO_BOT": policy,
        "KAROO_START_LEVEL": str(level),
        "KAROO_FIXED_DT": DT,
        "KAROO_SEED": str(seed),
        "KAROO_BOT_SUMMARY": os.path.join(out, "summary.json"),
        "KAROO_STATE_DUMP": os.path.join(out, "state.json"),
        "KAROO_RECORD": os.path.join(out, "bot.rec"),
        "KAROO_BOT_ACTIONS": os.path.join(out, "actions.jsonl"),
        "KAROO_BOT_MODEL_LOG": os.path.join(out, "model.jsonl"),
        "KAROO_BOT_FOE_LOG": os.path.join(out, "foes.jsonl"),
        "KAROO_BOT_BOMB_LOG": os.path.join(out, "bombs.jsonl"),
        "KAROO_RECORD_LABEL": name,
        "KAROO_AUTO_EXIT_SECS": str(timeout),
    })
    if trace:
        env["KAROO_BOT_TRACE"] = os.path.join(out, "trace.jsonl")
    env.update(extra_env)
    t0 = time.time()
    try:
        r = subprocess.run(["./OpenRoo"], cwd=work, env=env, timeout=timeout + 30,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        rc = r.returncode
    except subprocess.TimeoutExpired:
        rc = None
    wall = time.time() - t0
    for log in ("karoo_hooks.log",):
        if os.path.exists(os.path.join(work, log)):
            shutil.move(os.path.join(work, log), os.path.join(out, log))
    shutil.rmtree(work, ignore_errors=True)

    path = os.path.join(out, "summary.json")
    if os.path.exists(path):
        with open(path) as f:
            s = json.load(f)
    else:
        s = {"outcome": "crash" if rc not in (0, 1) else "no-summary",
             "level_index": level, "level": name}
    s["seed"] = seed
    s["wall_s"] = round(wall, 2)
    s["exit_code"] = rc
    with open(os.path.join(out, "summary.json"), "w") as f:
        json.dump(s, f, indent=2)
    return s


def key(s):
    return "%d/%s" % (s["level_index"], s["seed"])


# ── commands ──────────────────────────────────────────────────────────────

def git(*args):
    return subprocess.run(["git", *args], cwd=REPO, capture_output=True,
                          text=True).stdout.strip()


def run_all(args, out):
    exe = build(args.build_dir)
    names = all_levels()
    levels = select_levels(args.levels)
    seeds = [int(s) for s in args.seeds.split(",")]
    extra = dict(e.split("=", 1) for e in args.env)
    os.makedirs(out, exist_ok=True)
    meta = {
        "policy": args.policy, "levels": levels, "seeds": seeds, "dt": DT,
        "env": extra, "sha": git("rev-parse", "HEAD"),
        "dirty": bool(git("status", "--porcelain", "--untracked-files=no")),
        "started": datetime.datetime.now().isoformat(timespec="seconds"),
        "exe_sha256": hashlib.sha256(open(exe, "rb").read()).hexdigest(),
    }
    with open(os.path.join(out, "run.json"), "w") as f:
        json.dump(meta, f, indent=2)

    jobs = []
    for lvl in levels:
        for seed in seeds:
            d = os.path.join(out, "levels", "%02d-%s" % (lvl, seed))
            os.makedirs(d, exist_ok=True)
            jobs.append((exe, d, lvl, names[lvl], seed, args.policy, extra,
                         args.timeout if not args.watch else max(args.timeout, 1200),
                         args.trace, args.watch))

    results = []
    t0 = time.time()
    with concurrent.futures.ProcessPoolExecutor(max_workers=1 if args.watch else args.jobs) as pool:
        for s in pool.map(play, jobs):
            results.append(s)
            if not args.quiet:
                print("  %-34s seed %-6s %-10s %-8s %s" % (s.get("level", "?")[:34], s["seed"],
                                                           s["outcome"], s.get("death_cause", ""),
                                                           s.get("note", "")),
                      flush=True)
    results.sort(key=lambda s: (s["level_index"], s["seed"]))
    meta["wall_s"] = round(time.time() - t0, 1)
    with open(os.path.join(out, "run.json"), "w") as f:
        json.dump(meta, f, indent=2)
    with open(os.path.join(out, "results.json"), "w") as f:
        json.dump(results, f, indent=2)
    return results, meta


def summarise(results):
    n = len(results)
    passed = sum(1 for s in results if s["outcome"] == PASS)
    by = {}
    for s in results:
        k = s["outcome"] + (":" + s["death_cause"] if s.get("death_cause") else "")
        if s.get("note", "").startswith("no route"):
            k += " (no route)"
        by[k] = by.get(k, 0) + 1
    print("completed %d/%d (%.0f%%)" % (passed, n, 100.0 * passed / n if n else 0))
    for k in sorted(by, key=lambda k: -by[k]):
        print("  %-20s %d" % (k, by[k]))


def cmd_run(args):
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    label = args.label or args.policy
    out = os.path.join(RESULTS, "%s-%s-%s" % (stamp, git("rev-parse", "--short", "HEAD"), label))
    results, meta = run_all(args, out)
    print("%d runs in %.1fs -> %s" % (len(results), meta["wall_s"], os.path.relpath(out, REPO)))
    summarise(results)
    if os.path.exists(BASELINE):
        base = load_baseline()
        if base.get("policy") == args.policy:
            print()
            return compare(base["results"], results, "baseline", "this run")
    return 0


def resolve_run(name):
    for p in (name, os.path.join(RESULTS, name)):
        if os.path.exists(os.path.join(p, "results.json")):
            return p
    sys.exit("no run %r (looked in %s)" % (name, os.path.relpath(RESULTS, REPO)))


def load_run(name):
    with open(os.path.join(resolve_run(name), "results.json")) as f:
        return json.load(f)


def load_baseline():
    with open(BASELINE) as f:
        return json.load(f)


def compare(a, b, name_a, name_b):
    """Prints how each level and seed changed from a to b, regressions first.
    Returns 1 if any went from complete to not, else 0."""
    ia = {key(s): s for s in a}
    ib = {key(s): s for s in b}
    both = set(ia) & set(ib)
    if len(both) < max(len(ia), len(ib)):
        print("comparing the %d level runs both have (%s %d, %s %d)"
              % (len(both), name_a, len(ia), name_b, len(ib)))
    a = [ia[k] for k in both]
    b = [ib[k] for k in both]
    rows = []
    for k in sorted(both, key=lambda k: tuple(int(x) for x in k.split("/"))):
        sa, sb = ia[k], ib[k]
        oa, ob = sa["outcome"], sb["outcome"]
        ca, cb = sa.get("death_cause", ""), sb.get("death_cause", "")
        if (oa, ca) == (ob, cb):
            continue
        rank = 0 if oa == PASS and ob != PASS else 2 if ob == PASS and oa != PASS else 1
        name = sb.get("level", "?")
        rows.append((rank, k, name, oa + (":" + ca if ca else ""), ob + (":" + cb if cb else "")))
    rows.sort(key=lambda r: r[0])
    regressions = sum(1 for r in rows if r[0] == 0)
    fixes = sum(1 for r in rows if r[0] == 2)
    pa = sum(1 for s in a if s["outcome"] == PASS)
    pb = sum(1 for s in b if s["outcome"] == PASS)
    print("%s: %d/%d completed, %s: %d/%d completed; %d regressed, %d fixed, %d otherwise changed"
          % (name_a, pa, len(a), name_b, pb, len(b), regressions, fixes,
             len(rows) - regressions - fixes))
    tag = {0: "REGRESSED", 1: "changed", 2: "fixed"}
    for rank, k, name, oa, ob in rows:
        print("  %-9s %-6s %-30s %s -> %s" % (tag[rank], k, name[:30], oa, ob))
    return 1 if regressions else 0


def cmd_compare(args):
    if args.b:
        return compare(load_run(args.a), load_run(args.b), args.a, args.b)
    return compare(load_baseline()["results"], load_run(args.a), "baseline", args.a)


def cmd_bless(args):
    path = resolve_run(args.run)
    with open(os.path.join(path, "run.json")) as f:
        meta = json.load(f)
    keep = ("level_index", "level", "seed", "outcome", "death_cause", "note",
            "gems_collected", "gems_required", "frames")
    results = [{k: s[k] for k in keep if k in s} for s in load_run(args.run)]
    os.makedirs(os.path.dirname(BASELINE), exist_ok=True)
    with open(BASELINE, "w") as f:
        json.dump({"policy": meta["policy"], "sha": meta["sha"], "dt": meta["dt"],
                   "results": results}, f, indent=1)
        f.write("\n")
    print("baseline: %s, %d results, from %s" % (meta["policy"], len(results),
                                                 os.path.basename(path)))
    return 0


def cmd_check_determinism(args):
    """Runs the selection twice; every summary and recording must match."""
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    runs = []
    for i in (1, 2):
        out = os.path.join(RESULTS, "%s-determinism-%d" % (stamp, i))
        args.quiet = True
        run_all(args, out)
        runs.append(out)
    bad = set()
    for lvl_dir in sorted(os.listdir(os.path.join(runs[0], "levels"))):
        a, b = (os.path.join(r, "levels", lvl_dir) for r in runs)
        for fname in ("summary.json", "bot.rec"):
            fa, fb = os.path.join(a, fname), os.path.join(b, fname)
            if not (os.path.exists(fa) and os.path.exists(fb)):
                print("  %s: %s missing" % (lvl_dir, fname))
                bad.add(lvl_dir)
                continue
            if fname == "summary.json":
                ja, jb = (json.load(open(p)) for p in (fa, fb))
                for j in (ja, jb):
                    j.pop("wall_s", None)
                same = ja == jb
            else:
                same = open(fa, "rb").read() == open(fb, "rb").read()
            if not same:
                print("  %s: %s differs" % (lvl_dir, fname))
                bad.add(lvl_dir)
    n = len(os.listdir(os.path.join(runs[0], "levels")))
    print("determinism: %d of %d level runs identical" % (n - len(bad), n))
    return 1 if bad else 0


def cmd_levels(args):
    for i, n in enumerate(all_levels()):
        print("%3d  %s" % (i, n))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def run_opts(p):
        p.add_argument("--policy", required=True, help="KAROO_BOT: the bot's policy")
        p.add_argument("--levels", nargs="*", default=[],
                       help="indices, ranges (0-9) or names; default every level")
        p.add_argument("--seeds", default=SEEDS, help="comma-separated KAROO_SEEDs")
        p.add_argument("-j", "--jobs", type=int, default=os.cpu_count())
        p.add_argument("--timeout", type=int, default=120,
                       help="wall seconds a single level may take")
        p.add_argument("--env", action="append", default=[], metavar="NAME=VALUE",
                       help="more environment for the game, e.g. KAROO_BOT_STUCK_S=30")
        p.add_argument("--watch", action="store_true",
                       help="play each level on screen, one at a time, at the game's own speed "
                            "(vsync on); needs a display")
        p.add_argument("--trace", action="store_true",
                       help="write each level's BotView trace (trace.jsonl)")
        p.add_argument("--build-dir", default=os.path.join(REPO, "build"))
        p.add_argument("--quiet", action="store_true")

    p = sub.add_parser("run", help="run the bot over levels")
    run_opts(p)
    p.add_argument("--label", help="names the results directory; default the policy")
    p.set_defaults(fn=cmd_run)

    p = sub.add_parser("compare", help="compare two runs, or one against the baseline")
    p.add_argument("a")
    p.add_argument("b", nargs="?")
    p.set_defaults(fn=cmd_compare)

    p = sub.add_parser("bless", help="make a run the baseline")
    p.add_argument("run")
    p.set_defaults(fn=cmd_bless)

    p = sub.add_parser("check-determinism", help="run twice; the runs must match")
    run_opts(p)
    p.set_defaults(fn=cmd_check_determinism)

    p = sub.add_parser("levels", help="list the levels")
    p.set_defaults(fn=cmd_levels)

    args = ap.parse_args()
    sys.exit(args.fn(args))


if __name__ == "__main__":
    main()
