/* Send(k): the throwers sent through pad k (Board::sends), to an area they
 * could not walk to: the bait sites there are then theirs (Facts::throwerAt).
 * A foe's search knows no teleporters (foe.cpp, chase), so a thrower goes
 * through a pad only where its way to the player steps onto it, and comes
 * out the way it went on (foemodel.cpp, terrain); with the player gone
 * through first it has no way to it and stands.  So the player stays on the
 * thrower's side.  Its cost is a guess: only the foe model says when.
 *
 * Done a short step at a time, each checked (danger.h): a walk to a cell
 * near the pad and a wait there, taken where the foe model has a thrower go
 * through within the wait and walk off the far end; failing that, a walk to
 * the cell near the pad (or a wait where the player stands) that the foe
 * model has draw the thrower nearest the pad. */
#include "action.h"
#include <algorithm>
#include <string>
#include <stdlib.h>
#include "bombmodel.h"
#include "danger.h"
#include "moves.h"
#include "throwers.h"
#include "playout.h"
#include "walk.h"

namespace bot {

namespace {

/* What the planner takes a send to cost, past the walk to the pad. */
const double SEND_MS = 8000;
/* The cells to wait on: within NEAR cells of the pad, the nearest CELLS of
 * them, with these waits there. */
const int    NEAR = 3;
const size_t CELLS = 12;
/* No bound on the walk to them. */
const double ANY_MS = 1e9;
const double WAITS[] = {0, 1500, 3000, 5000, 8000};
/* How long after the wait a thrower sent through must still be off the pad
 * and on the far side: one that comes straight back is not sent. */
const double SETTLE_MS = 2500;
/* FoeView::kind of a thrower. */
const int THROWER = 3;
/* The four ways from a cell. */
const int SIDE_DU[4] = {0, 1, 0, -1}, SIDE_DV[4] = {-1, 0, 1, 0};
/* With a bait to follow (bait.cpp): the player sets off for it after one of
 * these pauses, and waits there this long at most for the bomb. */
const double FOLLOW_PAUSES[] = {0, 1500, 3000};
const double BAIT_WAIT_MS = 8000;
/* The wait on a cell drawing the thrower on, and how far past it the foe
 * model is run to see which cell draws it nearest the pad. */
const double DRAW_MS = 800;
const double DRAW_AHEAD_MS = 2000;

class SendAction : public Action {
public:
    const char *name() const override { return "send"; }

    void expand(const Facts &f, double, const Board &board, const PlanOptions &,
                std::vector<Successor> &out) const override
    {
        if (f.foes <= 0)
            return;
        for (const Send &p : board.sends(f.throwerAt)) {
            // The player waits about the pad, on the thrower's side.
            double best = -1;
            Step at{-1, -1};
            for (int d = 0; d < 4; d++) {
                const Step c{p.pad.u + SIDE_DU[d], p.pad.v + SIDE_DV[d]};
                const double t = board.time(f.changes, f.at, c);
                if (t >= 0 && (best < 0 || t < best)) {
                    best = t;
                    at = c;
                }
            }
            if (best < 0)
                continue;
            Successor n{f, {}};
            n.facts.throwerAt = p.at;
            n.facts.at        = {at.u, at.v, 0};
            n.step.end   = n.facts.at;
            n.step.heads = at;
            n.step.arg   = p.at;
            n.step.ms    = best + SEND_MS;
            out.push_back(n);
        }
    }

    void offer(Situation &s, const Board &board, const Plan &plan, std::vector<Option> &out) const override
    {
        const BotView &view = s.view;
        if (view.player.falling)
            return;
        const int k = plan.steps.front().arg;
        const std::vector<Send> sends = board.sends(0);
        const auto it = std::find_if(sends.begin(), sends.end(), [&](const Send &p) { return p.at == k; });
        if (it == sends.end())
            return;
        const Send pad = *it;
        const std::vector<char> far = board.throwerWalk(k);
        const int W = view.extentU;
        const Cell &padCell = view.cell(pad.pad.u, pad.pad.v);

        // The cells about the pad, on its level (a step up or down), nearest
        // first.
        const Step here{view.player.u, view.player.v};
        std::vector<Near> cells;
        if (chebyshev(here, pad.pad) <= NEAR)
            cells.push_back({0, here});
        for (const Near &n : nearCells(s, s.from, ANY_MS, CELLS, [&](Step c) {
                 const Cell &t = view.cell(c.u, c.v);
                 return chebyshev(c, pad.pad) <= NEAR && t.kind != padCell.kind &&
                        abs(t.height - padCell.height) <= 1;
             }))
            cells.push_back(n);
        if (cells.size() > CELLS)
            cells.resize(CELLS);

        // Whether, the player keeping to plan, a thrower has gone through
        // and walked off the far end by its end.
        auto sent = [&](const std::vector<Timed> &plan, double untilMs) {
            playOut(s.foes, view, plan, untilMs);
            for (size_t i = 0; i < s.foes.foeCount(); i++) {
                const Step c = s.foes.foeCell(i);
                if (s.foes.throwsAtPlayer(i) && view.inside(c.u, c.v) && far[(size_t)c.v * W + c.u] &&
                    (c.u != pad.out.u || c.v != pad.out.v))
                    return true;
            }
            return false;
        };
        // A bait to follow: the thrower must come out where the player,
        // following it, draws a bomb that spends a block, not straight back
        // through the pad after it.
        const PlanStep *next = plan.steps.size() > 1 && std::string(plan.steps[1].name) == "bait"
                                   ? &plan.steps[1]
                                   : nullptr;
        auto baited = [&](const Option &o, Step at) {
            if (!next)
                return true;
            const Step bait{next->end.u, next->end.v};
            for (const double pause : FOLLOW_PAUSES) {
                Option follow = o;
                Blast blast;
                if (pause > 0)
                    waitTill(view, follow, o.cost + pause, at);
                if (!walkOn(s, s.nav, follow, at, bait))
                    return false;
                if (baitedBomb(s, follow, BAIT_WAIT_MS, &blast))
                    return true;
            }
            return false;
        };

        for (const double wait : WAITS)
            for (const Near &n : cells) {
                const Step at = n.cell;
                Option o;
                if (!goTo(s, at, o))
                    continue;
                waitTill(view, o, o.cost + wait, at);
                if (!sent(o.schedule, o.cost + SETTLE_MS) || !baited(o, at) ||
                    !commit(s, o, o.cost, SETTLE_MS,
                            describe("send a thrower through (%d,%d), waiting at (%d,%d)", pad.pad.u,
                                     pad.pad.v, at.u, at.v)))
                    continue;
                out.push_back(std::move(o));
                return;
            }

        // How near the pad the foe model has a thrower by DRAW_AHEAD_MS past
        // the plan's end: 0 through it or on it.
        auto nearPad = [&](const std::vector<Timed> &plan, double untilMs) {
            playOut(s.foes, view, plan, untilMs);
            int best = -1;
            for (size_t i = 0; i < s.foes.foeCount(); i++) {
                if (!s.foes.throwsAtPlayer(i))
                    continue;
                const Step c = s.foes.foeCell(i);
                const bool through = view.inside(c.u, c.v) && far[(size_t)c.v * W + c.u];
                const int d = through ? 0 : chebyshev(c, pad.pad);
                if (best < 0 || d < best)
                    best = d;
            }
            return best;
        };

        // None yet: the cell about the pad (or where the player stands)
        // that draws a thrower nearest it, the nearest such.  A wait where
        // the player stands must bring one nearer than it is now: one that
        // has settled comes on only for a player that moves.
        int nowNear = -1;
        for (const FoeView &f : view.foes)
            if (f.kind == THROWER && !f.held && !f.dying) {
                const int d = chebyshev({f.u, f.v}, pad.pad);
                if (nowNear < 0 || d < nowNear)
                    nowNear = d;
            }
        Option best;
        int bestNear = -1;
        for (const Near &n : cells) {
            const Step at = n.cell;
            Option o;
            if (!goTo(s, at, o))
                continue;
            waitTill(view, o, o.cost + DRAW_MS, at);
            if (!commit(s, o, o.cost, DRAW_MS,
                        describe("draw a thrower to (%d,%d), to (%d,%d)", pad.pad.u, pad.pad.v, at.u, at.v)))
                continue;
            const int near = nearPad(o.schedule, o.cost + DRAW_AHEAD_MS);
            if (near < 0 || (n.ms <= 0 && near >= nowNear) || (bestNear >= 0 && near >= bestNear))
                continue;  // cells come nearest first: a tie keeps the nearer
            best     = std::move(o);
            bestNear = near;
        }
        if (bestNear >= 0)
            out.push_back(std::move(best));
    }
};

}  // namespace

const Action &sendAction()
{
    static const SendAction action;
    return action;
}

}  // namespace bot
