# Replay tests

Recorded input, replayed deterministically, with the resulting game state
asserted. See `REPLAY_PLAN.md` for how it works and what was proven along the
way; this file is the operating manual.

```bash
python3 tools/replaytest.py              # run every catalogued recording
python3 tools/replaytest.py --list       # what is catalogued, and why
python3 tools/replaytest.py NAME         # run one
python3 tools/replaytest.py --no-fast    # render every frame for real
```

## `--fast` is the default

A replay already runs on the virtual clock and with vsync off, so the loop is
uncapped. What is left is real work, and most of the per-frame half of it is
rendering the game does not need to do to be tested: the suite asserts *game
state*, and draw calls are pure output that nothing reads back.

Fast mode sets two switches the DLL already has -- `KAROO_D3D_FX=nodraw`, which
makes `RenderDevice::Draw`/`DrawBuffer` return without reaching the device, and `KAROO_FLIP_FX=noblt`, which skips the Blt in
`RenderDevice::PresentImage`. Measured over the whole suite:

| Run | Wall | Result |
|---|---|---|
| `--no-fast` | 216.0 s | 12/12 |
| default (fast) | 171.6 s (-21%) | 12/12 |

Measured 2026-09-05 over all twelve recordings. On the seven-recording suite
this was first measured on it was 106.5 s -> 79.2 s, -26%; the ratio moved
because `bombstart-crash` now opts out and renders for real.

Per recording that is roughly 5.9 s of fixed launch cost plus 3.9 ms a frame,
which fast mode takes to about 2.2 ms. Every recording passes either way,
water01 and freezestart with all 31 asserted fields, which is the evidence that
draw calls are pure output: nothing in the game reads them back, so removing
them cannot move the simulation.

`--no-fast` renders every frame for real. `--fast` still exists so a run can
say so explicitly.

## `--headless` -- no window, no graphics, no display

`--fast` stops the game *asking* the driver to draw. `--headless` removes the
driver: with `KAROO_HEADLESS=1`, `RenderDevice::Create` (`src/d3d/createdevice.cpp`)
creates no Direct3D objects at all, and the game's one window is created
message-only so Wine needs no display driver for it. Nothing appears on
screen, nothing takes focus, no desktop mode is switched, and the run works
with `DISPLAY` unset entirely.

```bash
python3 tools/replaytest.py --headless          # the whole suite, in the background
bash launch.sh --headless --auto-exit 40        # one run
```

This is the mode to use when the suite is running while somebody is working on
the machine, or on a box with no X server at all. It is orthogonal to
`--fast`: fast skips the draw calls, headless removes what they would have
gone to, and the two compose.

**How it works.** A headless `RenderDevice` is a `RenderDevice` whose Direct3D
device is `NULL`. Every method that would reach the device checks for that and
does nothing; everything else is the real code. The pieces that matter:

- The mode list is a fixed table of the 4:3 sizes (`kHeadlessSizes`), 32-bit
  first and then 16-bit, so `openroo.ini`'s `mode` means the same thing on
  every machine and in a real run: 3 is 1024x768x32, 10 is 800x600x16.
- The pipeline state, transforms, material and light are kept in a shadow
  (`PipelineState`) that the getters answer from, with or without a device.
  Vertex buffers keep a CPU copy of their vertices too.
- Textures are converted exactly as on a real device -- same format choice,
  same pixels -- and then dropped, unless `KAROO_TEXTURE_DUMP` wants them
  hashed. `tools/texdump.py` therefore gives the same answer headless and not.
- Draws that build their vertices in game code (`SceneQuad_Draw`) still read
  every one of them before `Draw` finds there is no device to give them to,
  which is what keeps the guard below working.

The game reads little of what the driver reports (the mode list, and which
texture format and depth buffer it got), and none of it reaches the simulation,
so nothing here has to be a recording of a real driver.

### The one recording that never runs fast

`bombstart-crash` carries `"fast": false` in the manifest and always renders for
real, whatever the suite was invoked with.

It exists to guard the CRASH.md fault: a bad pointer handed to the driver, which
faulted *inside* a strided draw (since replaced by `SceneQuad_Draw` interleaving the vertices itself). Skip the draw call and that pointer is
never handed over, so the only class of fault this suite has actually caught in
anger would go unnoticed. It costs about 10 s of the run, which is a better
trade than losing the guard.

Any recording can opt out the same way. Deleting the field would make the suite
fast unconditionally -- and would retire that guard, so do it deliberately.

Two things that look like wins and are not, both measured rather than assumed:

- Skipping the Blt on its own is worth 2%. It is bundled in because it is
  free, not because it matters.
- Turning Wine's tracing off (`WINEDEBUG=-all`; Proton's `PROTON_LOG_DIR`
  otherwise sets `+seh,+loaddll,+mscoree`) is worth 1-2%, inside the noise --
  and it would silently break `grep -c c000001d steam-123456.log`, which is the
  project's primary evidence that no call site was missed, because that string
  only appears in `trace:seh` lines. Our own VEH does not catch illegal
  instructions: a run with 9 UD2 hits logged 0 VEH lines. Not worth 2%.

Exit code 0 means every selected recording behaved exactly as `manifest.json`
says it should. Non-zero means one did not, and the output names the field.

A replay is unattended, not headless: the harness passes `--skip-launcher`, so
nobody has to click through the launcher dialog, but the game still opens a
window and renders each run on screen. These tests need a display.

## What is here

| Path | What it is |
|---|---|
| `manifest.json` | The catalogue: one entry per recording, with its description, save fixture and expected end state |
| `*.rec` | The recordings themselves (input streams, decoded by `tools/replay.py`) |
| `saves/<name>/` | Save fixtures — a single declarative `FIXTURE` file: the decoded slot fields, plus the SHA-256 the generated bytes must match |

## Determinism, and why the save fixture matters

A recording is a stream of *keypresses*, not a stream of game actions. When a
recording "loads save slot 4", what it actually replays is: down, down, down,
Enter. If `SavedGames/` holds a different set of slots than it did at record
time, those keypresses land somewhere else and the run diverges immediately —
a broken fixture, not a replay bug.

So a recording does not describe its save state, it **carries** it, in a
fixture named by `manifest.json`:

```bash
python3 tools/karoosave.py snapshot tests/saves/<name>   # capture SavedGames/
python3 tools/karoosave.py restore  tests/saves/<name>   # rebuild it exactly
python3 tools/karoosave.py verify   tests/saves/<name>   # exit 1 if it drifted
```

Since the 42-byte save record is fully decoded (see HOOKS.md), a fixture is
**generated, not stored**. `FIXTURE` declares each save file's decoded slot
fields and `restore` synthesises the bytes with `build_slot`.  Since
version 3 an in-use slot stores only **name, level and lives**; score,
completion, time and the unused tail are written as 0 (in use 1), so a
fixture carries no played history.  An empty slot is the word `empty`.
`snapshot` refuses a slot with history unless given `--trim`; `convert`
rewrites a version-2 fixture this way. A fixture directory holds that one file and no
`.sav` copies, so it is readable and diffable in review: you can see at a
glance which level slot 4 points at.

**Every generated file is then checked against a SHA-256 recorded in
`FIXTURE`, and a mismatch is a hard error.** That check is the reason this
design is safe. Generating from fields makes the fixture depend on
`karoosave.py`: a bug or a changed default in `build_slot` would otherwise
silently alter what every recording loads, and the symptom would be a replay
diverging thousands of frames later — one of the most expensive failures to
diagnose here. The hash converts that into an immediate, named error before
the game is launched. `replaytest.py` reports it as a FAIL for that recording
and does not start the game.

So if you edit a field in `FIXTURE`, `restore` will refuse it until you
re-`snapshot`. That is deliberate: re-snapshot only once you know which side
is wrong.

`restore` also deletes any save file the fixture does not name, so a leftover
slot from an earlier test cannot appear in the menu and catch a keypress.
`replaytest.py` restores the fixture before every run, so this is automatic —
the commands above are for building a fixture and for debugging.

Two details the format preserves rather than derives. Filenames are recorded
per slot: the shipped set has slot 3 on disk as `JJ3.sav`, uppercase, with no
lowercase counterpart, and Wine finds it case-insensitively. And `GAM.DAT` is
declared as a zero-byte `blob`; a non-empty non-slot file cannot be generated,
and `snapshot` refuses rather than pretending otherwise.

Old byte-copy fixtures (`file <sha> <name>` lines, `.sav` copies alongside)
are still restored by copying, so an unconverted fixture keeps working.

`JJ.GAM` is deliberately **not** part of a fixture: it is shared game data. But
a save slot stores a level *index* into it, so editing `JJ.GAM` would silently
repoint every fixture at a different level. Its hash is recorded in `FIXTURE`
and checked on restore, and a mismatch is a hard error.

The other two preconditions are `KAROO_FIXED_DT` and `KAROO_SEED` (see
REPLAY_PLAN.md Stages A and A2). Both are written into the recording's header
at record time, and `replaytest.py` refuses to run if the manifest disagrees
with the header rather than reporting a diff that was never a real comparison.

Music used to be a third precondition: with it on, every recording ran exactly
one frame longer, and nothing else moved (the per-frame hashes were identical).
The cause was the message loop, not the music.  A recording ends when the game
quits itself, with `PostQuitMessage` and, with music on, `CDM::stop()` in the
same tick.  Stopping the MCI device posts an `MM_MCINOTIFY` to the window;
`WM_QUIT` is only delivered once the queue is otherwise empty, and the loop
handled one message per rendered frame, so the notification cost one extra
frame.  `windev::runMessageLoop` now drains every pending message before each
frame, so the frame count no longer depends on what else is queued.  The suite
passes 16/16 with music on or off.

If a run ever ends on `frames_run` one or more frames over, with the hashes up
to the expected length unchanged, suspect a message posted by the quit path.

## Capturing a new recording

1. **Get `SavedGames/` into the state the recording should start from,** then
   freeze it. If the scenario starts from a particular level:

   ```bash
   python3 tools/karoosave.py levels Egypt
   python3 tools/karoosave.py write --slot 4 --match 'Egypt\Race' --all
   python3 tools/karoosave.py snapshot tests/saves/egypt-race
   ```

   The save record is fully decoded, so `write` synthesises the slot outright;
   `--all` writes every other slot as an empty slot and deletes anything else
   in `SavedGames/`, so the starting state is exactly what you asked for rather
   than whatever the directory happened to hold. Edit saves only while the game
   is **not** running — it reads every slot once at startup.

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

### Assert on the level you named

If a recording carries on past the level under test — into the next level, or
just back to the menu — the *last in-level frame* is no longer that level's end
state. `water01` completes Water01 and then launches the following level, so its
top-level dump block reads `gems 0/12`, `time_limit_s 220`, `level_complete 0`:
the next level starting.

The dump therefore also carries an **`at_completion`** block, latched the first
time the completion flag goes non-zero and locked when it clears, so a later
level cannot overwrite it. Assert on `at_completion.*` for anything about the
level the entry is named for:

```json
"at_completion.level_score": 320,
"at_completion.gems_collected": 15,
"at_completion.level_complete": 1
```

The latch refreshes while the flag stays set rather than freezing on its leading
edge, because `CalculateLevelScore` writes the score a frame or two after the
flag flips. `completed_a_level` is `false` and `at_completion` is `null` for a
recording that never finishes one.

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

### Running the whole suite

Recordings run back to back, and the second launch will wedge — hanging before
the DLL writes a single log line — if the previous run's `KarooOwn.exe` and
`wineserver` have not finished shutting down. The harness polls for that between
runs. If you ever see a run produce an empty `karoo_hooks.log` and no hash file,
that is this, not a replay failure: the recording will pass on its own. Clear it
with `pkill -f KarooOwn.exe; pkill -f wineserver`.

**The suite runs unthrottled.** A replay is on the virtual clock, so nothing in
the game paces itself against wall time — but presentation still blocked on the
display refresh, pinning every run at the monitor's rate (600 frames per 10.05 s
wall = 59.7 fps) and making a replay cost about as long as it took to play.
`replaytest.py` therefore launches with vsync disabled at the driver
(`VSYNC_OFF` in the harness: `vblank_mode`, `MESA_VK_WSI_PRESENT_MODE`,
`__GL_SYNC_TO_VBLANK` — all three, so the backend wined3d picked does not have
to be detected). Frames then run at roughly 230 fps and the two-recording suite
takes **35 s instead of 102 s**.

The wait is below Wine: passing `DDFLIP_NOVSYNC` through the ddraw proxy was
tried first and changed nothing.  The backend is Direct3D 9 now, and under
DXVK those driver switches do not reach the present, so the harness also sets
`KAROO_NOVSYNC=1`, which makes the device present immediately, whichever Direct3D
layer is in use. The game has no vsync option of its own.

This changes when a finished frame reaches the screen, not what is in it — every
frame is still rendered and presented, and nothing reads back present timing.
Checked rather than assumed: both recordings reproduce their catalogued end
state and exact frame counts unthrottled, and two consecutive `water01` runs
produce byte-identical 4150-frame hash logs.

Set `KAROO_NO_TURBO=1` to put the vsync limit back — useful when you want to
*watch* a replay at playing speed. A variable already set in your environment is
left alone either way. Capture (`replaytest.py record`) is never unthrottled;
a human plays that one in real time.

### `bombstart-crash`

Named for the CRASH.md guard-page fault it used to reproduce: before that bug
was fixed, replaying this recording unattended crashed at frame 797 *every time*,
while the bug was otherwise intermittent (~1 run in 5) and had never been caught
caught by an unattended run at all. That is what a deterministic replay buys.

The crash is now fixed (`Game::RenderSceneObjects` left `textureCoords[1]`
uninitialised), so the recording runs to its own end and the entry stands as a
regression guard. It was re-baselined on 2026-08-31: two consecutive runs
produced byte-identical 1036-frame hash logs and identical end-state dumps.

It is a menu-and-early-gameplay test, not a full level run — the recorded
session quits after about ten seconds of play. A scoring or level-completion
regression would not be caught by it; `water01` covers that.

### `water01`

The full-level counterpart. Loads slot 4, plays `Water\Water01` end to end
collecting all 15 gems, completes it at frame 3921 for a level score of 320,
then launches into the following level and quits a few seconds in. 4147 records
over 4150 frames, 2102 with a key held — four times the length of
`bombstart-crash`, and byte-identical across runs.

It pins two things nothing else does:

- **Level completion and the score formula.** The score reproduces exactly from
  six fields, `75 + 0 + 0 + 92 + 75 + 78 = 320`, and the running total steps
  `1077 -> 1397`.
- **The all-items bonus actually awarded.** Both runs recorded in REPLAY_PLAN.md
  had it withheld — one because items were short, one because the `+0x4220B`
  flag was set. Here `items_collected >= items_available` with the flag clear,
  so the `items_available * 5` term is paid. That is a score branch nothing else
  exercises.

Assert on `at_completion.*`, not the top-level block — see above.

## The level report — an all-levels check that is not a replay

`tests/levelreport/` is a different kind of baseline, and it is worth reaching
for *first* when a change could affect level parsing, object construction or
scoring, because it covers all 80 levels rather than the one a recording visits.

The game already has the test built in. `Game::LoadSounds` polls
`input_key_down(KEY_L)` three times just before it acquires the fixed sound
buffers, and if L is down it calls `WriteLevelReport`. That walks
every level in the game file — `SetCurrentLevelName`, `OpenLevelFile`,
`SetupLevelObjects`, `CalculateLevelScore` — and writes two text files into the
game directory:

| File | Contents |
|---|---|
| `LevelReport.txt` | one row per level: world, bonus flag, per-object-type counts (crystals, fields, enemies, elevators, glue, bombs, …), par time, cumulative score, level path; totals at the end |
| `ScriptTexts.txt` | every instruction-script text, level by level |

```bash
python3 tools/levelreport.py            # run and compare against the baseline
python3 tools/levelreport.py --bless    # re-baseline (say so in the commit)
python3 tools/levelreport.py --keep     # leave the produced files in place
python3 tools/levelreport.py --baseline # keep this run's text in run/levelreport-baseline/
python3 tools/levelreport.py --from DIR # compare outputs already in DIR, no run
```

**The gate is `tests/levelreport.json`**, not the text.  The harness parses the
outputs (plus `LeoRecords.txt`) into its own canonical record per level and per
`.leo` file and stores only a SHA-256 of each, with the counts that make a
failure readable.  A failure names the level and the counts that moved, or says
"counts equal; content moved" when only script text or record detail changed.
The game's own text lives only in `tests/levelreport/` in the private repo; when
that directory (or `run/levelreport-baseline/`) exists it is diffed too, and a
difference fails the run.  `--bless` rewrites the JSON and, where it exists,
`tests/levelreport/`.

Exit 0 means the run matches. The whole thing takes about
**17 seconds** — the dump itself is ~9 s — so there is no reason to skip it.

**It needs no determinism settings.** Unlike a replay there is no clock, no
seed and no save fixture in play: the report is computed from the level files
alone, before the first frame boundary. That also means it is orthogonal to the
replay suite — it catches content-level regressions a replay would only show as
a divergence thousands of frames later, and it says *which level* changed.

**How the run is driven.** `KAROO_LEVEL_REPORT=1` makes `KarooOwn.exe`
(`levelreport.cpp`) answer exactly the three `VK_L` queries `LoadSounds` makes,
and then, once the report is written, drive the menu's Quit node so the game
shuts itself down. The quit is not a convenience: `WriteLevelReport` logs
`GAME: level report created` *before* it `fclose`s the two files, so killing the
process on that log line can truncate the output. The answer is one-shot for
the same reason — L is a live game key, and a blanket override would hold it
down for the rest of the run.

**Side effects are contained.** `WriteLevelReport` ends by rewriting the
high-score table (and leaves a `jj.hsc.hsc` behind), and the game rewrites
`SavedGames/` on exit. None of that is part of the test and all of it is
committed repo state, so the harness snapshots `highscores/` and `SavedGames/`
before launching and restores them afterwards, including on a crash or Ctrl-C.

**The outputs are generated, not stored.** `LevelReport.txt` and
`ScriptTexts.txt` in the repo root are gitignored; only `tests/levelreport/`
holds the baseline. They used to be checked in at the root — the baseline there
is byte-identical to what the harness now reproduces, which is how it was
confirmed in the first place.
