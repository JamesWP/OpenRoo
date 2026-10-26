/* Ambush: with a bomb and a foe, a foe less and a bomb less; a carrier's
 * gem (board.h) taken too, if gems are needed.  Its cost is a
 * guess: whether one can be had depends on where the foes go, which only the
 * tactical layer predicts.  Done as a turn, a drop and a walk away, taken
 * only where the foe model says the blast kills a foe (drops.h).  Not while
 * a follower can still be let out to the catchers (release.cpp). */
#include "action.h"
#include <algorithm>
#include "drops.h"

namespace bot {

namespace {

/* What an ambush takes: turning, the drop, the walk away and the fuse. */
const double AMBUSH_MS = 2500;
/* And picking up the gem a carrier leaves. */
const double FETCH_MS = 1000;

class AmbushAction : public Action {
    /* The foe (a carrier, if so) nearest the place, as the foes stand: where
     * to head for while no foe comes near enough (tactics.h, approach). */
    static Step nearest(const Facts &f, const Board &board, bool carrier)
    {
        Step best{-1, -1};
        double bestMs = -1;
        for (const FoeView &foe : board.live().foes) {
            if (foe.dying || (carrier && !Board::carrier(foe)))
                continue;
            const double t = board.time(f.changes, f.at, {foe.u, foe.v});
            if (t >= 0 && (bestMs < 0 || t < bestMs)) {
                best = {foe.u, foe.v};
                bestMs = t;
            }
        }
        return best;
    }

public:
    const char *name() const override { return "ambush"; }

    void expand(const Facts &f, double, const Board &board, const PlanOptions &,
                std::vector<Successor> &out) const override
    {
        // A follower to let out bombs the catchers for one bomb (release.cpp).
        if (f.bombs <= 0 || f.foes <= 0 || board.releasePending(f.changes, f.bombs))
            return;
        // With bombs short, the gems lying about first: the kills are had
        // after, the foes bunched up behind the player.
        if (bombsShort(board.live()) && f.gemsNeeded > f.carriers)
            return;
        Successor n{f, {}};
        n.facts.bombs--;
        n.facts.foes--;
        n.facts.unused = false;
        n.step.ms = AMBUSH_MS;
        n.step.heads = nearest(f, board, false);
        if (f.foes > f.carriers)
            out.push_back(n);
        n.step.heads = nearest(f, board, true);
        // A carrier killed: its gem is left where it dies, near the player.
        if (f.carriers > 0) {
            n.facts.carriers--;
            if (n.facts.gemsNeeded > 0) {
                n.facts.gemsNeeded--;
                n.step.ms += FETCH_MS;
            }
            out.push_back(n);
        }
    }

    void offer(Situation &s, const Board &board, const Plan &plan, std::vector<Option> &out) const override
    {
        const size_t first = out.size();
        offerAmbushes(s, out);
        // The plan's kill a carrier's: only one that kills a carrier will do
        // (a bomb spent on another may leave none for it).
        if (plan.steps.front().after.carriers >= board.carriers())
            return;
        const std::vector<FoeView> &foes = s.view.foes;
        out.erase(std::remove_if(out.begin() + first, out.end(),
                                 [&](const Option &o) {
                                     return std::none_of(o.killed.begin(), o.killed.end(),
                                                         [&](const FoeModel::Kill &k) {
                                                             return k.id < foes.size() &&
                                                                    Board::carrier(foes[k.id]);
                                                         });
                                 }),
                  out.end());
    }
};

}  // namespace

const Action &ambushAction()
{
    static const AmbushAction action;
    return action;
}

}  // namespace bot
