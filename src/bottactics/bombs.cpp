/* Bombs(p): with none held and a foe or a block to use them on, the bombs
 * at pickup p, not taken yet: 3 of them, to be used before the exit (items
 * are only a means to an end).  Done as a walk to the planned pickup, or
 * another (costed with the whole plan still to do). */
#include "action.h"
#include "walk.h"

namespace bot {

namespace {

/* The bombs a pickup gives (player.cpp). */
const int PICKUP_BOMBS = 3;

class BombsAction : public Action {
public:
    const char *name() const override { return "bombs"; }

    void expand(const Facts &f, const Board &board, const PlanOptions &,
                std::vector<Successor> &out) const override
    {
        if (f.bombs != 0 || (f.foes == 0 && f.spent == board.everything()))
            return;
        const std::vector<Pickup> &pickups = board.pickups();
        for (size_t i = 0; i < pickups.size() && i < 32; i++) {
            if ((f.pickups & (1u << i)) || !Board::available(pickups[i], f.spent))
                continue;
            const double t = board.time(f.spent, f.at, pickups[i].cell);
            if (t < 0)
                continue;
            Successor n{f, {}};
            n.facts.pickups |= 1u << i;
            n.facts.bombs += PICKUP_BOMBS;
            n.facts.unused = true;
            n.facts.at = {pickups[i].cell.u, pickups[i].cell.v, 0};
            n.step.end   = n.facts.at;
            n.step.heads = pickups[i].cell;
            n.step.ms    = t;
            out.push_back(n);
        }
    }

    void offer(Situation &s, const Board &board, const Plan &plan,
               std::vector<Option> &out) const override
    {
        std::vector<Step> live;
        for (const Pickup &p : board.pickups())
            if (p.block < 0)
                live.push_back(p.cell);
        const Place &end = plan.steps.front().end;
        offerWalks(s, targets(s, plan, {end.u, end.v}, live), "bombs", true, out);
    }
};

}  // namespace

const Action &bombsAction()
{
    static const BombsAction action;
    return action;
}

}  // namespace bot
