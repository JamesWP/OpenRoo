# simbot: design

**Plan, then act.** The planner (GOAP, A*) searches over *Facts*: the
player's place (the start, gems, pickups, switches, sites, the exit),
gems taken, bombs and glides held, foes alive (blocks spent and bridges
extended are to come). An *Action* is both halves of one kind of step: what the planner sees
(preconditions, effects, an estimated cost) and its doing. A *Step* is one
planned (`collect g3`); an *Attempt* is a Step being done: searching copies
of the World for the presses that bring its effects about, then pressing
them. Plans are written `collect g3 (5,4) ~2.1s → blast b1 from
(6,7) ~3.0s → exit (9,9) ~1.5s`. Only the first step is done; the planner
plans again whenever the facts change.

**Sub-goals.** A plan is made for a list of sub-goals in turn, each
planned from where the last one's steps end. A sub-goal says how far the
facts are from it (0: met), and may give an estimate of the time still
to go and a tie-break between facts as good; if one can't be met within
its search, the plan goes as near as it can soonest. The bot's are
(`subgoals.cpp`): as few foes alive as can be, then the level done, its
estimate the rest of the quota (nearest gem first) and the exit. The
planner knows nothing of either: that a kill needs bombs, and a pickup
gives them, is in the Actions. The caller may also give the planner a
*polish*, a pass that reorders the steps a found plan begins with (the
bot's is the gem order, `collect.cpp`); the planner offers the steps
again in the new order and keeps it if it is quicker.

**Survey.** The one-off work at the start of a level, with a large budget.
The first part is the *MoveGraph*: every move played from every place the
player can rest on, on a copy of the World with the foes removed, giving
which places lead to which and how long each takes. Each move is also
played after waiting, but only till the times the game's rules say
something there changes (a lift beside it parks, glue lets go, a fallen
tile comes back), worked out from the rules, not played in steps. A lift
or platform also gives the times it passes each height or cell, since a
step off one moving is let through only then: riding, a wait till one is
how the player gets off part way, and a wait that carries it somewhere
new leaves that place to be played from in its own turn. Falling
tiles are put back at every place (`whatif::restoreFallings`), and an edge
notes the one-time tiles it breaks. On a level with foes and bombs to
be had, it also records where a bomb dropped at each place blows, dropped
standing or stepping back (it rolls, and falls off ledges).

Gliding is part of the MoveGraph too. A place in the air is a cell, facing
and height where the player glides with no step under way; in the air LEFT
and RIGHT turn and then step. Air places are worked out by the rules
(`simbot/gliding.h`), not played: each move's time and drop are played once
where a glide begins, an air place is kept at the highest the player comes
to it, and only a landing on a new ground place is played, to know where
it truly ends. The player is given glider charges at every place, and an
edge notes the charges it spends.

*Regions* are the MoveGraph's strongly connected components: a drop or a
glide leaves one for good. The level's estimate (`subgoals.cpp`) uses them: no way if no
chain of regions to the exit still holds enough gems, and a tour that
finishes the region it is in first. The planner's
costs come from it, and it guides the Attempts' searches (A*). Other precomputed structures go here too as
they are needed.

The Survey holds what is true of the level. What one Action works out
once for a level, for itself alone (the trap's stands), it keeps in a
*Working* of its own (`Survey::working`), which the Survey holds for it.

**What-ifs.** Hypothetical facts are made by editing a copy of the World
(the foes removed, the falling tiles put back, a bomb given), and the game is then
played on it. They are not game rules, so they are kept apart: in
`entities/whatif.{h,cpp}`, namespace `whatif`, each named `whatif::…`
(`whatif::removeFoes`, `whatif::giveBomb`). No game code calls them.

**Routes.** Most Actions search with a *Route* (`routing.h`): A* on the
clock over places, played on copies of the World, foes and all, guided by
the MoveGraph's time to a cell. The Route is the same for every Action.
What it searches for is a *Goal* the Action gives it: whether a node has
it, the estimate from a node, what a node's key adds, whether a bomb may
be dropped there, and which nodes are searched on. So a kill's or a
trap's rules are in its own file. A few rules hold for every Route: no
goal is reached with fewer gems held and left than the quota (a blast
may take some); a bomb dropped is got clear of and its blast waited out;
a short wait is tried only with a foe near; and a step from a place
where nothing near runs on the clock lands as it did before, so it is
played again only if it might come somewhere sooner.

**Requirements.** An Action that cannot find a way reports why, as a
*Requirement* (a condition on the facts, with a name for the log), at
once rather than after a long search. A drop can catch a foe from a
place whose blast covers it, or, if it is moving, one it can walk beside
before the fuse runs out. A kill whose every way to such a place passes a
place another foe g reaches first requires `dead(g)`, so the planner kills
g first (`dead` is the only one yet; `spent(b)` is to come with blocks).
A Requirement learned is kept for the level, and its step is offered
only while it holds. If nothing found makes it possible, the step is
marked impossible as things stand, until the facts change. The planner
plans around both.

**Real time.** Everything after the survey runs within each frame's spare
time. Searches (the planner, an Action's search) are objects that resume
and take a budget of work per frame. The player keeps doing the step under
way, or stands still, until a search finishes. Budgets are counted in work
done (nodes, ticks played), never wall time, so runs are deterministic.

**Tools.**
- **backtest** `--policy sim`, judged on levels completed and frames over
  budget. For each level not completed it says which kind of failure it
  was: no plan, died while thinking (nothing pressed), died while
  pressing, or out of time.
- **The sim log**, one JSON line per event: plans, Actions started,
  finished or blocked, what each learned, when a step starts and stops pressing, and the work
  each frame took.
- **Debug UI panel:** the plan, the step under way, its requirements, and the
  MoveGraph drawn over the level.
- **`KAROO_BOT_STOP`=frame:** stops a run at a frame. On the fixed clock
  the run plays out the same, so any decision can be reproduced and
  stepped through.
