/* Drop(k): a foe dropped off bridge k's deck, a foe less: the player crosses
 * the deck to a switch of k on the far side with a foe chasing it onto the
 * deck, and works it; the deck is cleared from its far end back, and the foe
 * on it falls (foemodel.h).  Its cost is a guess: where the foe is when the
 * switch is worked only the foe model says.  The bridge is retracted after.
 * Where the bridge is in, it is set out first from another of its switches.
 * Done as a walk (onto that switch, if so, and) to the deck's edge and over
 * it to the switch, with or without a lap first onto k's other switch and
 * back, or off it and on and off again (to switch k again while it moves, so
 * that a foe working that switch behind the player sets the deck back out
 * rather than in), and a wait then and one at the edge (for the foe to come
 * near); taken only where the foe model has a foe fall and the player stays
 * safe throughout (danger.h), and not broken off once begun. */
#include <algorithm>
#include <stdio.h>
#include "action.h"
#include "danger.h"
#include "terrain.h"
#include "walk.h"

namespace bot {

namespace {

/* What the planner takes a drop to cost, on top of the walk. */
const double DROP_MS = 10000;
/* The waits offered where the player stands and at the deck's edge, ms. */
const double WAITS[] = {0, 600, 1500, 3000, 5000, 8000};
/* How long after the switch is worked a foe may still fall: the bridge's
 * move, and the fall's start. */
const double FALLS_WITHIN_MS = 500;

bool onDeck(const BridgeView &b, Step c)
{
    for (const Step &d : b.deck)
        if (d.u == c.u && d.v == c.v)
            return true;
    return false;
}

/* Bridge k's switch nearest the place by time (as times says), other than
 * not; {-1, -1} if none. */
template <typename Time>
Step nearSwitch(const Board &board, size_t k, Step not_, Time times)
{
    Step best{-1, -1};
    double bestMs = -1;
    for (const auto &[cell, b] : board.switches()) {
        if ((size_t)b != k || (cell.u == not_.u && cell.v == not_.v))
            continue;
        const double t = times(cell);
        if (t >= 0 && (bestMs < 0 || t < bestMs)) {
            best = cell;
            bestMs = t;
        }
    }
    return best;
}

class DropAction : public Action {
public:
    const char *name() const override { return "drop"; }

    void expand(const Facts &f, double, const Board &board, const PlanOptions &options,
                std::vector<Successor> &out) const override
    {
        // A carrier's gem would fall with it.
        if (f.foes <= f.carriers)
            return;
        const BotView &view = board.live();
        const int facing = f.at.facing ? f.at.facing : board.worlds().pointFacing();
        for (const auto &[cell, k] : board.switches()) {
            const BridgeView &b = view.bridges[k];
            if (b.moving)
                continue;
            // In, it is set out first from another of its switches.
            const Changes bit = board.worlds().bridgeBit(k);
            const Changes extended = board.extended(f.changes, k) ? f.changes : f.changes ^ bit;
            if (!board.worlds().has(extended) && board.worlds().size() > options.maxWorlds)
                continue;
            Step from{f.at.u, f.at.v};
            double first = 0;
            if (extended != f.changes) {
                from = nearSwitch(board, k, cell, [&](Step c) { return board.time(f.changes, f.at, c); });
                if (from.u < 0)
                    continue;
                first = board.time(f.changes, f.at, from) + b.moveMs();
            }
            // The way to the switch crosses the deck.
            const Paths &paths = board.worlds().get(extended).from(from.u, from.v, facing);
            const double t = paths.cost(cell.u, cell.v);
            if (t < 0)
                continue;
            bool crosses = false;
            for (const Step &c : paths.cellsTo(cell.u, cell.v))
                crosses = crosses || onDeck(b, c);
            if (!crosses)
                continue;
            Successor n{f, {}};
            n.facts.foes--;
            n.facts.changes = extended ^ bit;
            n.facts.at   = {cell.u, cell.v, 0};
            n.step.end   = n.facts.at;
            n.step.heads = from.u == f.at.u && from.v == f.at.v ? cell : from;
            n.step.arg   = k;
            n.step.ms    = first + t + DROP_MS + b.moveMs();
            out.push_back(n);
        }
    }

    void offer(Situation &s, const Board &board, const Plan &plan,
               std::vector<Option> &out) const override
    {
        const BotView &view = s.view;
        const PlayerView &p = view.player;
        const int arg = plan.steps.front().arg;
        if (p.falling || arg < 0 || (size_t)arg >= view.bridges.size())
            return;
        const size_t k = (size_t)arg;
        const BridgeView &b = view.bridges[k];
        if (b.moving && !b.extended)
            return;
        const Step here{p.u, p.v};
        const Step to{plan.steps.front().end.u, plan.steps.front().end.v};
        // k's other switch: set out from it first if the bridge is in, and
        // the lap goes to it.
        const Step near = nearSwitch(board, k, to, [&](Step c) { return s.from.cost(c.u, c.v); });
        if (!b.extended && near.u < 0)
            return;
        // The ground with the deck out, for the walks over it (one going out
        // is out, terrain.h; the danger check has it as it goes).
        const Terrain base(view);
        const std::unique_ptr<Terrain> switched = b.extended ? nullptr : base.changed({}, {k});
        const NavModel &nav = switched ? switched->nav() : base.nav();
        // Where the walk over it sets off from, and its edge: the last cell
        // before the deck on the way to the switch.
        const Step start = b.extended ? here : near;
        const Paths over(nav, start.u, start.v, 1, 0);
        const std::vector<Step> way = over.cellsTo(to.u, to.v);
        Step edge{-1, -1};
        for (size_t i = 1; i < way.size() && edge.u < 0; i++)
            if (onDeck(b, way[i]))
                edge = way[i - 1];
        if (edge.u < 0)
            return;
        // The lap: onto the other switch and back, or, standing on it, off
        // it towards the edge and back.
        Step lap = near;
        const bool onSwitch = lap.u == start.u && lap.v == start.v;
        if (onSwitch)
            lap = way.size() > 1 ? way[1] : Step{-1, -1};

        for (int laps = 0; laps <= (lap.u >= 0 ? 1 : 0); laps++)
            for (const double first : WAITS)
                for (const double atEdge : WAITS) {
                    Option o;
                    o.schedule.push_back({0, here});
                    o.cost = s.from.startMs();
                    if (!(start.u == here.u && start.v == here.v) && !walkOn(s, s.nav, o, here, start, 0))
                        continue;
                    // The lap; from the switch, it ends off it again.
                    Step at = start;
                    if (laps) {
                        if (!(walkOn(s, nav, o, start, lap, 0) && walkOn(s, nav, o, lap, start, 0)))
                            continue;
                        if (onSwitch && !walkOn(s, nav, o, start, lap, 0))
                            continue;
                        at = onSwitch ? lap : start;
                    }
                    if (first > 0) {
                        o.script.push_back({ScriptStep::Wait, 0, {}, view.now + o.cost + first});
                        o.cost += first;
                    }
                    if (!(at.u == edge.u && at.v == edge.v) && !walkOn(s, nav, o, at, edge, 0))
                        continue;
                    if (!walkOn(s, nav, o, edge, to, atEdge))
                        continue;
                    const double done = o.cost + b.moveMs() + FALLS_WITHIN_MS;
                    follow(s.foes, view, o.schedule, view.now + done);
                    if (s.foes.fell().empty())
                        continue;
                    o.script.push_back({ScriptStep::Wait, 0, {}, view.now + done});
                    o.lingerMs = done - o.schedule.back().t + 1000;
                    if (checkPlan(s.foes, view, o.schedule, o.lingerMs).hit)
                        continue;
                    // Not broken off: a walk stopped short on the deck, or
                    // before it, leaves the foe behind with the bridge.
                    o.checked = true;
                    o.committedUntil = view.now + done;
                    o.stepMs  = done;
                    o.cost    = plan.cost - plan.steps.front().ms + done;
                    char buf[112];
                    snprintf(buf, sizeof(buf), "drop a foe off bridge %d at (%d,%d)%s%s", b.slot, to.u,
                             to.v, b.extended ? "" : ", set out first", laps ? ", a lap first" : "");
                    o.what = buf;
                    out.push_back(std::move(o));
                    return;
                }
    }
};

}  // namespace

const Action &dropAction()
{
    static const DropAction action;
    return action;
}

}  // namespace bot
