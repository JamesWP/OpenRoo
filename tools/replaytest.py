#!/usr/bin/env python3
r"""Ka'roo replay test harness — REPLAY_PLAN.md Stage E.

Runs the recordings catalogued in tests/manifest.json: restore the save
fixture, replay the recorded input under the fixed clock and fixed seed, then
compare the end state and the crash classification against what the manifest
says should happen.

    python3 tools/replaytest.py                 # run every recording
    python3 tools/replaytest.py bombstart-crash # run one
    python3 tools/replaytest.py --list          # what is catalogued
    python3 tools/replaytest.py --bless NAME    # re-baseline expect.state
    python3 tools/replaytest.py record NAME --description "..." \
            --saves tests/saves/bombstart --level 'Forest\BombStart'

Exit code is the point: 0 = every selected recording behaved as catalogued,
1 = at least one did not.

Why the harness does the asserting.  The plan called for KAROO_ASSERT inside
the DLL setting the process exit code.  Stage A established that
`launch.sh --skip-launcher` exits non-zero whatever the game returns — the code
comes from the game's own WinMain after the posted window close — so a
DLL-set exit code cannot reach the caller.  The DLL therefore only writes
KAROO_STATE_DUMP, and the comparison lives here, where the expected values sit
next to the recording in a file a human can read and edit.

Determinism preconditions, all of which this script enforces rather than
assumes (REPLAY_PLAN.md Stages A/A2):

  - KAROO_FIXED_DT and KAROO_SEED must match the recording's own header, or
    the replay diverges.  Checked against the header before launching.
  - SavedGames/ must hold exactly the bytes it held at record time, because
    the recording replays the *keypresses* that pick a save slot, not the load
    itself.  A different slot layout lands the menu somewhere else and the run
    diverges immediately.  That is what the save fixtures are for.
  - The replay ends on the recording's own length, never on wall-clock time.
    The DLL does that itself now (clock.cpp); --auto-exit is only a safety net
    for a run that wedges.
"""

import argparse
import json
import os
import signal
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS = os.path.join(REPO, "tests")
MANIFEST = os.path.join(TESTS, "manifest.json")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import replay as recfmt          # noqa: E402  — the recording decoder


# ── manifest ──────────────────────────────────────────────────────────────

def load_manifest():
    with open(MANIFEST) as fh:
        m = json.load(fh)
    if m.get("version") != 1:
        sys.exit("%s: unsupported manifest version %r" % (MANIFEST, m.get("version")))
    return m


def save_manifest(m):
    with open(MANIFEST, "w") as fh:
        json.dump(m, fh, indent=2)
        fh.write("\n")


def entry_defaults(m, e):
    d = dict(m.get("defaults", {}))
    d.update({k: v for k, v in e.items() if v is not None})
    return d


def select(m, names):
    recs = m["recordings"]
    if not names:
        return recs
    by_name = {r["name"]: r for r in recs}
    out = []
    for n in names:
        if n not in by_name:
            sys.exit("no recording named %r (see --list)" % n)
        out.append(by_name[n])
    return out


# ── running ───────────────────────────────────────────────────────────────

def restore_fixture(entry, verbose=False):
    """Rebuild SavedGames/ from the recording's fixture. True if it is usable.

    A v2 fixture is *generated* from the decoded slot fields and then checked
    against the hash recorded for each file, so a fixture that has been edited
    without re-snapshotting fails here — before the game is launched — instead
    of showing up as a replay divergence thousands of frames later.
    """
    fixture = entry.get("saves")
    if not fixture:
        print("  ! no save fixture — the run inherits whatever SavedGames holds")
        return True
    path = os.path.join(REPO, fixture)
    stdout = None if verbose else subprocess.DEVNULL
    r = subprocess.run([sys.executable,
                        os.path.join(REPO, "tools", "karoosave.py"),
                        "restore", path],
                       cwd=REPO, stdout=stdout)
    return r.returncode == 0


def check_header(entry, cfg, rec_path):
    """A replay under different determinism settings than the recording is not
    a test, it is noise.  Refuse it up front rather than reporting a diff."""
    hdr, frames = recfmt.load(rec_path)
    problems = []
    want_dt = float(cfg["dt"]) if cfg.get("dt") else 0.0
    if abs(hdr["dt"] - want_dt) > 1e-12:
        problems.append("manifest dt=%s but the recording was made at %.9f"
                        % (cfg.get("dt"), hdr["dt"]))
    want_seed = int(cfg["seed"]) if cfg.get("seed") else None
    if want_seed is not None and (not hdr["seed_set"] or hdr["seed"] != want_seed):
        problems.append("manifest seed=%s but the recording was made with %s"
                        % (want_seed,
                           hdr["seed"] if hdr["seed_set"] else "no seed"))
    return hdr, frames, problems


# Wall-clock ceiling for ONE recording — the stuck detector.
#
# A healthy recording takes 11-14 s headless (12-15 s with a display), so 25 s
# is roughly a 2x margin on the slowest observed run and still catches a wedge
# in seconds rather than minutes.  This is deliberately much tighter than the
# old max(300, auto_exit*6): that bound existed only to stop an *indefinite*
# hang, and in practice a wedged Proton prefix or a game stuck in
# futex_wait_multiple would sit there for five minutes per recording and turn
# the suite into an hour of nothing.  Both failure modes are documented in
# GAMETICK_PLAN.md's standing hazards, and both are visible within 25 s.
#
# It is a *stuck* detector, not a benchmark: exceeding it is reported as STUCK,
# distinct from FAIL, because the usual cause is the environment (a surviving
# wineserver, a stale Karoo.exe) rather than the change under test.  Raise it
# with KAROO_STUCK_SECONDS if a genuinely slower machine needs the room.
STUCK_SECONDS = int(os.environ.get("KAROO_STUCK_SECONDS", "25"))


def wait_for_quiet(timeout=60):
    """Wait for a previous run's game process to actually be gone.

    Running two recordings back to back wedged the second launch: it hung
    before our DLL wrote a single log line, because the previous run's
    Karoo.exe / wineserver had not finished shutting down and the new launch
    sat waiting on the prefix.  Each recording passes on its own, so this is a
    harness problem, not a replay one.  Poll rather than sleeping a fixed
    amount, so the common case (already quiet) costs nothing.
    """
    import time
    deadline = time.time() + timeout
    while time.time() < deadline:
        r = subprocess.run(["pgrep", "-f", r"Karoo(Own)?\.exe"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode != 0:          # nothing matched
            time.sleep(2)              # let wineserver finish behind it
            return True
        time.sleep(1)
    print("  ! a previous Karoo.exe is still running after %ds — the launch "
          "below may wedge" % timeout)
    return False


# Presentation vsync, and why the harness turns it off.
#
# A replay runs on the virtual clock (KAROO_FIXED_DT), so no part of the game
# paces itself against wall time — the loop is uncapped as far as the game is
# concerned.  Presentation is not: the flip blocks on the display refresh, which
# pinned every run at the monitor's rate (measured: 600 frames per 10.05 s wall
# = 59.7 fps) and made a replay cost about as long as it took to play.
#
# The wait is below Wine — passing DDFLIP_NOVSYNC through the ddraw proxy was
# tried and changed nothing — so these are the driver-side switches: Mesa GL,
# Mesa's Vulkan WSI, and the NVIDIA GL equivalent.  Setting all three covers
# whichever backend wined3d picked without having to detect it.
#
# This changes when a finished frame reaches the screen, not what is in it:
# every frame is still rendered and presented, the clock is virtual, and nothing
# in the game reads back present timing.  Confirmed rather than assumed — with
# it on, both recordings reproduce their catalogued end state and their exact
# frame counts.
#
# KAROO_NO_TURBO=1 restores the vsync-limited pace, and a variable already set
# in the caller's environment is left alone, so a run can be slowed deliberately
# to watch it.
VSYNC_OFF = {
    "vblank_mode": "0",                       # Mesa OpenGL
    "MESA_VK_WSI_PRESENT_MODE": "immediate",  # Mesa Vulkan WSI
    "__GL_SYNC_TO_VBLANK": "0",               # NVIDIA OpenGL
}


def apply_turbo(env):
    """Unthrottle presentation unless the caller asked not to."""
    if os.environ.get("KAROO_NO_TURBO", "") not in ("", "0"):
        return env
    for k, v in VSYNC_OFF.items():
        env.setdefault(k, v)
    return env


# --fast: stop rendering, keep simulating.
#
# Turbo above removed the *pacing* on presentation; this removes the rendering
# work itself, using two switches the DLL already has:
#
#   KAROO_D3D_FX=nodraw   the com_proxy device returns D3D_OK from all six
#                         DrawPrimitive* entry points without forwarding
#   KAROO_FLIP_FX=noblt   Direct3D_FlipPrimaryFrame skips the Blt to the back
#                         buffer (it still Flips)
#
# Measured on this machine, whole suite, 12 recordings (2026-09-05):
#
#   --no-fast   216.0 s
#   default     171.6 s      -21%
#
# 12/12 pass either way.  On the 7-recording suite this was first measured on,
# the same change was 106.5 s -> 79.2 s, -26%; the ratio moved because
# bombstart-crash now opts out and renders for real.
#
# Per recording the shape is about 5.9 s of fixed launch cost plus 3.9 ms a
# frame, and fast mode takes the per-frame figure to about 2.2 ms.  water01 and
# freezestart pass with all 31 asserted fields either way, which is the point:
# draw calls are pure output.  Nothing in the game reads them back, so removing
# them cannot move the simulation -- and if it ever did, the suite would say so.
#
# THIS IS THE DEFAULT.  --no-fast restores the full render path.
#
# One recording opts out of it permanently (unless headless is specified), and the reason is worth keeping in
# view.  bombstart-crash exists to guard the CRASH.md fault: a bad pointer
# handed to ddraw, which faulted *inside* DrawPrimitiveStrided.  Skipping the
# draw call means that pointer is never handed over, so that entire class of
# fault -- the only one this suite has actually caught in anger -- would go
# unnoticed.  Rather than trade it away for the ~10 s that recording costs, the
# manifest entry carries "fast": false and it always runs the real render path.
#
# So the suite is fast by default and still guards the thing it was built to
# guard.  Any recording can opt out the same way; see entry_fast() below.
#
# Two things deliberately NOT done here, both measured:
#
#   Skipping the Blt alone is worth 2%.  It is bundled into --fast because it
#   is free, not because it matters.
#
#   Turning Wine's tracing off (WINEDEBUG=-all, which Proton's PROTON_LOG_DIR
#   otherwise sets to +seh,+loaddll,+mscoree) is worth 1-2%, inside the noise,
#   and it would silently break the project's primary evidence that no call
#   site was missed: `grep -c c000001d steam-123456.log` counts trace:seh
#   lines that would no longer be written.  Our own VEH does not catch illegal
#   instructions -- checked, a run with 9 UD2 hits logged 0 VEH lines -- so
#   there is no replacement for that check.  Not worth 2%.
FAST_ENV = {
    "KAROO_D3D_FX":  "nodraw",
    "KAROO_FLIP_FX": "noblt",
}


def apply_fast(env, fast):
    """Skip the render work.  Caller-set values win, so a run can override."""
    if not fast:
        return env
    for k, v in FAST_ENV.items():
        env.setdefault(k, v)
    return env


def entry_fast(entry, fast):
    """Whether THIS recording runs fast.

    A recording whose value is guarding something on the render path sets
    "fast": false in the manifest and is never run fast, however the suite was
    invoked.  --no-fast still turns it off for everything.
    """
    return fast and entry.get("fast", True)


def launch(entry, cfg, rec_path, dump_path, hash_path, fast=True,
           headless=True):
    env = apply_fast(apply_turbo(dict(os.environ)), fast)
    env["KAROO_REPLAY"] = rec_path
    env["KAROO_STATE_DUMP"] = dump_path
    env["KAROO_HASH_LOG"] = hash_path
    env["KAROO_FIXED_DT"] = str(cfg.get("dt", ""))
    env["KAROO_SEED"] = str(cfg.get("seed", ""))
    auto_exit = int(cfg.get("timeout", 120))
    # --headless replaces DirectDraw with the in-DLL null device
    # (karoo-hooks/nullddraw.cpp) and makes the game's window message-only, so
    # the run needs no display, opens nothing on screen and takes no focus.
    # It implies --skip-launcher.  Orthogonal to --fast: fast skips the draw
    # CALLS, headless removes the driver underneath them.
    cmd = ["bash", os.path.join(REPO, "launch.sh"),
           "--headless" if headless else "--skip-launcher",
           "--auto-exit", str(auto_exit)]

    # Hard wall-clock bound.  --auto-exit is a *game-time* budget, so it does
    # not bound wall time at all: if the game wedges, or the machine suspends
    # mid-run, an unbounded wait blocks the whole suite indefinitely (seen
    # 2026-08-31 on a laptop that slept).  The margin is deliberately wide —
    # a slow run must not be reported as a hang — but finite.
    budget = STUCK_SECONDS

    # start_new_session so the whole tree gets signalled: killing the bash
    # child alone would leave Proton/Wine running and the next recording would
    # then fail for the wrong reason.
    proc = subprocess.Popen(cmd, cwd=REPO, env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            start_new_session=True)
    # launch.sh exits non-zero regardless of how the run went (Stage A note),
    # so its return code is deliberately ignored; crashcheck.py and the state
    # dump are the oracles.
    try:
        proc.wait(timeout=budget)
        return False
    except subprocess.TimeoutExpired:
        pass

    print("  STUCK: no exit after %ds wall clock (--auto-exit was %ds of game "
          "time); killing the run.\n"
          "         A healthy recording finishes in 11-14s, so this is a wedge,\n"
          "         not a slow run.  Suspect the environment first: a surviving\n"
          "         wineserver or a stale Karoo.exe from an earlier run wedges\n"
          "         the next launch (see GAMETICK_PLAN.md standing hazards).\n"
          "         Raise KAROO_STUCK_SECONDS if this machine is genuinely slower."
          % (budget, auto_exit))
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
    # Backstop: the game can outlive the launcher script.
    subprocess.run(["pkill", "-f", r"Karoo(Own)?\.exe"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    wait_for_quiet()
    return True


def crash_verdict():
    """0 clean / 1 the known crash / 2 a different crash / 3 undetermined."""
    r = subprocess.run([sys.executable, os.path.join(REPO, "tools", "crashcheck.py"),
                        "--quiet"], cwd=REPO)
    return r.returncode


CRASH_NAME = {0: "none", 1: "known", 2: "different", 3: "undetermined"}


def lookup(actual, key):
    """Resolve a dotted key such as at_completion.total_score.

    Returns (found, value).  Nested blocks matter because the last in-level
    frame is not the end of the level under test once a recording carries on
    into the next one — at_completion.* is the block that describes the level
    the entry is named for."""
    cur = actual
    for part in key.split("."):
        if not isinstance(cur, dict) or part not in cur:
            return False, None
        cur = cur[part]
    return True, cur


def compare_state(expected, actual):
    """Return a list of human-readable mismatches.  Only the fields the
    manifest names are compared — an unlisted field is deliberately not
    asserted, so a test can pin a gem count without pinning a float position."""
    bad = []
    for key, want in sorted(expected.items()):
        found, got = lookup(actual, key)
        if not found:
            bad.append("%s: expected %r but the dump has no such field" % (key, want))
        elif got != want:
            bad.append("%s: expected %r, got %r" % (key, want, got))
    return bad


def run_one(m, entry, bless=False, fast=True, headless=False, verbose=False):
    cfg = entry_defaults(m, entry)
    name = entry["name"]
    rec_path = os.path.join(TESTS, entry["file"])
    dump_path = os.path.join(REPO, "replaytest-%s.json" % name)
    hash_path = os.path.join(REPO, "replaytest-%s.hash" % name)

    print("=" * 72)
    print("%s — %s" % (name, entry.get("level", "?")))
    
    start = time.perf_counter()

    if verbose:
        print("  %s" % entry.get("description", "").strip())

    if not os.path.exists(rec_path):
        print("  FAIL: recording %s is missing" % rec_path)
        return False

    hdr, frames, problems = check_header(entry, cfg, rec_path)
    if problems:
        for p in problems:
            print("  FAIL: %s" % p)
        return False

    if verbose:
        print("  %d frames, dt=%.9f seed=%u, fixture=%s"
              % (len(frames), hdr["dt"], hdr["seed"], entry.get("saves", "<none>")))

    if not restore_fixture(entry, verbose):
        print("  FAIL: could not rebuild the save fixture %s — see the error "
              "above. The game was not launched." % entry.get("saves"))
        return False
    wait_for_quiet()
    for stale in (dump_path, hash_path):
        if os.path.exists(stale):
            os.remove(stale)

    if not entry_fast(entry, fast) and headless:
        if verbose:
            print("ignoring fastonly test due to headless being enabled")

    if launch(entry, cfg, rec_path, dump_path, hash_path,
              fast=entry_fast(entry, fast) or headless, headless=headless):
        print("  FAIL (STUCK): run exceeded the %ds stuck detector and was "
              "killed.\n"
              "         Treat this as a hang, not a state mismatch — "
              "karoo_hooks.log ends where it wedged." % STUCK_SECONDS)
        return False

    verdict = crash_verdict()
    want_crash = entry.get("expect", {}).get("crash", "none")
    ok = True

    if verbose:
        print("  crash: %s (expected %s)" % (CRASH_NAME.get(verdict, verdict), want_crash))
    if CRASH_NAME.get(verdict) != want_crash:
        print("  FAIL: crash classification differs — see karoo_hooks.log / "
              "steam-123456.log, and CRASH.md for the fingerprint")
        ok = False

    nframes = 0
    if os.path.exists(hash_path):
        with open(hash_path) as fh:
            nframes = sum(1 for _ in fh)
        if verbose:
            print("  hash log: %d frames -> %s" % (nframes, os.path.basename(hash_path)))
    want_frame = entry.get("expect", {}).get("crash_frame")
    if want_frame is not None and nframes and abs(nframes - want_frame) > 2:
        print("  FAIL: expected to stop around frame %d, stopped at %d"
              % (want_frame, nframes))
        ok = False

    actual = None
    if os.path.exists(dump_path):
        with open(dump_path) as fh:
            actual = json.load(fh)
        if verbose:
            print("  end state: reason=%s frame=%s" % (actual.get("reason"), actual.get("frame")))
    else:
        if verbose:
            print("  end state: no dump written")

    if bless:
        if actual is None:
            print("  cannot bless: no state dump")
            return False
        skip = ("reason", "frame", "pos", "at_completion")
        keep = {k: v for k, v in actual.items()
                if not k.startswith("_") and k not in skip}
        # Flatten the completion block into dotted keys.  Its own "frame" is
        # dropped for the same reason the top-level one is: it is a position in
        # the run, not a property of the level.
        done = actual.get("at_completion")
        if isinstance(done, dict):
            keep.update({"at_completion.%s" % k: v for k, v in done.items()
                         if k != "frame"})
        entry.setdefault("expect", {})["state"] = keep
        entry["expect"]["crash"] = CRASH_NAME.get(verdict, "undetermined")
        # A blessed run reached its own end; any stop-early expectation from a
        # previous baseline no longer applies.
        entry["expect"].pop("crash_frame", None)
        # frames_run is already inside state; a second frame count would be a
        # duplicate expectation that could drift out of step with it.
        print("  blessed %d fields into the manifest" % len(keep))
        return True

    expected = entry.get("expect", {}).get("state") or {}
    if expected:
        if actual is None:
            print("  FAIL: manifest expects end state but no dump was written")
            ok = False
        else:
            bad = compare_state(expected, actual)
            for b in bad:
                print("  FAIL: %s" % b)
            if not bad:
                if verbose:
                    print("  end state matches all %d asserted field(s)" % len(expected))
            ok = ok and not bad
    else:
        if verbose:
            print("  no end-state assertions (expect.state is empty)")

    end = time.perf_counter()

    duration_s = end-start
    print("  %s (%02.2f)" % ("PASS" if ok else "FAIL", duration_s))
    return ok


# ── recording capture ─────────────────────────────────────────────────────

CAPTURE_HELP = r"""
Capturing a recording
---------------------
1. Put SavedGames/ into the exact state the recording should start from, then
   freeze it:

       python3 tools/karoosave.py set 4 --match BombStart --seed-from 2
       python3 tools/karoosave.py snapshot tests/saves/<name>

2. Run this command.  The game launches windowed, with the fixed clock and the
   fixed seed already set, recording to tests/<name>.rec.  Play the scenario,
   then quit the game normally.

3. The entry is written into tests/manifest.json with your --description.  It
   starts with no end-state assertions; run --bless once you trust the run to
   pin the fields it should reproduce.

Record and replay must both use the same dt and seed, or the replay diverges.
That is why this command sets them rather than leaving them to the shell.
"""


def cmd_record(args):
    m = load_manifest()
    if any(r["name"] == args.name for r in m["recordings"]) and not args.force:
        sys.exit("a recording named %r is already catalogued; pass --force to "
                 "re-capture it" % args.name)

    cfg = m.get("defaults", {})
    dt = args.dt or cfg.get("dt", "0.016667")
    seed = args.seed or cfg.get("seed", "12345")
    rec_path = os.path.join(TESTS, args.name + ".rec")

    if args.saves:
        fixture = os.path.join(REPO, args.saves)
        if not os.path.exists(os.path.join(fixture, "FIXTURE")):
            sys.exit("%s is not a save fixture — run `karoosave.py snapshot %s` "
                     "first, with SavedGames/ in the state the recording should "
                     "start from" % (args.saves, args.saves))
        # Restore it now so what is recorded is what will be replayed.
        subprocess.run([sys.executable, os.path.join(REPO, "tools", "karoosave.py"),
                        "restore", fixture], cwd=REPO, check=True)

    env = dict(os.environ)
    env["KAROO_RECORD"] = rec_path
    env["KAROO_RECORD_LABEL"] = args.level or args.name
    env["KAROO_FIXED_DT"] = dt
    env["KAROO_SEED"] = seed
    env["KAROO_HASH_LOG"] = os.path.join(REPO, "replaytest-%s.hash" % args.name)
    env["KAROO_STATE_DUMP"] = os.path.join(REPO, "replaytest-%s.json" % args.name)

    print("Recording to %s" % rec_path)
    print("  dt=%s seed=%s fixture=%s" % (dt, seed, args.saves or "<none>"))
    print("Play the scenario, then quit the game normally.")
    subprocess.run(["bash", os.path.join(REPO, "launch.sh")], cwd=REPO, env=env)

    if not os.path.exists(rec_path):
        sys.exit("no recording was written — was KAROO_RECORD reaching the game? "
                 "(it must be in launch.sh's `env -i` block)")

    hdr, frames = recfmt.load(rec_path)
    keyed = sum(1 for f in frames if recfmt.pressed(f[2]))
    print("\ncaptured %d frames, %d with a key held" % (len(frames), keyed))

    entry = {
        "name": args.name,
        "file": os.path.basename(rec_path),
        "saves": args.saves,
        "level": args.level,
        "description": args.description,
        "scenario": args.scenario or args.description,
        "expect": {"crash": "none", "state": {}},
        "notes": "Captured %d frames. No end-state assertions yet — run "
                 "`replaytest.py --bless %s` once a replay is trusted."
                 % (len(frames), args.name),
    }
    m["recordings"] = [r for r in m["recordings"] if r["name"] != args.name]
    m["recordings"].append(entry)
    m["recordings"].sort(key=lambda r: r["name"])
    save_manifest(m)
    print("added %r to %s" % (args.name, os.path.relpath(MANIFEST, REPO)))
    print("\nNow verify it replays before trusting it:")
    print("  python3 tools/replaytest.py %s" % args.name)


def cmd_list(m):
    print("%d recording(s) in %s\n" % (len(m["recordings"]), os.path.relpath(MANIFEST, REPO)))
    for r in m["recordings"]:
        exp = r.get("expect", {})
        print("%-22s %s" % (r["name"], r.get("level", "?")))
        print("  %s" % r.get("description", "").strip())
        print("  fixture=%s  expect crash=%s, %d asserted field(s)"
              % (r.get("saves", "<none>"), exp.get("crash", "none"),
                 len(exp.get("state") or {})))
        if r.get("notes"):
            print("  note: %s" % r["notes"])
        print()


def main():
    # `record` is dispatched by hand rather than with add_subparsers: argparse
    # cannot combine a subparser with the trailing `names` positional that the
    # run mode wants, and the run mode is the common case.
    if len(sys.argv) > 1 and sys.argv[1] == "record":
        p = argparse.ArgumentParser(
            prog="replaytest.py record",
            description=CAPTURE_HELP,
            formatter_class=argparse.RawDescriptionHelpFormatter)
        p.add_argument("name", help="identifier, also the .rec basename")
        p.add_argument("--description", required=True,
                       help="what the recording does and what it is good for")
        p.add_argument("--scenario", help="short form, e.g. 'gem run, no deaths'")
        p.add_argument("--level", help=r"level name, e.g. 'Forest\BombStart'")
        p.add_argument("--saves",
                       help="save fixture to start from, e.g. tests/saves/bombstart")
        p.add_argument("--dt")
        p.add_argument("--seed")
        p.add_argument("--force", action="store_true", help="replace an existing entry")
        return cmd_record(p.parse_args(sys.argv[2:])) or 0

    ap = argparse.ArgumentParser(
        description=__doc__ + CAPTURE_HELP,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("names", nargs="*", help="recordings to run (default: all)")
    ap.add_argument("--list", action="store_true",
                    help="show the catalogue and exit")
    ap.add_argument("--fast", dest="fast", action="store_true", default=True,
                    help="skip the render work (KAROO_D3D_FX=nodraw, "
                         "KAROO_FLIP_FX=noblt). This is the DEFAULT; the flag "
                         "is kept so it can be stated explicitly.")
    ap.add_argument("--no-fast", dest="fast", action="store_false",
                    help="render every frame for real. Slower (~216 s vs "
                         "~172 s for the suite), and the only way to exercise "
                         "the ddraw draw path for every recording.")
    ap.add_argument("--no-headless", dest="headless", action="store_false", 
                    help="disable the headless mode which is enabled by default")
    ap.add_argument("--headless", dest="headless", action="store_true", default=True,
                    help="run with no window and no graphics at all: "
                         "DirectDraw is replaced by the in-DLL null device and "
                         "the game's window is made message-only, so the suite "
                         "can run in the background without stealing focus and "
                         "without a display. Implies --skip-launcher. also means"
                         " test fast flag is ignored")
    ap.add_argument("--bless", action="store_true",
                    help="write the run's own end state into the manifest as "
                         "the expectation — only after you believe the run")
    ap.add_argument("--own", action="store_true",
                    help="run our own executable KarooOwn.exe (launch.sh --own) "
                         "instead of the patched Karoo.exe + karoo_hooks.dll")
    ap.add_argument("--verbose", "-v", dest="verbose", default=False, action="store_true")
    args = ap.parse_args()
    if args.own:
        os.environ["EXE"] = "KarooOwn.exe"   # launch.sh picks it up

    m = load_manifest()
    if args.list:
        cmd_list(m)
        return 0

    entries = select(m, args.names)
    if args.bless and len(entries) != 1:
        sys.exit("--bless takes exactly one recording name")

    results = [(e["name"], run_one(m, e, bless=args.bless, fast=args.fast,
                                   headless=args.headless, verbose=args.verbose))
               for e in entries]
    if args.bless:
        save_manifest(m)
        print("manifest updated")
        return 0 if all(ok for _, ok in results) else 1

    print("=" * 72)
    failed = [n for n, ok in results if not ok]
    print("%d/%d passed" % (len(results) - len(failed), len(results)))
    for n in failed:
        print("  FAILED: %s" % n)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
