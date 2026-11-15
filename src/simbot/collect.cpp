/* collect: walk to a gem not yet taken, while the quota is short.
 *   pre:  the gem is not taken; gems held < quota; the MoveGraph reaches it;
 *         it is one of the NEAREST nearest in its region
 *   eff:  the gem taken, one more held, the player on it
 *   do:   a RouteAttempt to the gem. */
#include <stdio.h>
#include <algorithm>
#include <map>
#include "actions.h"
#include "routeattempt.h"
#include "survey.h"

namespace simbot {

namespace {

/* The gems offered in each region: the nearest so many. */
const int NEAREST = 4;

class Collect : public Action {
public:
    const char *name() const override { return "collect"; }

    void offer(const Facts &f, const Survey &survey, std::vector<Step> &out,
               int only) const override
    {
        if (f.done || f.at < 0 || f.gems >= survey.quota())
            return;
        // The nearest few in each region the player comes to (regions are
        // left for good, so each is offered its own).
        struct Near {
            double ms;
            size_t gem;
            int    to, used;
        };
        std::vector<Near> near;
        const MoveGraph &g = survey.graph();
        for (size_t i = 0; i < survey.gems().size(); i++) {
            if (f.taken.has(i) || (only >= 0 && (int)i != only))
                continue;
            Near n{0, i, -1, 0};
            n.to = g.arrive(f.at, survey.gems()[i], &n.ms, f.glides, &n.used);
            if (n.to >= 0)
                near.push_back(n);
        }
        std::sort(near.begin(), near.end(), [](const Near &a, const Near &b) {
            return a.ms < b.ms || (a.ms == b.ms && a.gem < b.gem);
        });
        std::map<int, int> offered;   // by region
        for (const Near &n : near) {
            if (offered[g.region(n.to)]++ >= NEAREST && only < 0)
                continue;
            const Cell c = survey.gems()[n.gem];
            Step s;
            s.action = this;
            s.arg    = (int)n.gem;
            s.after  = f;
            s.after.at = n.to;
            s.after.taken.add(n.gem);
            s.after.gems++;
            s.after.glides -= n.used;
            s.ms = n.ms;
            char buf[48];
            snprintf(buf, sizeof(buf), "collect g%zu (%d,%d)", n.gem, c.u, c.v);
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
