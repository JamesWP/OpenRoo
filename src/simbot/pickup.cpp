/* pickup: walk to a bomb pickup not yet taken.
 *   pre:  the pickup is not taken; the MoveGraph reaches it
 *   eff:  the pickup taken, three more bombs held, the player on it
 *   do:   a RouteAttempt to the pickup. */
#include <stdio.h>
#include "actions.h"
#include "routeattempt.h"
#include "survey.h"

namespace simbot {

namespace {

/* The bombs a pickup holds (player.cpp). */
const int PICKUP_BOMBS = 3;

class Pickup : public Action {
public:
    const char *name() const override { return "pickup"; }

    void offer(const Facts &f, const Survey &survey, std::vector<Step> &out) const override
    {
        if (f.done || f.at < 0)
            return;
        for (size_t i = 0; i < survey.pickups().size(); i++) {
            if (f.pickups.has(i))
                continue;
            const Cell c = survey.pickups()[i];
            double ms;
            const int to = survey.graph().arrive(f.at, c, &ms);
            if (to < 0)
                continue;
            Step s;
            s.action = this;
            s.arg    = (int)i;
            s.after  = f;
            s.after.at = to;
            s.after.pickups.add(i);
            s.after.bombs += PICKUP_BOMBS;
            s.ms = ms * (1 + foeRisk(f, survey));
            char buf[48];
            snprintf(buf, sizeof(buf), "pickup b%zu (%d,%d)", i, c.u, c.v);
            s.what = buf;
            out.push_back(std::move(s));
        }
    }

    std::unique_ptr<Attempt> attempt(const Step &step, const Survey &survey) const override
    {
        return std::make_unique<RouteAttempt>(Goal{survey.pickups()[step.arg]}, survey.graph());
    }
};

}  // namespace

const Action &pickupAction()
{
    static const Pickup a;
    return a;
}

}  // namespace simbot
