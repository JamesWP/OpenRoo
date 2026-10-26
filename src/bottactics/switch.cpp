/* Switch(k): a bridge switched, by coming to rest on a switch of its slot,
 * and waited for until its move is done (terrain.h): extended, where that
 * leads somewhere new from the switch, or the switch itself does (passed over
 * on the way somewhere new), or another switch it leads to does; or retracted, if it is not in the way or the
 * switch leads somewhere new (to pass over a switch).  Not the switch the player stands on (it
 * works only when stepped onto).  A bridge on the move ignores its switch
 * till the move is done, and then the switch works it (foemodel.h).  A bridge that lets catchers in to the player costs
 * more than its time.  Done as a walk to the planned switch and a wait there:
 * till the bridge is done, or, where the plan's next step does not cross its
 * deck and a catcher is after the player, not at all (one passed over on the
 * way). */
#include <algorithm>
#include <vector>
#include <stdio.h>
#include "action.h"
#include "moves.h"
#include "playout.h"
#include "walk.h"

namespace bot {

namespace {

/* The waits a walk to a switch is also offered after, ms. */
const double WAITS[] = {0, 600, 1500, 3000, 5000, 8000};
/* The wait past the move's end, for the last deck cell to be stamped. */
const double SETTLE_MS = 100;
/* What letting a catcher in to the player costs: more than a way round a
 * switch, less than a bridge the level needs. */
const double LET_IN_MS = 5000;
/* The cells near a switch a wait may be taken on, within NEAR_MS of it. */
const double NEAR_MS    = 1500;
const size_t NEAR_CELLS = 6;

/* FoeView::kind of a catcher. */
const int CATCHER = 2;

/* How long the bridge's move under way has still to go, ms from now. */
double busyMs(const BotView &view, const BridgeView &b)
{
    return b.moving ? std::max(0.0, b.phaseStart + b.moveMs() - view.now) : 0;
}

/* Whether a switch other than cell, that cell can reach in world next,
 * reaches more in next than in s. */
bool beyond(const Board &board, Changes s, Changes next, Step cell)
{
    for (const auto &[other, k] : board.switches())
        if ((other.u != cell.u || other.v != cell.v) &&
            board.time(next, {cell.u, cell.v, 0}, other) >= 0 &&
            board.reach(next, other) > board.reach(s, other))
            return true;
    return false;
}

/* Whether, the player keeping to plan and then staying, the foe model has a
 * foe work bridge k within untilMs: one going after the switches it is
 * raised, to take it back in before the player is across. */
bool workedBack(Situation &s, const std::vector<Timed> &plan, double untilMs, size_t k)
{
    const BotView &view = s.view;
    if (view.foes.empty())
        return false;
    playOut(s.foes, view, plan, untilMs);
    for (const FoeModel::Switched &w : s.foes.switched())
        if (w.bridge == k && w.byFoe)
            return true;
    return false;
}

/* Whether the walk from the switch cell to the plan's next step crosses
 * bridge k's deck, the bridge switched: no next step does not. */
bool nextCrosses(const Board &board, const Plan &plan, Step cell, size_t k)
{
    if (plan.steps.size() < 2)
        return true;
    const Place &next = plan.steps[1].end;
    const Paths &paths = board.worlds().get(board.worlds().bridgeBit(k)).from(cell.u, cell.v,
                                                                             board.worlds().pointFacing());
    for (const Step &c : paths.cellsTo(next.u, next.v))
        for (const Step &d : board.live().bridges[k].deck)
            if (c.u == d.u && c.v == d.v)
                return true;
    return false;
}

/* A walk to the switch cell by way of at, with the wait taken there (where
 * the lifts keep a wait from being taken before setting off, walkTo; or
 * where a foe is to be drawn off first). */
bool waitAt(const Situation &s, Step at, Step cell, double wait, Option &o)
{
    o = Option();
    if (at.u == s.view.player.u && at.v == s.view.player.v) {
        o.schedule.push_back({0, at});
        o.cost = s.from.startMs();
    } else if (!walkTo(s, s.from, at, 0, o)) {
        return false;
    }
    o.cost += wait;
    o.script.push_back({ScriptStep::Wait, 0, {}, s.view.now + o.cost});
    return walkOn(s, s.nav, o, at, cell);
}

/* The cells to wait on for a switch: the one before it on the way, then
 * the nearest others within NEAR_MS of it, the nearest first. */
std::vector<Step> waitCells(const Situation &s, Step cell)
{
    std::vector<Step> cells;
    const std::vector<Timed> route = s.from.scheduleTo(cell.u, cell.v);
    if (route.size() >= 2)
        cells.push_back(route[route.size() - 2].cell);
    const Paths back(s.nav, cell.u, cell.v, 1, 0);
    for (const Near &n : nearCells(s, back, NEAR_MS, NEAR_CELLS, [&](Step c) {
             return s.from.cost(c.u, c.v) >= 0 && (cells.empty() || c.u != cells[0].u || c.v != cells[0].v);
         }))
        if (cells.size() < NEAR_CELLS)
            cells.push_back(n.cell);
    return cells;
}

class SwitchAction : public Action {
public:
    const char *name() const override { return "switch"; }

    void expand(const Facts &f, double, const Board &board, const PlanOptions &,
                std::vector<Successor> &out) const override
    {
        const Changes wanted = board.inTheWay(f.changes, f.at, false);
        for (const auto &[cell, k] : board.switches()) {
            const BridgeView &b = board.live().bridges[k];
            if (cell.u == f.at.u && cell.v == f.at.v)
                continue;
            const Changes bit = board.worlds().bridgeBit(k);
            const Changes next = f.changes ^ bit;
            const double t = board.time(f.changes, f.at, cell);
            if (t < 0)
                continue;
            // Retracting the way, but for coming to a switch that leads
            // somewhere new (one passed over).
            if (board.extended(f.changes, k) && (wanted & bit) != 0 &&
                !board.opens(next, cell, f.changes, f.at))
                continue;
            // Extending a bridge is worth it only if it leads somewhere new,
            // or the switch does, passed over (one of a row of switches), or
            // another switch does (the way it opens is beyond that one).
            if (!board.extended(f.changes, k) && board.reach(next, cell) <= board.reach(f.changes, cell) &&
                !board.opens(next, cell, f.changes, f.at) && !beyond(board, f.changes, next, cell))
                continue;
            Successor n{f, {}};
            n.facts.changes = next;
            n.facts.at   = {cell.u, cell.v, 0};
            n.step.end   = n.facts.at;
            n.step.heads = cell;
            n.step.ms    = std::max(t, busyMs(board.live(), b)) + b.moveMs() + SETTLE_MS;
            n.step.penalty = LET_IN_MS * board.letsIn(next, cell, f.changes, f.at);
            out.push_back(n);
        }
    }

    void offer(Situation &s, const Board &board, const Plan &plan,
               std::vector<Option> &out) const override
    {
        const Place &end = plan.steps.front().end;
        const Step cell{end.u, end.v};
        const int k = board.switched(cell);
        if (k < 0)
            return;
        const BridgeView &b = board.live().bridges[k];
        const double moveMs = b.moveMs() + SETTLE_MS;
        // On the switch till the bridge is done (a foe kept off it the while),
        // or, where the way on does not need it and a catcher is after the
        // player, only till a move under way is: coming onto it works it.
        const bool needed = nextCrosses(board, plan, cell, (size_t)k);
        const bool chased = std::any_of(s.view.foes.begin(), s.view.foes.end(), [](const FoeView &f) {
            return f.kind == CATCHER && !f.dying && !f.held;
        });
        const double busy = busyMs(s.view, b);
        // Going out, and needed, how long the bridge must then stay out: for
        // the walk from the switch to its far end.
        double holdMs = -1;
        if (!b.extended && needed)
            for (const Step &c : {b.deck.front(), b.deck.back()})
                holdMs = std::max(holdMs, board.time(board.worlds().bridgeBit(k), {cell.u, cell.v, 0}, c));
        // Now, or after a wait: a foe working the switches may have the
        // bridges on the way as they should be only for a while.
        std::vector<Step> at{{-1, -1}};
        const std::vector<Step> cells = waitCells(s, cell);
        at.insert(at.end(), cells.begin(), cells.end());
        for (size_t i = 0; i < at.size(); i++) {
            for (const double wait : WAITS) {
                Option o;
                // Walked straight to, or by way of a cell to wait on, when going
                // out wants a foe drawn off (or the lifts keep a wait from being
                // taken before setting off).
                if (at[i].u < 0 ? !walkTo(s, s.from, cell, wait, o)
                                : (holdMs <= 0 && i > 1) || !waitAt(s, at[i], cell, wait, o))
                    continue;
                // A move under way is done first: the switch waits for it,
                // and the player with it, as the next switch come onto would
                // take the place of this one (movableentity.h, switchSlot_).
                const double arrive = o.cost;
                const double stay = needed || !chased ? std::max(arrive, busy) + moveMs - arrive
                                                      : (busy > arrive ? busy - arrive + SETTLE_MS : 0);
                o.script.push_back({ScriptStep::Wait, 0, {}, s.view.now + arrive + stay});
                o.schedule.push_back({arrive + stay, cell});
                o.stepMs = arrive + stay;
                if (holdMs > 0 && workedBack(s, o.schedule, arrive + moveMs + holdMs, (size_t)k))
                    continue;
                o.cost = plan.cost - plan.steps.front().ms + o.stepMs;
                char buf[96];
                snprintf(buf, sizeof(buf), "switch (%d,%d) for bridge %d", cell.u, cell.v,
                         board.live().bridges[k].slot);
                o.what = buf;
                out.push_back(std::move(o));
            }
        }
        if (holdMs <= 0 || !out.empty())
            return;
        // A decoy: another switch worked first, whose own switches, raised,
        // draw a foe off (FoeModel::nearestSwitch), and the walk on from it.
        for (const auto &[decoy, dk] : board.switches()) {
            const BridgeView &db = board.live().bridges[dk];
            if (dk == k || db.moving || (decoy.u == cell.u && decoy.v == cell.v) ||
                s.from.cost(decoy.u, decoy.v) < 0)
                continue;
            Option first;
            if (!walkTo(s, s.from, decoy, 0, first))
                continue;
            for (const double wait : WAITS) {
                Option o = first;
                const double settled = o.cost + db.moveMs() + SETTLE_MS + wait;
                waitTill(s.view, o, settled, decoy);
                if (!walkOn(s, s.nav, o, decoy, cell))
                    break;
                const double arrive = o.cost;
                waitTill(s.view, o, arrive + moveMs, cell);
                o.stepMs = arrive + moveMs;
                if (workedBack(s, o.schedule, arrive + moveMs + holdMs, (size_t)k))
                    continue;
                o.cost = plan.cost - plan.steps.front().ms + o.stepMs;
                char buf[128];
                snprintf(buf, sizeof(buf), "switch (%d,%d) for bridge %d, after (%d,%d) to draw a foe off",
                         cell.u, cell.v, b.slot, decoy.u, decoy.v);
                o.what = buf;
                out.push_back(std::move(o));
                return;
            }
        }
    }
};

}  // namespace

const Action &switchAction()
{
    static const SwitchAction action;
    return action;
}

}  // namespace bot
