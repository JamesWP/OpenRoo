/* collect: walk to a gem not yet taken, while the quota is short.
 *   pre:  the gem is not taken; gems held < quota; the MoveGraph reaches it
 *   eff:  the gem taken, one more held, the player on it
 *   do:   GoTo the gem. */
#include <stdio.h>
#include "actions.h"
#include "goto.h"
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
            if (f.taken >> i & 1)
                continue;
            const Cell c = survey.gems()[i];
            double ms;
            const int to = survey.graph().arrive(f.at, c, &ms);
            if (to < 0)
                continue;
            Step s;
            s.action = this;
            s.arg    = (int)i;
            s.after  = f;
            s.after.at = to;
            s.after.taken |= (uint64_t)1 << i;
            s.after.gems++;
            s.ms = ms;
            char buf[48];
            snprintf(buf, sizeof(buf), "collect g%zu (%d,%d)", i, c.u, c.v);
            s.what = buf;
            out.push_back(std::move(s));
        }
    }

    std::unique_ptr<Attempt> attempt(const Step &step, const Survey &survey) const override
    {
        return std::make_unique<GoTo>(survey.gems()[step.arg]);
    }
};

}  // namespace

const Action &collectAction()
{
    static const Collect a;
    return a;
}

}  // namespace simbot
