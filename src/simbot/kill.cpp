/* kill: drop a bomb that kills a foe, and live through the blast.
 *   pre:  the foe alive; a bomb held; the MoveGraph reaches the foe's cell,
 *         or one beside it, as last seen
 *   eff:  the foe dead, a bomb fewer, the player near where it was seen
 *   do:   a RouteAttempt with the foe as its goal (routing.h): walking,
 *         waiting and one drop, played on copies till one has the foe dead.
 * The foe moves, so where it is reckoned to be is only where it was last
 * seen (Survey::see). */
#include <stdio.h>
#include "actions.h"
#include "routeattempt.h"
#include "survey.h"
#include "world.h"

namespace simbot {

namespace {

class Kill : public Action {
public:
    const char *name() const override { return "kill"; }

    void offer(const Facts &f, const Survey &survey, std::vector<Step> &out) const override
    {
        if (f.done || f.at < 0 || f.bombs == 0)
            return;
        for (size_t i = 0; i < survey.foes().size(); i++) {
            if (!(f.alive >> i & 1))
                continue;
            const Cell c = survey.seen((int)i);
            // The quickest arrival on its cell or one beside it.
            const Cell near[] = {c, {c.u + 1, c.v}, {c.u - 1, c.v}, {c.u, c.v + 1}, {c.u, c.v - 1}};
            int to = -1;
            double best = 0;
            for (const Cell &n : near) {
                double ms;
                const int p = survey.graph().arrive(f.at, n, &ms);
                if (p >= 0 && (to < 0 || ms < best)) {
                    to = p;
                    best = ms;
                }
            }
            if (to < 0)
                continue;
            Step s;
            s.action = this;
            s.arg    = (int)i;
            s.after  = f;
            s.after.at = to;
            s.after.alive &= ~((uint64_t)1 << i);
            s.after.bombs--;
            s.ms = best + Bomb::CLEAR_MS;
            char buf[48];
            snprintf(buf, sizeof(buf), "kill f%zu (%d,%d)", i, c.u, c.v);
            s.what = buf;
            out.push_back(std::move(s));
        }
    }

    std::unique_ptr<Attempt> attempt(const Step &step, const Survey &survey) const override
    {
        Goal g;
        g.cell = survey.seen(step.arg);
        g.kill = true;
        g.foe  = survey.foes()[step.arg];
        return std::make_unique<RouteAttempt>(g, survey.graph());
    }
};

}  // namespace

const Action &killAction()
{
    static const Kill a;
    return a;
}

}  // namespace simbot
