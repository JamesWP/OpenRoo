# Test recordings

## `bombstart-crash.rec`

A hand-played recording (James, 2026-08-30): menu navigation, load save slot 4
(`Forest\BombStart`), then gameplay. 1034 frames, 313 with a key held.

It is two things at once.

**A Stage D fixture.** Replaying it reproduces the recorded run's per-frame
state checksum exactly (REPLAY_PLAN.md Stage D).

**A deterministic reproducer for the crash in CRASH.md.** Replaying it headless
crashes at frame 797, every time — three consecutive runs produced
byte-identical 797-line hash logs and the same guard-page fault. The bug is
otherwise intermittent (~1 run in 5) and had never been caught headless.

```bash
# restore the save state the recording expects, then replay
KAROO_FIXED_DT=0.016667 KAROO_SEED=12345 \
  KAROO_REPLAY=tests/bombstart-crash.rec KAROO_HASH_LOG=/tmp/replay.hash \
  bash launch.sh --headless --auto-exit 40
python3 tools/crashcheck.py     # exits 1 on the known crash, 0 when fixed
```

The recording depends on save slot 4 holding `Forest\BombStart`, because it
replays the keypresses that select it from the menu:

```bash
python3 tools/karoosave.py set 4 --match BombStart --seed-from 2
```

If the slot differs, the replayed menu navigation lands somewhere else and the
run diverges immediately — that is a broken fixture, not a replay bug.
