# simbot: design

**Plan, then act.** The planner (GOAP, A*) searches over *Facts*: the
player's place (the start, gems, pickups, switches, sites, the exit),
gems taken, bombs and glides held, foes alive, blocks spent, bridges
extended. An *Action* is both halves of one step: what the planner sees
(preconditions, effects, an estimated cost) and its doing (searching
copies of the World for the presses that bring its effects about, then
pressing them). Plans are written `collect g3 (5,4) ~2.1s → blast b1 from
(6,7) ~3.0s → exit (9,9) ~1.5s`. Only the first step is done; the planner
plans again whenever the facts change.

**Survey.** The one-off work at the start of a level, with a large budget.
The first part is the *MoveGraph*: every move played from every place the
player can rest on, on a copy of the World with the foes removed, giving
which places lead to which and how long each takes. `reach` and the
planner's costs come from it. Other precomputed structures go here too as
they are needed.

**What-ifs.** Hypothetical facts are made by editing a copy of the World
(a foe removed, a block spent, a bridge switched), and the game is then
played on it. They are not game rules, so they are kept apart: in
`entities/whatif.{h,cpp}`, namespace `whatif`, each named `whatif::…`
(`whatif::removeFoe`, `whatif::spendBlock`). No game code calls them.

**Blockers.** An Action that cannot find a way reports why, as a fact:
the step was impossible but becomes possible on a what-if copy without
foe f, so `dead(f)` is added to that Action's preconditions; likewise
`spent(b)`. If nothing found makes it possible, the Action is marked
impossible as things stand. A learned condition holds until a fact it
depends on changes. The planner then plans around it.

**Real time.** Everything after the survey runs within each frame's spare
time. Searches (the planner, an Action's search) are objects that resume
and take a budget of work per frame. The player keeps doing the step under
way, or stands still, until a search finishes. Budgets are counted in work
done (nodes, ticks played), never wall time, so runs are deterministic.

**Tools.**
- **backtest** `--policy sim`, judged on levels completed and frames over
  budget.
- **The sim log**, one JSON line per event: plans, Actions started,
  finished or blocked, what each learned, and the work each frame took.
- **Debug UI panel:** the plan, the step under way, its blockers, and the
  MoveGraph drawn over the level.
- **`KAROO_BOT_STOP`=frame:** stops a run at a frame. On the fixed clock
  the run plays out the same, so any decision can be reproduced and
  stepped through.
