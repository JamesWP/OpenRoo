#include "fallback.h"

namespace bot {

namespace {

/* Fetch: with no way to finish, where the foes are to give the gems
 * (gemsFromSpawners), the nearest gem they have left. */
class FetchFallback : public Fallback {
public:
    const char *name() const override { return "fetch"; }
    void offer(Situation &s, const Board &board, const Plan &finish,
               std::vector<Option> &out) const override
    {
        if (!finish.found && gemsFromSpawners(s.view))
            offerFetchGem(s, board, out);
    }
};

/* Approach: with a way to finish, as near the way as is safe. */
class ApproachFallback : public Fallback {
public:
    const char *name() const override { return "approach"; }
    void offer(Situation &s, const Board &board, const Plan &finish,
               std::vector<Option> &out) const override
    {
        for (const PlanStep &step : finish.steps)
            if (step.heads.u >= 0) {
                offerApproach(s, board, step.heads, out);
                return;
            }
    }
};

/* Evade: a nearby cell to stand on; with no way to finish, only to keep out
 * of the foes' way, while what they do (a gem a carrier leaves, a bridge
 * switched) may open one. */
class EvadeFallback : public Fallback {
public:
    const char *name() const override { return "evade"; }
    void offer(Situation &s, const Board &board, const Plan &finish,
               std::vector<Option> &out) const override
    {
        if (finish.found || !s.view.foes.empty())
            offerEvade(s, board, out);
    }
};

}  // namespace

const std::vector<const Fallback *> &fallbacks()
{
    static const FetchFallback fetch;
    static const ApproachFallback approach;
    static const EvadeFallback evade;
    static const std::vector<const Fallback *> all{&fetch, &approach, &evade};
    return all;
}

}  // namespace bot
