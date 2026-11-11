/* exit: with the quota, walk onto the exit; the level is then done.
 *   pre:  gems held >= quota; the MoveGraph reaches the exit
 *   eff:  done
 *   do:   a RouteAttempt to the exit, then stand there till the game completes the
 *         level (which ends the run). */
#include <stdio.h>
#include "actions.h"
#include "routeattempt.h"
#include "survey.h"
#include "world.h"

namespace simbot {

namespace {

class ExitAttempt : public Attempt {
public:
    ExitAttempt(Cell exit, const MoveGraph &graph) : walk_(Goal{exit}, graph) {}
    Status act(const World &live, Work &work, unsigned *presses) override
    {
        if (!there_) {
            const Status s = walk_.act(live, work, presses);
            if (s != Done)
                return s;
            there_ = true;
        }
        if (live.completed())
            return Done;
        *presses = 0;
        return Pressing;
    }

private:
    RouteAttempt walk_;
    bool there_ = false;
};

class Exit : public Action {
public:
    const char *name() const override { return "exit"; }

    void offer(const Facts &f, const Survey &survey, std::vector<Step> &out) const override
    {
        if (f.done || f.at < 0 || f.gems < survey.quota())
            return;
        double ms;
        if (survey.graph().arrive(f.at, survey.exit(), &ms, f.glides) < 0)
            return;
        Step s;
        s.action = this;
        s.after  = f;
        s.after.done = true;
        s.ms = ms * (1 + foeRisk(f, survey));
        char buf[48];
        snprintf(buf, sizeof(buf), "exit (%d,%d)", survey.exit().u, survey.exit().v);
        s.what = buf;
        out.push_back(std::move(s));
    }

    std::unique_ptr<Attempt> attempt(const Step &, const Survey &survey) const override
    {
        return std::make_unique<ExitAttempt>(survey.exit(), survey.graph());
    }
};

}  // namespace

const Action &exitAction()
{
    static const Exit a;
    return a;
}

}  // namespace simbot
