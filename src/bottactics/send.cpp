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
#include <cmath>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include "bombmodel.h"
#include "danger.h"
#include "rules.h"
#include "throwers.h"
#include "walk.h"

namespace bot {

namespace {

/* What the planner takes a send to cost, past the walk to the pad. */
const double SEND_MS = 8000;
/* The cells to wait on: within NEAR cells of the pad, the nearest CELLS of
 * them, with these waits there. */
const int    NEAR = 3;
const size_t CELLS = 12;
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
        // Through: where a thrower out of the pad can walk and none now can
        // (a way down from the far side may lead back to this one).
        const std::vector<char> &far = board.throwerWalk(k), &now = board.throwerWalk(0);
        const int W = view.extentU;
        auto through = [&](Step c) {
            const size_t i = (size_t)c.v * W + c.u;
            return view.inside(c.u, c.v) && far[i] && !now[i];
        };
        const Cell &padCell = view.cell(pad.pad.u, pad.pad.v);

        // The cells about the pad, on its level (a step up or down), nearest
        // first.
        const Step here{view.player.u, view.player.v};
        Near cells;
        if (chebyshev(here, pad.pad) <= NEAR)
            cells.push_back({0, here});
        const Near near = nearCells(view, s.from, INFINITY, CELLS - cells.size(), [&](Step at) {
            const Cell &c = view.cell(at.u, at.v);
            return chebyshev(at, pad.pad) <= NEAR && c.kind != padCell.kind &&
                   abs(c.height - padCell.height) <= 1 && !switchedBy(view, at.u, at.v);
        });
        cells.insert(cells.end(), near.begin(), near.end());

        // Whether, the player keeping to plan, a thrower has gone through
        // and walked off the far end by its end.
        auto sent = [&](const std::vector<Timed> &plan, double untilMs) {
            follow(s.foes, view, plan, view.now + untilMs);
            for (size_t i = 0; i < s.foes.foeCount(); i++) {
                const Step c = s.foes.foeCell(i);
                if (s.foes.throwsAtPlayer(i) && through(c) && (c.u != pad.out.u || c.v != pad.out.v))
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
                Option after = o;
                Blast blast;
                waitTill(view, after, o.cost + pause);
                if (!walkOn(s, s.nav, after, at, bait))
                    return false;
                if (baitedBomb(s, after, BAIT_WAIT_MS, &blast))
                    return true;
            }
            return false;
        };

        for (const double wait : WAITS)
            for (const auto &[ms, at] : cells) {
                Option o;
                if (!walkTo(s, s.from, at, 0, o))
                    continue;
                waitTill(view, o, o.cost + wait);
                if (!sent(o.schedule, o.cost + SETTLE_MS) || !baited(o, at))
                    continue;
                o.lingerMs = SETTLE_MS;
                if (checkPlan(s.foes, view, o.schedule, o.lingerMs).hit)
                    continue;
                o.checked = true;
                o.stepMs  = o.cost;
                char buf[80];
                snprintf(buf, sizeof(buf), "send a thrower through (%d,%d), waiting at (%d,%d)",
                         pad.pad.u, pad.pad.v, at.u, at.v);
                o.what = buf;
                out.push_back(std::move(o));
                return;
            }

        // How near the pad the foe model has a thrower by DRAW_AHEAD_MS past
        // the plan's end: 0 through it or on it.
        auto nearPad = [&](const std::vector<Timed> &plan, double untilMs) {
            follow(s.foes, view, plan, view.now + untilMs);
            int best = -1;
            for (size_t i = 0; i < s.foes.foeCount(); i++) {
                if (!s.foes.throwsAtPlayer(i))
                    continue;
                const Step c = s.foes.foeCell(i);
                const int d = through(c) ? 0 : chebyshev(c, pad.pad);
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
        for (const auto &[ms, at] : cells) {
            Option o;
            if (!walkTo(s, s.from, at, 0, o))
                continue;
            waitTill(view, o, o.cost + DRAW_MS);
            o.lingerMs = DRAW_MS;
            if (checkPlan(s.foes, view, o.schedule, o.lingerMs).hit)
                continue;
            const int near = nearPad(o.schedule, o.cost + DRAW_AHEAD_MS);
            if (near < 0 || (ms <= 0 && near >= nowNear) || (bestNear >= 0 && near >= bestNear))
                continue;  // cells come nearest first: a tie keeps the nearer
            // Only a start on the send: its time is still the planner's guess.
            o.checked = true;
            char buf[80];
            snprintf(buf, sizeof(buf), "draw a thrower to (%d,%d), to (%d,%d)", pad.pad.u, pad.pad.v, at.u, at.v);
            o.what   = buf;
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
