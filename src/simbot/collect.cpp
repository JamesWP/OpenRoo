/* collect: walk to a gem not yet taken, while the quota is short.
 *   pre:  the gem is not taken; gems held < quota; the MoveGraph reaches it
 *   eff:  the gem taken, one more held, the player on it
 *   do:   a RouteAttempt to the gem. */
#include <stdio.h>
#include "actions.h"
#include "routeattempt.h"
#include "survey.h"

namespace simbot {

namespace {

class Collect : public Action {
public:
    const char *name() const override { return "collect"; }

    void offer(const Facts &f, const Survey &survey, std::vector<Step> &out) const override
    {
        if (f.done || f.at < 0 || f.gems >= survey.quota())
            return;
        for (size_t i = 0; i < survey.gems().size(); i++) {
            if (f.taken.has(i))
                continue;
            const Cell c = survey.gems()[i];
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
            s.after.taken.add(i);
            s.after.gems++;
            s.after.glides -= used;
            s.ms = ms * (1 + foeRisk(f, survey));
            char buf[48];
            snprintf(buf, sizeof(buf), "collect g%zu (%d,%d)", i, c.u, c.v);
            s.what = buf;
            out.push_back(std::move(s));
        }
    }

    std::unique_ptr<Attempt> attempt(const Step &step, const Survey &survey) const override
    {
        return std::make_unique<RouteAttempt>(Goal{survey.gems()[step.arg]}, survey.graph());
    }
};

}  // namespace

const Action &collectAction()
{
    static const Collect a;
    return a;
}

}  // namespace simbot
