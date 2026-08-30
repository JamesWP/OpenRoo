# Replay tests

Recorded input, replayed deterministically, with the resulting game state
asserted. See `REPLAY_PLAN.md` for how it works and what was proven along the
way; this file is the operating manual.

```bash
python3 tools/replaytest.py              # run every catalogued recording
python3 tools/replaytest.py --list       # what is catalogued, and why
python3 tools/replaytest.py NAME         # run one
```

Exit code 0 means every selected recording behaved exactly as `manifest.json`
says it should. Non-zero means one did not, and the output names the field.

## What is here

| Path | What it is |
|---|---|
| `manifest.json` | The catalogue: one entry per recording, with its description, save fixture and expected end state |
| `*.rec` | The recordings themselves (input streams, decoded by `tools/replay.py`) |
| `saves/<name>/` | Save fixtures — a byte-for-byte copy of `SavedGames/` plus a `FIXTURE` manifest of SHA-256s |

## Determinism, and why the save fixture matters

A recording is a stream of *keypresses*, not a stream of game actions. When a
recording "loads save slot 4", what it actually replays is: down, down, down,
Enter. If `SavedGames/` holds a different set of slots than it did at record
time, those keypresses land somewhere else and the run diverges immediately —
a broken fixture, not a replay bug.

`karoosave.py set --seed-from` prepares a slot by hand, but it is not
reproducible: the result depends on which slot you seeded from and on what that
slot happened to contain. So a recording does not describe its save state, it
**carries** it:

```bash
python3 tools/karoosave.py snapshot tests/saves/<name>   # freeze SavedGames/
python3 tools/karoosave.py restore  tests/saves/<name>   # put it back, exactly
python3 tools/karoosave.py verify   tests/saves/<name>   # exit 1 if it drifted
```

`restore` also deletes any save file the fixture does not name, so a leftover
slot from an earlier test cannot appear in the menu and catch a keypress.
`replaytest.py` restores the fixture before every run, so this is automatic —
the commands above are for building a fixture and for debugging.

`JJ.GAM` is deliberately **not** part of a fixture: it is shared game data. But
a save slot stores a level *index* into it, so editing `JJ.GAM` would silently
repoint every fixture at a different level. Its hash is recorded in `FIXTURE`
and checked on restore, and a mismatch is a hard error.

The other two preconditions are `KAROO_FIXED_DT` and `KAROO_SEED` (see
REPLAY_PLAN.md Stages A and A2). Both are written into the recording's header
at record time, and `replaytest.py` refuses to run if the manifest disagrees
with the header rather than reporting a diff that was never a real comparison.

## Capturing a new recording

1. **Get `SavedGames/` into the state the recording should start from,** then
   freeze it. If the scenario starts from a particular level:

   ```bash
   python3 tools/karoosave.py levels Egypt
   python3 tools/karoosave.py set 4 --match 'Egypt\Race' --seed-from 2
   python3 tools/karoosave.py snapshot tests/saves/egypt-race
   ```

   `--seed-from` copies a slot the game already loads, so the still-undecoded
   save fields keep values it accepts. Edit saves only while the game is **not**
   running — it reads every slot once at startup.

2. **Record.** This launches the game windowed with the fixed clock and fixed
   seed already set, so what you record is replayable:

   ```bash
   python3 tools/replaytest.py record egypt-race \
       --level 'Egypt\Race' --saves tests/saves/egypt-race \
       --description "Loads slot 4, runs the Egypt race to the exit collecting
                      all 12 gems, no deaths. Covers the gem-gated exit and the
                      all-items bonus."
   ```

   Play the scenario, then quit the game normally. The entry is written into
   `manifest.json` with no end-state assertions yet.

3. **Verify it replays,** twice. A recording that does not reproduce is not a
   test:

   ```bash
   python3 tools/replaytest.py egypt-race
   ```

4. **Bless it** once you believe the run. This writes the run's own end state
   into the manifest as the expectation:

   ```bash
   python3 tools/replaytest.py --bless egypt-race
   ```

   Then run it once more and confirm it passes, and edit one expected value to
   confirm it fails. An assertion nobody has watched fail is not an assertion.

### Writing the description

The description is the point of the catalogue — it is how a future task decides
whether a recording covers what it is about to change. Say what the recording
*does* and what it *exercises*, and be honest about what it does not reach.
"Plays BombStart" is useless; "loads slot 4, plays ~10s, picks up one item and
kills one foe, never completes the level" tells the next reader that this
recording will not catch a scoring regression.

## Removing a recording

Delete its entry from `manifest.json`, its `.rec`, and its fixture directory if
no other entry uses it. Nothing else references them by name — except prose, so
grep before deleting one that documentation cites (`bombstart-crash` is cited by
`CRASH.md`).

## Catalogue

Run `python3 tools/replaytest.py --list` for the current contents.
`manifest.json` is the source of truth; the notes below are the standing
context that does not belong in a JSON field.

### `bombstart-crash`

Named for the CRASH.md guard-page fault it used to reproduce: before that bug
was fixed, replaying this recording headless crashed at frame 797 *every time*,
while the bug was otherwise intermittent (~1 run in 5) and had never been caught
headless at all. That is what a deterministic replay buys.

The crash is now fixed (`Game::RenderSceneObjects` left `textureCoords[1]`
uninitialised), so the recording runs to its own end and the entry stands as a
regression guard. It was re-baselined on 2026-08-31: two consecutive runs
produced byte-identical 1036-frame hash logs and identical end-state dumps.

It is a menu-and-early-gameplay test, not a full level run — the recorded
session quits after about ten seconds of play. A scoring or level-completion
regression would **not** be caught by it. That gap is the obvious next
recording to capture.
