# A player bot: design

A bot that plays Ka'roo levels, and the framework that measures it.  The
details live in the code's header comments; this is the shape and the
reasons for it.

## 1. Aims

- **Complete every level**: collect the gem quota and reach the exit, without
  dying.  Score does not count; time matters only against the time limit.
  The headline number is the completion rate over the 75 shipped levels.
- **One level, one life.**  A run stops at the first death, so the bot never
  plans across levels.
- **Perfect knowledge.**  The bot reads everything, hidden contents too.
- **Items are means, not ends**: taken only when a plan needs them.
- **Fast iteration**: a change is judged by running every level headless, in
  minutes, and comparing against the last run.

## 2. Ethos

- **Each layer answers one question.**  Complexity goes in the lowest layer
  that can carry it.  The layers above stay generic: the planner, the
  executive and the log never name an action.
- **Actions are the unit of extension.**  An action is one class in one file
  with both its halves: how the planner sees it (preconditions, effects,
  estimated cost) and how it is done now (concrete options).  Adding one
  touches nothing else.
- **Estimate to search, check to act.**  The planner searches on cheap
  estimates.  Only the step about to be acted on is checked against the
  model, and its real cost goes back to the planner.  A wrong estimate costs
  a little optimality, never safety.
- **Count on a prediction only within its known error.**  An outcome is
  relied on only if it survives the model's error, for example a kill that
  still happens with the blast a little early or late.  Robust outcomes are
  required; shapes that tend to be robust (such as "centred") are not rules.
- **The engine is the authority.**  The model reproduces the engine's
  rules, and its constants are measured from the engine, not guessed.
- **Deterministic.**  No wall clock, no unseeded randomness, budgets in
  nodes.  Two runs of the same build are byte-identical, so a refactor is
  proven by identical recordings.
- **Measured, not argued.**  Every change is judged on the whole corpus:
  completions, deaths by cause, and whether predictions came true.

## 3. Architecture

```
  Game ──▶ Perception ──BotView──▶ Executive ──script──▶ Motor ──▶ keys
                                     │   ▲
                         goals,  plan│   │option
                                     ▼   │
              Goal selection ──▶ Planner ◀──▶ Actions / tactics
                                     │            │
                                     ▼            ▼
                 Model: nav, routes, foe / tile / bomb prediction, danger
```

Every layer is plain C++ over a `BotView` value, with no engine calls.

### 3.1 Perception

`BotView`: a copyable snapshot of everything the bot may know, built once a
tick.  It serialises to JSON for traces and offline replays.

### 3.2 Model

What happens if the player does X: the nav graph (where a move ends and
what it costs, with tank controls, ice, slides, lifts and falls), the glide
model (where a paraglider can bring the player down), the route
optimiser (which k of the n gems, in what order, and against the clock
which time bonuses on the way), and the predictors for
foes, falling tiles and bombs.  The foe predictor steps the foes' own chase
search, since their routes are not stable.  The danger check runs a
player schedule against all of them.

### 3.3 Goal selection

Which goals are worth planning for now, the most relevant first: *finish*
always, and above it *remove foe* while bombs are held and a foe will meet
the player soon.  Goals are relevant on sensed facts, not on plans.

### 3.4 Planner

GOAP: A* over a small fact set (place, blocks spent and bridges switched,
gems needed and gathered, bombs, glides, foes, pickups) to a goal, by the
actions it is given.  Cost is time, plus an optional weight on risk.
A plan that would run out the clock is planned again against it: each step
must end in time, its times counted with a margin for their error, and the
routes may take the time bonuses that buy what they need.  If no plan is in
time even so, the first one stands.

### 3.5 Actions and tactics

Each action expands states for the planner and offers concrete options
for the step about to be done.  An option is a script, the player's
schedule, any blasts and a cost, and is checked by the danger check.
`choose` is the loop between planner and tactics: check the cheapest plan's
first step, rule it out or cost it in fact, and plan again until the
cheapest plan starts with a checked step.
A block can be spent without a bomb of the player's: a thrower that bombs
the player is baited to it, the player standing where its bomb would land,
and the foe model says when the bomb drops, so the walk away can be timed.

### 3.6 Motor

Turns a script of cells, turns, drops and waits into key presses.  It is
the only part that presses keys.

### 3.7 Executive

The agent loop.  It decides from rest, and re-selects goals every half
second while a script runs.  A higher goal breaks the script off, unless
the option under way is committed (its bomb still to go off).  When no
plan's first step is safe, it plans again with the routes going round the
throwers near the player, which the player cannot walk through: the
quickest way past one is usually safe, as it steps aside, but not always.  The rest of
the script is re-checked for safety, and the script is stopped if it turns
unsafe.  Every decision is logged with its reason.

### 3.8 In the code

One `src/` group per layer, held to this order by the layering check:
`botapi` (vocabulary), `botmodel`, `botmotor`, `botplan` (goals, planner),
`bottactics` (actions, tactics), `botexec`, and `bot` (the only one that
reads the game).  `tools/bot/` holds the Python orchestration and analysis.

## 4. Measuring

- **`backtest.py`** runs every level headless and in parallel, one process
  per level and seed, and writes per level a summary, the decision log, a
  trace and a recording that replays on screen.  `compare` diffs two runs,
  regressions first; `check-determinism` runs twice and requires identical
  results.
- **Checks of the model against the game**: `calibrate.py` (moves),
  `foecheck.py` and `modelcheck.py` (foe and nav predictions), `bombcheck.py`
  (blasts), `ambushcheck.py` (did the predicted kills happen).
- **A bot run is a replay test** as a by-product: a completed level's
  recording can join the replay catalogue as is.
