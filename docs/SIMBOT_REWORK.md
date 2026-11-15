# simbot: the architecture rework

Two parts. Part 1 is a prompt to hand to a session that does the seam
fixes: no new behaviour, the code brought back in line with
`SIMBOT_DESIGN.md`. Part 2 is the design of the change that follows it:
an Attempt split into a *solve* and a generic *press*.

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
- Stop after item 4. The solve / press split (Part 2 of
  docs/SIMBOT_REWORK.md) is next and is not part of this; if something
  you are about to build would be thrown away by it, say so instead.

When you finish, report: the backtest counts before and after, by kind
of failure; the line counts of planner.cpp, routing.cpp and
routeattempt.cpp before and after; and anything in the design you found
did not hold.
```

## Part 2: solve and press

### The problem

An Attempt today does four jobs in one `act`: it searches for presses, it
presses them, it checks the game is where the search expected, and it
manages the time the search takes (the lead, doubled when outrun). Each
Attempt class repeats some of this, and the bot can only think about a
step once the last one is finished. So the player stands still while the
planner and the Route search, and stands still for good when there is no
plan. Most of the 17 deaths are of this kind: no presses under way, a foe
arriving.

Nothing needs the bot to wait. On the fixed clock a copy of the World
plays out exactly as the game will, so the World at the end of the
presses now being made is known before they are made. The next step can
be planned and searched from there, while the hands are still busy.

### The shape

```
            plans from factsOf(horizon)
  Planner ─────────────────────────────▶ Step
                                           │ Action::solve(step, horizon)
                                           ▼
                                         Solve ── searches copies ──▶ Solution
                                                                         │
     horizon = solution.end  ◀───────────────────────────────────────────┤
                                                                         ▼
  live World ──▶ Presser: a queue of Solutions, one press a tick ──▶ keys
```

**Solution.** What a search found: the presses, one for each tick; what
the game must look like before each (`Expect`, as now); and the World
after the last. It always ends with the player alive and at rest.

```cpp
struct Solution {
    std::vector<unsigned> presses;
    std::vector<Expect>   expects;
    World                 end;
};
```

**Solve.** A Step's search, and nothing else: given a World to start
from, it works a little each frame and ends with a Solution or with
Impossible (and, from Part 1, a Requirement if it knows what would make
it possible). It never sees the live game and never presses.

```cpp
class Solve {
public:
    enum Status { Searching, Found, Impossible };
    virtual Status run(Work &work) = 0;
    virtual const Solution &solution() const = 0;
    virtual std::string why() const { return ""; }
    virtual Requirement requirement() const { return {}; }
};
```

**Action.** Its doing half becomes
`solve(step, from, survey)` in place of `attempt(step, survey)`.

**Presser.** The one thing that presses, the same for every Action. It
holds a queue of Solutions. Each tick it checks the live World against
the next `Expect` and gives the press. The *horizon* is the World at the
end of everything queued: where the game will be when the queue runs
out.

### The loop

Each tick, within the frame's Work:

1. **Think**, while fewer than two Solutions are queued (the one being
   pressed, and one ahead). With no Solve under way, plan from
   `factsOf(horizon)` and start the Solve of the plan's first step from
   the horizon. Run it. Found: queue its Solution, which moves the
   horizon to its end. Impossible: learn its Requirement or rule the step
   out, and plan again, as now.
2. **Keep the hands busy.** If the queue is empty, queue an *idle*
   Solution from the horizon: at first a stand of `lead` ticks, checked
   on a copy that the player lives. A Solve under way started from the
   old horizon, which the game is now past: it starts again from the new
   one, with the lead doubled. This is today's lead, kept in one place.
3. **Press** the queue's next press. If the live World is not what was
   expected, something is not deterministic: log it, drop the queue and
   any Solve, and start again from the live World.

A step is done when its Solution has been pressed to the end. The next
plan is made from the facts of the World it ended in, not from the
Step's estimate of them.

### What it removes

- `Attempt`, and with it `RouteAttempt`'s lead, its retries (`MAX_ROUTES`:
  a Solution is exact, so it is not tried again) and its waiting for the
  player to come to rest (a Solution ends at rest, so the horizon is
  always at rest).
- The wrapper Attempts. Two helpers over Solves replace them: *then* (a
  second Solve started from the first one's end, their presses joined)
  and *else* (a second tried from the same World if the first is
  Impossible). The exit is a Route to its cell, then a stand till the
  level completes. A trap is a Route to the stand, then the trap's
  search. A kill is a Route near the foe, then the drop. A walk is a
  Route without waits, else one with.
- Standing while thinking, in the common case: the search for step N+1
  runs during the presses of step N.

`Route` is nearly a Solve already. It takes a World, runs on Work and
gives presses and Expects; it needs only to keep the World its presses
end in, which `found()` already plays through.

### Staying alive

The idle Solution is where "stay safe" goes. It starts as a stand. When
a stand would have the player die, it becomes a search of its own, an
ordinary Solve whose goal is "alive and at rest in N ticks", and the
Presser queues that instead. It is not an Action: it changes no fact, so
the planner never sees it. The same idle covers "no plan": the hands are
kept busy and alive while the planner tries again from the new horizon.

### In what order

Each a commit, the backtest run after each.

1. `Solution` and `Solve`; `Route` keeps its end World and is one.
2. An adapter: one Attempt that runs any Solve with today's lead and
   presses its Solution. Nothing behaves differently yet, and the
   Actions can move over one at a time.
3. *then* and *else*; collect, pickup and exit moved to `solve`.
4. Kill and trap moved. `RouteAttempt` and the wrapper Attempts go.
5. The Presser and the loop above, with one Solution queued at most
   (no looking ahead yet). The adapter and `Attempt` go. The same levels
   should complete.
6. Two queued: the next step is solved while this one is pressed. This
   is the first commit meant to change results: fewer deaths while
   thinking, shorter runs.
7. The idle that evades.

### Open questions

- **A search that outlasts the queue** is thrown away and started again
  further ahead. That is today's behaviour, and rare once searches run
  during the presses before them, but a trap's search is long (its lead
  is 60 ticks from the first). An Action could say what lead its Solve
  wants; left out until the scoreboard shows it matters.
- **How far ahead.** One step, to begin with. Further would have the bot
  commit to more of a plan before the facts are in, for little gain
  while one step's search fits in the presses of the last.
- **Whether an `Expect` is enough.** It compares the clock and the
  player. A foe gone astray with the player still in place is not seen
  until the player is. On the fixed clock neither should happen; if the
  log ever shows a mismatch, compare more.
