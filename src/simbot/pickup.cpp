/* pickup: walk to a bomb pickup, or a glider, not yet taken.
 *   pre:  the pickup is not taken; the MoveGraph reaches it
 *   eff:  the pickup taken, three more bombs held (a glider: one more
 *         charge), the player on it
 *   do:   a RouteAttempt to the pickup. */
#include <stdio.h>
#include "actions.h"
#include "routeattempt.h"
#include "survey.h"

namespace simbot {

namespace {

/* What a pickup holds (player.cpp): bombs, or glider charges. */
const int PICKUP_BOMBS   = 3;
const int GLIDER_CHARGES = 1;

class Pickup : public Action {
public:
    explicit Pickup(bool glider) : glider_(glider) {}
    const char *name() const override { return glider_ ? "glider" : "pickup"; }

    void offer(const Facts &f, const Survey &survey, std::vector<Step> &out,
               int /*only*/) const override
    {
        if (f.done || f.at < 0)
            return;
        const std::vector<Cell> &cells = glider_ ? survey.gliders() : survey.pickups();
        const IndexSet &taken = glider_ ? f.gliders : f.pickups;
        for (size_t i = 0; i < cells.size(); i++) {
            if (taken.has(i))
                continue;
            const Cell c = cells[i];
            double ms;
            int used;
            const int to = survey.graph().arrive(f.at, c, &ms, f.glides, &used);
            if (to < 0)
                continue;
            Step s;
            s.action = this;
            s.arg    = (int)i;
            s.after  = f;
            s.after.at = to;
            s.after.glides -= used;
            if (glider_) {
                s.after.gliders.add(i);
                s.after.glides += GLIDER_CHARGES;
            } else {
                s.after.pickups.add(i);
                s.after.bombs += PICKUP_BOMBS;
            }
            s.ms = ms;
            char buf[48];
            snprintf(buf, sizeof(buf), glider_ ? "glider p%zu (%d,%d)" : "pickup b%zu (%d,%d)", i,
                     c.u, c.v);
            s.what = buf;
            out.push_back(std::move(s));
        }
    }

    std::unique_ptr<Attempt> attempt(const Step &step, const Survey &survey) const override
    {
        const std::vector<Cell> &cells = glider_ ? survey.gliders() : survey.pickups();
        return std::make_unique<RouteAttempt>(std::make_shared<Goal>(cells[step.arg]), survey);
    }

private:
    bool glider_;
};

}  // namespace

const Action &pickupAction()
{
    static const Pickup a(false);
    return a;
}

const Action &gliderAction()
{
    static const Pickup a(true);
    return a;
}

}  // namespace simbot
