# simbot: the architecture rework

Two parts. Part 1 is a prompt to hand to a session that does the seam
fixes: no new behaviour, the code brought back in line with
`SIMBOT_DESIGN.md`. Part 2 is the design of the change that follows it:
an Attempt split into an `ActionSearch`, the `ActionScript` it finds, and
one `ScriptQueue` that plays them.

Where this comes from: a review of `src/simbot` at `577bab8`. The latest
full backtest then (`635015b`, policy `sim`) completed 41 of 80 levels,
with 20 stuck, 17 deaths and 2 timeouts.

## Part 1: the prompt for the seam fixes

```text
You are working on simbot, the new player bot in src/simbot, on the local
branch `botplan`. Read docs/SIMBOT_DESIGN.md and docs/SIMBOT_REWORK.md
first, then all of src/simbot (about 3,900 lines).

The design says the planner is generic and knows nothing of the game: it
drives small Action modules, and adding an Action is one file and a line
in actions(). The code has drifted from that. Your job is to bring it
back, with no change in what the bot does. This is groundwork: the
mechanics still to come (bridges, switches, blocks, the clock, throwers)
should each be an Action file and a fact, and today each would mean
editing the planner and the router.

The leaks to fix, each its own commit, in this order:

0. A scoreboard. Have tools/bot/backtest.py say, for each level that does
   not complete, which kind of failure it was: no plan found, died while
   thinking (no presses under way), died while pressing, or out of time.
   The sim log (simbot.jsonl) has what is needed. Print the counts with
   the results, so every later commit shows which kind moved.

1. The planner names the game. In planner.cpp, estimate() knows gems, the
   quota, regions, the exit and gliders; polish() names collectAction and
   exitAction; the open list breaks ties on gems held; factsOf reads tile
   contents. Move each to where it belongs: the estimate and the tie-break
   come with the SubGoal, the polish is a pass the caller supplies (its
   code beside collect), factsOf has its own file. Done when planner.h and
   planner.cpp include none of survey.h, world.h or tile.h, name no
   Action, and read no field of Facts but through a SubGoal. Facts itself
   stays a plain struct: it is the shared vocabulary, and that is fine.

2. Requirements are foes only. The design has an Action that cannot find
   a way report why "as a fact". The code has Attempt::requiresDead(), a
   foe bitmask in the planner (Requires), and SimBot::learn mapping foe
   ids to bits. Make a Requirement a condition on Facts with a name for
   the log; the Attempt gives one, the planner leaves the step out while
   it does not hold, and neither the planner nor SimBot knows what it is
   about.

3. Kill and trap live in the shared router. Goal (routing.h) is a struct
   of bools, routing.cpp branches on kill / trap / track in ten places,
   and RouteAttempt holds the kill's blockedBy, its approach phase and
   the trap's lead. Turn the goal into an interface the Route searches
   for (whether a node has reached it, the estimate from a place, what a
   node's key adds, which moves it allows, what it prunes) and move the
   kill's and the trap's halves into kill.cpp and trap.cpp. Done when
   routing.cpp and routeattempt.cpp name neither.

4. Survey holds one Action's working. Survey::stands is the trap's alone.
   Say how an Action should keep what it works out once for a level, and
   move it.

How to work:

- I own the design. Before each of 1 to 4, show me the interface you
  mean to end up with (the header, a dozen lines) and wait for a yes.
  Fixes within the agreed shape need no asking.
- All commits go on `botplan`, one per item, small enough to review one
  by one. Do not push or open a PR unless I ask.
- Before you start, run the whole backtest at the branch head for the
  baseline: `python3 tools/bot/backtest.py run --policy sim --label base`.
  After each commit run it again and `compare` with the baseline. These
  are refactors, so the same levels should complete; recordings need not
  be byte-identical. If a level changes outcome, find out why before
  going on, and tell me.
- Build and test on Linux only (README.md). While iterating, run a few
  levels (`--levels 0-9`), the whole set once per commit.
- Keep it simple. If an interface needs more than a handful of methods,
  or a fix adds more code than it removes, stop and tell me: the seam is
  probably in the wrong place.
- Leave docs/SIMBOT_DESIGN.md as it is. At the end, list the edits to it
  you would suggest.
- Stop after item 4. The ActionSearch / ActionScript split (Part 2
  of docs/SIMBOT_REWORK.md) is next and is not part of this; if something
  you are about to build would be thrown away by it, say so instead.

When you finish, report: the backtest counts before and after, by kind
of failure; the line counts of planner.cpp, routing.cpp and
routeattempt.cpp before and after; and anything in the design you found
did not hold.
```

## Part 2: ActionSearch, ActionScript and ScriptQueue

### The problem

An Attempt today does four jobs in one `act`: it searches for presses, it
presses them, it checks the game is where the search expected, and it
manages the time the search takes (the lead, doubled when outrun). Each
Attempt class repeats some of this, and the bot can only search for a
step once the last one is finished. So the player stands still while the
planner and the Route search, and stands still for good when there is no
plan. Most of the 17 deaths are of this kind: no presses under way, a foe
arriving.

Nothing needs the bot to wait. On the fixed clock a copy of the World
plays out exactly as the game will, so the World at the end of the
presses now being made is known before they are made. The next step can
be planned and searched from that World while the current presses are
still being played.

### The names

| Name | What it is | Replaces |
|---|---|---|
| `ActionScript` | The presses that do one Step, tick by tick, and the World they end in | the presses and expects inside `Route` / `RouteAttempt` |
| `ActionSearch` | The search for a Step's `ActionScript`, from a given World | the searching half of `Attempt` |
| `ScriptQueue` | The `ActionScript`s waiting to be played, and the one thing that plays them | the pressing half of `Attempt` |
| `ScriptQueue::endWorld()` | The World after everything queued has been played | the lead |
| `waitScript` | An `ActionScript` that presses nothing for some ticks | standing while searching |
| `SequenceSearch` | Two searches, the second from the World the first one's script ends in | `ExitAttempt`, `TrapAttempt`, the kill's approach |
| `FallbackSearch` | Two searches from the same World, the second tried if the first is impossible | "without waits, then with" |

`presses` stays the name of the field: it is the game's own word for a
tick's input (`presses.h`, `Policy::act`).

### The shape

```
  Planner ── plans from factsOf(queue.endWorld()) ──▶ Step
                                                        │ Action::search(step, queue.endWorld())
                                                        ▼
                                                  ActionSearch ── searches copies ──▶ ActionScript
                                                                                          │ queued
                                                                                          ▼
  live World ──▶ ScriptQueue: one press a tick, checked against the script ──▶ keys
```

**ActionScript.** What a search found: the presses, one for each tick;
what the game must look like before each (`Expect`, as now); and the
World after the last. It always ends with the player alive and at rest.

```cpp
struct ActionScript {
    std::vector<unsigned> presses;
    std::vector<Expect>   expects;
    World                 end;
};
```

**ActionSearch.** A Step's search, and nothing else: given a World to
start from, it works a little each frame and ends with an ActionScript or
with Impossible (and, from Part 1, a Requirement if it knows what would
make it possible). It never sees the live game and never presses.

```cpp
class ActionSearch {
public:
    enum Status { Searching, Found, Impossible };
    virtual Status run(Work &work) = 0;
    virtual const ActionScript &script() const = 0;
    virtual std::string why() const { return ""; }
    virtual Requirement requirement() const { return {}; }
};
```

**Action.** Its doing half becomes `search(step, from, survey)` in place
of `attempt(step, survey)`.

**ScriptQueue.** The same for every Action, and the only code that
presses. It holds the ActionScripts still to be played. Each tick it
checks the live World against the next `Expect` and gives the press.
`endWorld()` is the World at the end of everything queued: where the game
will be when the queue runs out.

### The loop

Each tick, within the frame's Work:

1. **Search**, while fewer than two ActionScripts are queued (the one
   being played, and the next). With no ActionSearch under way, plan from
   `factsOf(queue.endWorld())` and start the search of the plan's first
   step from that World. Run it. Found: queue its script, which moves
   `endWorld()` to the script's end. Impossible: learn its Requirement or
   rule the step out, and plan again, as now.
2. **Refill.** If the queue is empty, queue a `waitScript` of `lead`
   ticks from `endWorld()`, checked on a copy that the player lives. An
   ActionSearch under way started from the old end World, which the game
   is now past: it starts again from the new one, with the lead doubled.
   This is today's lead, kept in one place.
3. **Play** the queue's next press. If the live World is not what the
   script expected, something is not deterministic: log it, drop the
   queue and any search, and start again from the live World.

A step is done when its ActionScript has been played to the end. The
next plan is made from the facts of the World it ended in, not from the
Step's estimate of them.

### What it removes

- `Attempt`, and with it `RouteAttempt`'s lead, its retries (`MAX_ROUTES`:
  an ActionScript is exact, so it is not searched for again) and its
  waiting for the player to come to rest (a script ends at rest, so
  `endWorld()` is always at rest).
- The wrapper Attempts, replaced by `SequenceSearch` and
  `FallbackSearch`. The exit is a Route to its cell in sequence with a
  wait till the level completes. A trap is a Route to the stand in
  sequence with the trap's search. A kill is a Route near the foe in
  sequence with the drop. A walk is a Route without waits, falling back
  to one with.
- Standing while searching, in the common case: the search for step N+1
  runs while step N's script is played.

`Route` is nearly an ActionSearch already. It takes a World, runs on Work
and gives presses and Expects; it needs only to keep the World its
presses end in, which `found()` already plays through.

### Staying alive

The `waitScript` is where staying alive goes. When waiting would have
the player die, the refill instead runs a `SurviveSearch`: an ordinary
ActionSearch whose goal is "alive and at rest in N ticks". It is not an
Action: it changes no fact, so the planner never sees it. The same refill
covers "no plan": scripts keep being played and the player kept alive
while the planner tries again from the new end World.

### In what order

Each a commit, the backtest run after each.

1. `ActionScript` and `ActionSearch`; `Route` keeps its end World and is
   an ActionSearch.
2. `SearchAttempt`, an adapter: one Attempt that runs any ActionSearch
   with today's lead and plays its script. Nothing behaves differently
   yet, and the Actions can move over one at a time.
3. `SequenceSearch` and `FallbackSearch`; collect, pickup and exit moved
   to `search`.
4. Kill and trap moved. `RouteAttempt` and the wrapper Attempts go.
5. `ScriptQueue` and the loop above, holding one script at most (the
   next step is not yet searched early). `SearchAttempt` and `Attempt`
   go. The same levels should complete.
6. Two scripts queued: the next step is searched while this one is
   played. This is the first commit meant to change results: fewer
   deaths with no presses under way, shorter runs.
7. `SurviveSearch`.

### Open questions

- **A search that outlasts the queue** is thrown away and started again
  from a later World. That is today's behaviour, and rare once searches
  run while the script before them is played, but a trap's search is
  long (its lead is 60 ticks from the first). An Action could say what
  lead its search wants; left out until the scoreboard shows it matters.
- **How many scripts to queue.** Two, to begin with. More would have the
  bot commit to more of a plan before the facts are in, for little gain
  while one step's search fits in the playing of the last.
- **Whether an `Expect` is enough.** It compares the clock and the
  player. A foe gone astray with the player still in place is not seen
  until the player is. On the fixed clock neither should happen; if the
  log ever shows a mismatch, compare more.
