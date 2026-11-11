/* SimBot: the policy (docs/SIMBOT_DESIGN.md).  It surveys the level once
 * (survey.h), then within each frame's work (work.h): plans from the facts
 * as they are (planner.h), and makes an Attempt at the plan's first step
 * (actions.h).  When the step is done it plans again; when it is impossible,
 * that step is left out of the next plan, until the facts change. */
#pragma once

#include <memory>
#include <string>
#include <vector>
#include "actions.h"
#include "planner.h"
#include "policy.h"
#include "simlog.h"
#include "survey.h"

namespace simbot {

class SimBot : public bot::Policy {
public:
    SimBot();
    ~SimBot();
    unsigned act(const bot::BotView &view, const World &world) override;
    std::string note() const override { return note_; }
    std::vector<bot::Thought> thoughts() const override;

private:
    /* Ticks without a plan before trying again. */
    static const unsigned long RETRY_TICKS = 60;

    unsigned decide(const World &world, Work &work);
    /* Keeps a Requirement the Attempt found, if it gave one that can be met. */
    bool learn(const Attempt &a);

    SimLog                   log_;
    std::unique_ptr<Survey>  survey_;
    std::unique_ptr<Planner> planner_;
    Plan                     plan_;
    std::unique_ptr<Attempt> attempt_;
    Step                     step_;
    std::vector<Step>        rest_;   // the plan's steps after step_, it done
    Ruled                    ruled_;       // steps impossible as things stand
    Facts                    ruledAt_;     // the facts they were found so in
    Requires                 requires_;    // Requirements learned, for the level
    unsigned long            ticks_ = 0, retryAt_ = 0;
    std::string              note_;
};

}  // namespace simbot
