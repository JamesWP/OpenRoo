/* Glider: the nearest paraglider pickup not taken yet: one glide more
 * (items are only a means: a plan takes it to glide, and may take several
 * before setting off, where there is no way back).  Done as a
 * walk to the planned pickup, or another (costed with the whole plan still
 * to do). */
#include "action.h"
#include "walk.h"

namespace bot {

namespace {

class GliderAction : public Action {
public:
    const char *name() const override { return "glider"; }

    void expand(const Facts &f, const Board &board, const PlanOptions &,
                std::vector<Successor> &out) const override
    {
        // The nearest one only: which glider is taken first makes little
        // difference, and taking them in every order would swamp the search.
        const std::vector<Pickup> &pickups = board.pickups();
        int best = -1;
        double bestMs = -1;
        for (size_t i = 0; i < pickups.size() && i < 32; i++) {
            if (pickups[i].kind != Pickup::Glider || (f.pickups & (1u << i)) ||
                !Board::available(pickups[i], f.changes))
                continue;
            const double t = board.time(f.changes, f.at, pickups[i].cell);
            if (t >= 0 && (best < 0 || t < bestMs)) {
                best = (int)i;
                bestMs = t;
            }
        }
        if (best < 0)
            return;
        Successor n{f, {}};
        n.facts.pickups |= 1u << best;
        n.facts.glides++;
        n.facts.at = {pickups[best].cell.u, pickups[best].cell.v, 0};
        n.step.end   = n.facts.at;
        n.step.heads = pickups[best].cell;
        n.step.ms    = bestMs;
        out.push_back(n);
    }

    void offer(Situation &s, const Board &board, const Plan &plan,
               std::vector<Option> &out) const override
    {
        std::vector<Step> live;
        for (const Pickup &p : board.pickups())
            if (p.block < 0 && p.kind == Pickup::Glider)
                live.push_back(p.cell);
        const Place &end = plan.steps.front().end;
        offerWalks(s, targets(s, plan, {end.u, end.v}, live), "glider", true, out);
    }
};

}  // namespace

const Action &gliderAction()
{
    static const GliderAction action;
    return action;
}

}  // namespace bot
