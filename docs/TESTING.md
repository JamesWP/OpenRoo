# Replay tests

Recorded input, replayed deterministically, with the resulting game state
asserted. See `REPLAY_PLAN.md` for how it works and what was proven along the
way; this file is the operating manual.

```bash
python3 tools/replaytest.py              # run every catalogued recording
python3 tools/replaytest.py --list       # what is catalogued, and why
python3 tools/replaytest.py NAME         # run one
python3 tools/replaytest.py --all        # the suite plus the playthroughs
python3 tools/replaytest.py --no-headless  # open a window, render every frame for real
```

## `--headless` is the default -- no window, no graphics, no display

With `KAROO_HEADLESS=1`, `GLRenderDevice::Create` (`src/gl/createdevice.cpp`)
creates no OpenGL context at all, and the game's one window is created
message-only so Wine needs no display driver for it. Nothing appears on
screen, nothing takes focus, no desktop mode is switched, and the run works
with `DISPLAY` unset entirely.

A headless run also skips the render work itself, with two switches the DLL
already has -- `KAROO_RENDER_FX=nodraw`, which makes
`RenderDevice::Draw`/`DrawBuffer` return without reaching the device, and
`KAROO_FLIP_FX=noblt`, which skips the Blt in `RenderDevice::PresentImage`.
The suite asserts *game state*, and draw calls are pure output that nothing
reads back, so removing them cannot move the simulation.

```bash
python3 tools/replaytest.py                     # the whole suite, in the background
bash launch.sh --headless --auto-exit 40        # one run
```

**How it works.** A headless `RenderDevice` is a `RenderDevice` with no OpenGL
context (`Native::active` is false). Every method that would reach OpenGL checks for that and
does nothing; everything else is the real code. The pieces that matter:

- The mode list is a fixed table of the 4:3 sizes (`kHeadlessSizes`), 32-bit
  first and then 16-bit, so `openroo.ini`'s `mode` means the same thing on
  every machine and in a real run: 3 is 1024x768x32, 10 is 800x600x16.
- The pipeline state, transforms, material and light are kept in a shadow
  (`PipelineState`) that the getters answer from, with or without a device.
  Vertex buffers keep a CPU copy of their vertices too.
- Textures are converted exactly as on a real device -- same format choice,
  same pixels -- and then dropped, unless `KAROO_TEXTURE_DUMP` wants them
  hashed.

The game reads little of what the driver reports (the mode list, and which
texture format and depth buffer it got), and none of it reaches the simulation,
so nothing here has to be a recording of a real driver.

## `--no-headless` -- render for real

`--no-headless` passes `--skip-launcher` instead, so nobody has to click
through the launcher dialog, but the game opens a window and renders every
frame on screen. It needs a display. It is the only way to exercise the draw
path: `bombstart-crash` guards the CRASH.md fault, a bad pointer that faulted
*inside* a strided draw, and only a `--no-headless` run hands the driver a
pointer at all. `tools/drawtrace.py` uses it too.

Turning Wine's tracing off (`WINEDEBUG=-all`) was measured as well: worth 1-2%,
inside the noise, and it would silently break `grep -c c000001d steam-123456.log`,
the project's primary evidence that no call site was missed. Not worth it.

Exit code 0 means every selected recording behaved exactly as `manifest.json`
says it should. Non-zero means one did not, and the output names the field.

## What is here

| Path | What it is |
|---|---|
| `manifest.json` | The catalogue: one entry per recording, with its description and expected end state |
| `*.rec` | The recordings themselves (delta-compressed input streams, decoded by `tools/replay.py`; format in `src/testing/record.cpp`), each bundling the `SavedGames/` files it starts from |

## Recording a long playthrough

`python3 tools/playthrough.py [series]` records a playthrough across several
sittings. A recording cannot span a game exit, so each sitting is its own
recording (`<series>-01`, `-02`, ...) whose bundled saves are where the
previous one ended. The script prompts for new/continue and your plan, launches
the recording, then asks how it went, catalogues the part (with a `playthrough`
field in the manifest) and offers to commit. End-of-sitting saves are kept in
`run/playthroughs/` so the next sitting can resume from them.

## Determinism, and why the saves are bundled

A recording is a stream of *keypresses*, not a stream of game actions. When a
recording "loads save slot 4", what it actually replays is: down, down, down,
Enter. If `SavedGames/` holds a different set of slots than it did at record
time, those keypresses land somewhere else and the run diverges immediately —
a broken save state, not a replay bug.

So a recording does not describe its save state, it **carries** it: the raw
save files (`jjN.sav`, `GAM.DAT`; a few hundred bytes) are stored in the
recording's header. `replaytest.py` writes them into `run/SavedGames/` before
every run and deletes any other save file there, so a leftover slot from an
earlier test cannot appear in the menu and catch a keypress. `replaytest.py
record` bundles whatever `SavedGames/` holds when it launches the game.
`python3 tools/replay.py <name>.rec` lists the bundled files.

`JJ.GAM` is not bundled: it is shared game data. But a save slot stores a level
*index* into it, so editing `JJ.GAM` would repoint every recording's slots at
different levels.

The other two preconditions are `KAROO_FIXED_DT` and `KAROO_SEED` (see
REPLAY_PLAN.md Stages A and A2). Both are written into the recording's header
at record time, and `replaytest.py` refuses to run if the manifest disagrees
with the header rather than reporting a diff that was never a real comparison.

Music used to be a third precondition: with it on, every recording ran exactly
one frame longer, and nothing else moved.
The cause was the message loop, not the music.  A recording ends when the game
quits itself, with `PostQuitMessage` and, with music on, `CDM::stop()` in the
same tick.  Stopping the MCI device posts an `MM_MCINOTIFY` to the window;
`WM_QUIT` is only delivered once the queue is otherwise empty, and the loop
handled one message per rendered frame, so the notification cost one extra
frame.  `windev::runMessageLoop` now drains every pending message before each
frame, so the frame count no longer depends on what else is queued.  The suite
passes 16/16 with music on or off.

If a run ever ends on `frames_run` one or more frames over, suspect a message posted by the quit path.

## Capturing a new recording

1. **Get `SavedGames/` into the state the recording should start from.** If the scenario starts from a particular level:

   ```bash
   python3 tools/karoosave.py levels Egypt
   python3 tools/karoosave.py write --slot 4 --match 'Egypt\Race' --all
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
       --level 'Egypt\Race' \
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

### Assert on events, not the final frame

A recording usually crosses several levels and deaths, so the last in-level
frame says little about any one of them. The state dump instead carries an
**`events`** list, one full snapshot each time a level **loads** (first
in-level frame), **completes**, the player **dies** (lives drop), or the run **quits** (the last
state seen, with the level it was in), in order.
Each carries `kind`, `level` and the same fields (`gems_collected`,
`level_score`, `total_score`, `lives`, ...). `--bless` flattens them into
dotted keys, plus `event_count` and `frames_run`:

```json
"events.1.kind": "complete",
"events.1.level": "Water\\Water01",
"events.1.level_score": 320,
"events.1.gems_collected": 15
```

A completion is recorded when the complete flag clears (or at the end of the
run), because `CalculateLevelScore` writes the score a frame or two after the
flag flips. Frame numbers and positions are not asserted. A load event's score
fields still show the previous level's values until the new level scores.

### Writing the description

The description is the point of the catalogue — it is how a future task decides
whether a recording covers what it is about to change. Say what the recording
*does* and what it *exercises*, and be honest about what it does not reach.
"Plays BombStart" is useless; "loads slot 4, plays ~10s, picks up one item and
kills one foe, never completes the level" tells the next reader that this
recording will not catch a scoring regression.

## Removing a recording

Delete its entry from `manifest.json`, and its `.rec`. Nothing else references it by name — except prose, so
grep before deleting one that documentation cites (`bombstart-crash` is cited by
`CRASH.md`).

## Catalogue

Run `python3 tools/replaytest.py --list` for the current contents.
`manifest.json` is the source of truth; the notes below are the standing
context that does not belong in a JSON field.

### Running the whole suite

Recordings run back to back, and the second launch will wedge — hanging before
the DLL writes a single log line — if the previous run's `OpenRoo.exe` and
`wineserver` have not finished shutting down. The harness polls for that between
runs. If you ever see a run produce an empty `karoo_hooks.log` and no end-state dump,
that is this, not a replay failure: the recording will pass on its own. Clear it
with `pkill -f OpenRoo.exe; pkill -f wineserver`.

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
tried first and changed nothing.  The backend is OpenGL now, and not every
driver honours those switches, so the harness also sets
`KAROO_NOVSYNC=1`, which makes the device present immediately (swap interval 0),
whatever the driver does. The game has no vsync option of its own.

This changes when a finished frame reaches the screen, not what is in it — every
frame is still rendered and presented, and nothing reads back present timing.
Checked rather than assumed: both recordings reproduce their catalogued end
state and exact frame counts unthrottled.

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
produced identical end-state dumps.

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

Assert on the `complete` event for the level — see above.

## The particle gallery -- every particle effect at once

```bash
KAROO_PARTICLE_GALLERY=1 bash launch.sh --skip-launcher       # from the first theme
KAROO_PARTICLE_GALLERY=Space bash launch.sh --skip-launcher   # from that one
```

A test scene, not a level (`src/app/particlegallery.cpp`): one theme at a
time, every look with a particle system on a floor tile of its own, labelled
with its theme-file keyword and drawn through the same theme draws a level
uses. Bursts and dying effects fire again every few seconds and the speed
trail runs in a circle; the exit is shown open. Left/Right change theme,
Up/Down zoom, Space stops the camera's orbit, Escape quits.

It also opens over a running game from the debug UI (F10): the "Particle
gallery" section's button opens it, and the same section then picks the
theme, the zoom and the orbit. Escape or "back to the game" returns to the
level, which waited where it was. Use it to check a change to the particles
by eye; `KAROO_SCREENSHOT` works here too.
