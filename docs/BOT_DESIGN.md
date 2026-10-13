# A player bot: design

A goal-oriented planner that plays Ka'roo levels, and, more importantly, the
framework that lets us measure it, compare versions of it, and debug it.

Status: agreed design.  Nothing here is built yet.

## 1. Aims

- **Success means completing every level.**  Score does not count.  The
  headline metric is the completion rate over the shipped levels.
- **Standing goal, every level:** collect every gem and reach the exit,
  **without dying**.  Each level is planned on its own and lives are never
  spent, so the bot never has to plan across levels (section 7).
- **Items are only means to an end:** the bot takes an item when it helps
  complete the level (bombs for a block, freeze or protection to get past a
  foe, time when the limit is tight), and ignores it otherwise.
- **Gates:** pick up bombs before blowing up the bombable blocks that wall off
  an area; throw switches to extend bridges.
- **Foes:** avoid them, lure them away from what we want, or lure them onto an
  armed falling tile so they drop with it.
- **Iteration speed:** a change to the bot is judged by running it over the
  whole level corpus headless, in minutes, and diffing the results against a
  baseline.  The framework gets the most effort.

Non-goals for now: playing the menus (the harness starts the level directly);
being fun to watch.

## 2. What the engine already gives us

| Seam | Where | Use |
|---|---|---|
| `input::KeySource` | `src/inputapi/keysource.h` | The bot's output.  It is a key source like the keyboard or a replay, and the game cannot tell them apart.  `exclusive()` keeps a real key from interfering. |
| `const World &` | `src/entities/world.h` | The bot's input.  This is already intended as "a read-only view ... a player AI planning its moves"; constness is deep. |
| Fixed step + seed | `KAROO_FIXED_DT`, `KAROO_SEED` | Determinism.  The sim's only `rand()` calls are the random pickup and the level builder's phases (`player.cpp`, `levelsetup.cpp`). |
| Headless | `KAROO_HEADLESS`, `replaytest.py --headless` | No GL context, no draws, no display. |
| State dump + events | `src/sim/gamestate.cpp`, `KAROO_STATE_DUMP` | An end-of-run JSON with load/complete/death events.  The backtest summary extends this rather than inventing a new one. |
| Recorder | `KAROO_RECORD`, `src/testing/record.cpp` | Every bot run can also write a `.rec`, so any run replays in the real renderer to be watched. |
| Foe pathfinder | `FoePath` (`src/entities/foepath.h`) | Foes chase with a deterministic A*-shaped search.  The bot's model can run the same code to predict them. |

Facts the design leans on:

- **Movement is tank-style and grid-locked:** forward, back, turn left, turn
  right, drop bomb, harakiri (`Player::act*`).  A step takes 200 ms (100 or
  400 ms under the speed effects).  Ice, slides, teleporters, stairs and lifts
  change where a step ends.
- **Falling tile:** it arms when anything occupies the cell.  It falls 1500 ms
  after arming and, unless its param says otherwise, comes back 5000 ms after
  arming (`fallingtile.cpp`).  An occupant at the moment of the fall goes
  with it.  That moment is the lure's target.
- **Foes chase by type:** catchers touch, throwers drop bombs.  Each type has
  its own chase speed, which is a search budget, and some types escort or head
  for switches (`Foe::chooseTarget`).  Each tick a foe re-targets the
  player's cell.  That is what makes luring possible: the player's position
  steers the foe.

## 3. Architecture

```
               const World&                               keys
  ┌────────┐   (each tick)   ┌──────────────┐               ┌────────┐
  │  Game  │ ──────────────▶ │  Perception  │──BotView──┐   │ Motor  │──▶ input::KeySource
  └────────┘                 └──────────────┘           │   └────────┘
                                                        ▼        ▲ cell path / maneuver
     ┌───────────────────────────────────────────────────────┐  │
     │ Executive (monitor): is the plan still valid?          │──┘
     │   prediction vs. reality, preconditions, progress      │
     └───────┬──────────────────────────────▲────────────────┘
             │ replan                       │ next step
     ┌───────▼────────┐  goal set  ┌────────┴──────┐  per action ┌───────────────┐
     │ Goal selection │──────────▶ │ GOAP planner  │───────────▶ │ Tactical layer │
     │ (risk)         │            │ (symbolic A*) │             │ space-time A*, │
     └────────────────┘            └───────┬───────┘             │ maneuvers      │
                                           │ costs, reachability └───────┬───────┘
                                   ┌───────▼─────────────────────────────▼──┐
                                   │ Model: nav graph, distances, route     │
                                   │ optimiser, foe/tile/bomb prediction    │
                                   └────────────────────────────────────────┘
```

Every layer is a plain C++ class over a `BotView` value, with no engine calls.
This one property makes every layer unit-testable without the game, and it
lets the harness serialise exactly what the bot saw.

### 3.1 Perception: `BotView`

A compact, copyable snapshot built from `const World &` once per tick:

- **Grid:** per cell, height, kind, visible contents, passability class,
  slide direction, teleporter destination, and the switch-to-bridge mapping.
- **Player:** cell, facing, mid-step or not, bombs, glides, active timed
  effects and their remaining time, lives, and gems collected vs. required.
- **Foes:** cell, kind, type, held/frozen, dying, and last chase target.
- **Falling tiles:** armed, `armedAt`, fallen/busy, and whether they respawn.
- **Bombs:** cell, fuse remaining, and blast zone.
- **Bridges and lifts:** extended/position.
- **Clock and time left.**

**Knowledge:** the bot starts with perfect knowledge.  It reads everything,
including a bombable block's hidden contents (`field202`).  Some levels take a
human many plays to learn where things are, and the bot should not have to.
All reads go through the perception filter, so a "fair" mode (no hidden
contents) can be added there later without touching the planner.

`BotView` serialises to JSON.  That gives us the trace format, offline replay
of the planner against recorded views, and planner unit tests built from
ASCII maps.

### 3.2 Model

- **Nav graph:** cells and legal moves, with the cost of each move in ms.  It
  covers turning (tank controls make facing part of the state:
  `(u, v, facing)`), ice runs, slides, teleporters, stairs and drops (a fall
  rule, so a high drop is a death edge).  Edges are *conditional* on symbolic
  facts: "block B spent", "bridge S extended".
- **Regions:** connected components under the current facts, plus the facts
  that would join them.  This is how GOAP asks whether an item is reachable
  and what it would take to reach it.
- **Distances:** all-pairs travel times between points of interest (gems,
  items, bomb pickups, blocks, switches, falling tiles, exit), cached per
  fact-set.
- **Route optimiser (the TSP part):** given a start, a set of points and an
  end, it orders them.  Exact Held-Karp DP up to about 15 points, and
  nearest-neighbour plus 2-opt/Or-opt beyond that.  GOAP never orders gems
  itself; it calls this.
- **Predictors:**
  - Foe: run the real `FoePath` on a copy of the map to predict each foe's
    next steps, given a hypothesised player trajectory.
  - Falling tile: exact from the timers above.
  - Bomb: exact from its fuse and its roll.

  The predictors need two fidelities:
  - **L1, the abstract model above.**  Cheap, and used inside search.
  - **L2, the real engine stepped forward from a copy of the World.**  The
    entities hold raw pointers into the World (`EntityContext`) and sound
    voices, so a `World::clone()` is a refactor.  We defer it until the
    calibration numbers (section 4.6) say L1 cannot time a lure.

### 3.3 Goal selection

Score does not count, so there is one goal: all gems, then the exit, alive.
Goal selection is therefore small, and it chooses between *plans*, not goals:

- GOAP proposes the plans; each plan's cost is `time + μ·risk`.
- Risk is the model's estimate of the chance of dying: near-misses with
  predicted foes, tight lure windows, falling tiles crossed close to their
  fall.  It dominates the cost, because a death fails the level.
- Time matters only against the level's time limit.  A plan that overruns it
  is invalid, and that is the one case where a time bonus is worth a detour.
- Items enter only through action preconditions.  `Blast` needs bombs, and a
  route past a guarding foe can use freeze or protection, so the planner takes
  an item when an action needs it.

The weight μ lives in the bot config, so the harness can sweep it.  Selection
re-runs only on a significant event (a block spent, a foe died, a plan
failed), not every tick.

### 3.4 GOAP planner

- **State:** a small fact set, such as `gemsLeft`, `bombs=n`,
  `blockSpent[b]`, `switchOn[s]`, `foeAlive[f]`, `foeAway[f]`,
  `itemTaken[i]`, `atExit`, and the player's abstract position (a point of
  interest).
- **Actions:** macro-actions whose costs come from the model:

  | Action | Preconditions | Effects |
  |---|---|---|
  | `CollectRegion(R)` | R reachable | all gems in R taken (route optimiser) |
  | `Take(item)` | reachable, not guarded | item effect (`bombs+3`, life, ...) |
  | `Blast(block)` | `bombs≥1`, block adjacent cell reachable | `blockSpent`, regions merge |
  | `Throw(switch)` | reachable | `switchOn`, bridge edges open |
  | `LureAway(foe, from)` | foe alive | `foeAway[foe]` for a window |
  | `LureToDrop(foe, tile)` | tile respawned/unarmed, reachable, foe chases | `foeAlive=false` |
  | `Exit` | `gemsLeft=0`, exit reachable | `atExit` |

- **Search:** A* over the facts.  The heuristic is the route optimiser's cost
  for the remaining mandatory points under the most permissive fact-set, which
  is a lower bound.  The search budget is counted in **nodes expanded, never
  wall time**, so the bot stays deterministic.

Example: a gem (or bomb pickup) guarded by foe `f2` becomes the plan
`LureToDrop(f2, t7) → Take(gem) → CollectRegion(A) → Exit`.  `Take` requires
`¬guarded`, and either `LureAway` or `LureToDrop` satisfies that; the planner
picks whichever costs less, risk included.

### 3.5 Tactical layer

The tactical layer turns one GOAP action into a timed cell path.

- **Space-time A*** over `(u, v, facing, t)`.  Foe-occupied cells, predicted by
  the model, are forbidden or penalised, and so are falling tiles that will be
  gone by the time we arrive and bomb zones.
- **Maneuvers** are small parametric scripts with a verification step.  For
  example, `LureToDrop`:
  1. Pick a tile T and a route that steps onto T (which arms it at
     `t_arm`) and off again, so that the foe's predicted path puts it on T
     during `[t_arm + 1500 − ε, t_arm + 1500]`.
  2. Search over the approach timing (wait k steps) and the exit direction.
  3. Accept only if the predictor says the foe is on T at the fall and the
     player is off it and untouched.

### 3.6 Motor

The motor turns a cell path into held keys under tank controls: it turns to
face, holds forward, releases at the cell boundary, and knows that ice and
slides keep moving without input.  It is the only part that knows about keys,
and it is a `KeySource`.

### 3.7 Executive

Each tick the executive compares the current `BotView` with what the plan
predicted.  It checks the foe positions against tolerance, that we are where
the path said we should be, and that the preconditions still hold.  It also
watches progress (time since the last fact change).  On a divergence it
replans at the lowest layer that can absorb it: re-path first, then the
action, then the whole plan.  Every decision goes to the decision log with its
reason.

### 3.8 Placement in the code

- **`src/bot/`, a new group on the `[screens, sim]` line of `layers.yaml`.**
  It needs `entities` (const World), `game` (the game state) and `inputapi`,
  and nothing above it.
- **`app`:** behind `--bot <config>`, it adds the bot as the exclusive key
  source.
- **`tools/bot/`:** the Python orchestration and analysis (section 4).

## 4. Backtest framework

### 4.1 The runner

`botrun` is `OpenRoo --headless --bot cfg.json`, plus engine support:

- **`KAROO_START_LEVEL=<index | Theme\Name | path.jjm>`** starts a fresh game
  straight into the level, skipping the launcher and menus.  It reuses the
  level-select's start path (`sequencer/levelselect.cpp`).  Accepting a
  `.jjm` path lets synthetic test levels run without editing `JJ.GAM`.
- **End conditions:** level complete, **the first death** (a run is one
  level, one life), time limit, a tick cap, and **stuck** (no fact progress for
  N seconds).  The run then exits with a status.
- **Fixed dt and seed, unthrottled, no rendering.**  It should run far faster
  than real time.  Measure it first (section 6, P0).
- **Outputs**, per run:
  - `summary.json`: outcome, frames, gems collected and required, the death
    cause and frame if it died, replans, nodes expanded, and prediction-error
    stats.
  - `trace.jsonl.gz`: a `BotView` per tick, or every k ticks, plus the bot's
    keys.
  - `decisions.jsonl`: the plan and its reason at every (re)plan.
  - `bot.rec`: an ordinary recording.  Replaying it with `--no-headless`
    lets you *watch* any run.

### 4.2 The corpus and the suites

- **Shipped levels (88):** a static analyser (Python, from the `.jjm` and
  `JJ.GAM`) tags each level: `foes`, `throwers`, `falling-tiles`,
  `bombables`, `needs-bomb`, `switches`, `ice`, `teleporters`, gem count.
  Suites are tag queries ("all no-foe levels"), so a phase of the bot is judged
  only on what it claims to handle.
- **Skill scenarios:** tiny synthetic levels written as ASCII, compiled to
  `.jjm` (a writer added to `tools/jjm.py`).  Each isolates one skill:
  "8-gem TSP", "bomb the block to reach the gem", "lure the catcher onto T",
  "lure the foe away from the life", "ice run".  Each has a pass condition and
  a tick cap.
- **Planner unit tests:** the same ASCII maps loaded straight into a `BotView`,
  with no engine.  They test the route optimiser, GOAP and the tactical
  searches in milliseconds (ctest).

### 4.3 Running many

`python3 tools/bot/backtest.py run --suite shipped --config cfg.json [-j N] [--seeds 1,2,3]`

- Runs one process per (level, seed), in a process pool.  Headless needs no
  display, so it parallelises cleanly.
- Writes everything to `results/<date>-<sha>-<label>/`, with a `run.json`
  holding the git sha, dirty flag, config, config hash, dt, seeds and suite.
- Results are *not* committed.  A small `tests/bot/baseline.json` is: the
  blessed per-level summary, as `replaytest.py --bless` does it.

### 4.4 Comparing

`backtest.py compare <A> <B>` (or `compare --baseline`) prints a per-level
diff of outcome, failure cause and time, with the regressions first.  Across
seeds it reports the completion rate per level and flags changes smaller than
the seed-to-seed spread as noise.  It exits non-zero on any level that went
from complete to not complete.

The headline number is the **completion rate** per suite.  Under it come the
failures broken down by cause (death by foe, fall or blast; timeout; stuck;
no plan found), since that is what says what to work on next.  Time is
reported but not optimised.  It is kept as a diagnostic: a bot far slower than
the human recordings (`water01` completes at frame 3921) is usually wandering.

### 4.5 Robustness

A plan that only works on one exact frame is fragile.  The runner supports
perturbations to measure that:

- **Seeds** move the random pickups.
- **Start offset:** idle for k frames first, which shifts the foes' phase
  relative to ours.
- **Reaction latency:** the bot sees the `BotView` k ticks late.
- **Input jitter:** occasionally delay a key by one tick.

The **robustness score** is the completion rate under a perturbation sweep.
A lure that succeeds unperturbed but fails at offset +1 is reported as
fragile.

### 4.6 Model calibration

The model is a prediction, so the harness measures it like one.  On every
tick, the bot logs the model's predictions for t+Δ (Δ = 1, 5, 20 ticks): foe
cells, our own cell, and falling-tile states.  When t+Δ arrives, the harness
scores them against reality.

- `calibrate.py` reports the hit rate per predictor, per Δ and per level tag.
- **Motor calibration scenarios** measure turn time, step time, ice and slide
  behaviour, and stairs empirically, and check them against the model's edge
  costs.  The engine is the authority; the model's constants are fitted from
  it.

These numbers settle the L1-vs-L2 question with evidence: if foe prediction at
Δ=8 (the lure window) is below ~95%, we build `World::clone()`.

### 4.7 Determinism as a gate

The bot is a pure function of the views it receives and its config: no wall
clock, no unseeded randomness, and budgets in nodes, not milliseconds.  So
running the same (sha, config, level, seed) twice must produce byte-identical
traces.  `backtest.py check-determinism` does exactly that over a small suite,
and it is the first test that runs.

A side effect, and not a goal in itself: **a bot run is a replay test.**
Every run already writes a `.rec`.  Once the recording header carries the
start level (as it already carries `KAROO_SEED`), blessing a bot baseline
writes each completed level's `.rec` and expected end state into the replay
catalogue with no further work.  This gives the engine refactors regression
coverage across every level the bot can finish, without anyone playing them by
hand.  No effort beyond the header field is planned for it.

### 4.8 Seeing what it did

- **In game:** a bot overlay on the existing debug map (`debugui`).  It shows
  the current GOAP plan as text, the tactical path, the foes' predicted cells
  with their time, the falling-tile timers, and the region colouring.  It is
  drawn from the same structures the decision log serialises.
- **Offline:** `backtest.py report <run>` writes a static HTML page per level:
  the map, the path actually walked, the deaths marked, the decision timeline,
  and a link to the frame to replay from.
- **Bisecting a failure:** the planner can be re-run offline from any traced
  `BotView` (`botplan --view trace.jsonl.gz --tick 1234`).  This gives "what
  would it have planned here", without re-running the level.

## 5. Engine changes needed

Each is small and self-contained; they come first.

1. `KAROO_START_LEVEL`, including loading a bare `.jjm` path.
2. Run end conditions and an exit status for a headless bot run.  Add the
   **death cause** to the state-dump events (foe contact, fall, blast,
   timeout, harakiri).
3. Bot key-source wiring in `app` (`--bot`).
4. The start level in the recording header, and `replaytest.py` honouring it.
5. A `.jjm` writer in `tools/jjm.py`.  Its cell-byte notes also look stale
   against `tile.h` (height, kind, param, contents), so check them while we
   are there.
6. (Later, only if calibration demands it) `World::clone()`.

## 6. Phases

Each phase is a stack of PRs, each built on the previous one, and each ends
with a suite the bot is judged on.

| Phase | Builds | Judged on |
|---|---|---|
| **P0 Harness** | engine changes 1–4; `backtest.py run/compare/check-determinism`; a placeholder bot (a scripted key list) | the pipeline runs the 88 levels in parallel, deterministically; speed measured |
| **P1 Perception + motor** | `BotView` + JSON, the motor, a "walk to cell" bot; motor calibration scenarios | the scenarios: reaches the target cell; turn and step costs fitted |
| **P2 Navigation + collection** | nav graph, regions, distances, route optimiser | the foe-free, bomb-free shipped levels |
| **P3 Prediction** | foe/tile/bomb predictors, the prediction log, `calibrate.py`, space-time A* avoidance | prediction hit rates; completion on the foe levels by avoidance alone |
| **P4 GOAP** | facts, actions, the planner, risk-weighted plan choice; bombs, blocks, switches, bridges, time limits | the `needs-bomb` and `switches` suites |
| **P5 Lures** | `LureAway`, `LureToDrop` maneuvers with verification | the lure scenarios, robustness sweeps, guarded-gem levels |
| **P6 Tooling** | debug overlay, HTML report, `botplan` offline re-plan | — |

Every phase is measured by the same number: the completion rate over all 88
levels, which should never go down.

## 7. Decisions

1. **Success:** complete every level.  Score is ignored; time matters only
   against the level's time limit.
2. **Knowledge:** perfect.  The bot reads hidden contents.  A fair mode can
   come later through the perception filter.
3. **Lives:** a run is one level and one life, and stops at the first death.
   We believe every level can be completed without dying.  Collected gems
   persist through a death, so using lives would make some levels easier,
   but lives are a resource shared across levels.  Spending them would
   force planning across levels, which we are avoiding.  If a level turns
   out to need a death, we revisit this for that level.
4. **Replay tests:** bot runs become replay tests as a by-product (section
   4.7), with no dedicated effort.
5. **Language:** the planner is C++, in-process, and works on `BotView`.  The
   orchestration and analysis are Python.  `BotView` JSON still allows
   offline experiments against traces.
