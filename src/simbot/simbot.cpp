#include "simbot.h"
#include <stdio.h>
#include "logger.h"
#include "moves.h"
#include "world.h"

namespace simbot {

SimBot::SimBot() = default;
SimBot::~SimBot() = default;

unsigned SimBot::act(const bot::BotView &, const World &world)
{
    ticks_++;
    if (!survey_) {
        survey_ = std::make_unique<Survey>(world);
        // The gems the MoveGraph has no way to from the start.
        std::string unreached;
        const MoveGraph &g = survey_->graph();
        const int from = g.find(placeOf(world));
        for (const Cell &c : survey_->gems()) {
            double ms;
            if (from < 0 || g.arrive(from, c, &ms) < 0)
                unreached += (unreached.empty() ? "" : " ") + std::to_string(c.u) + "," +
                             std::to_string(c.v);
        }
        char buf[192];
        snprintf(buf, sizeof(buf),
                 "\"places\":%zu,\"moves\":%ld,\"breaking\":%ld,\"gems\":%zu,\"quota\":%d,"
                 "\"pickups\":%zu,\"foes\":%zu,",
                 g.size(), g.played(), g.breaking(), survey_->gems().size(), survey_->quota(),
                 survey_->pickups().size(), survey_->foes().size());
        log_.write(ticks_, "survey", buf + std::string("\"unreached\":") + SimLog::quote(unreached));
    }
    Work work;
    const unsigned presses = decide(world, work);
    if (work.used > 0) {
        char buf[48];
        snprintf(buf, sizeof(buf), "\"used\":%ld", work.used);
        log_.write(ticks_, "work", buf);
    }
    return presses;
}

unsigned SimBot::decide(const World &world, Work &work)
{
    for (;;) {
        if (attempt_) {
            unsigned presses = 0;
            const Attempt::Status s = attempt_->act(world, work, &presses);
            if (s == Attempt::Pressing)
                return presses;
            if (s == Attempt::Thinking)
                return 0;
            log_.write(ticks_, s == Attempt::Done ? "done" : "impossible",
                       "\"step\":" + SimLog::quote(step_.what) +
                           (attempt_->why().empty() ? ""
                                                    : ",\"why\":" + SimLog::quote(attempt_->why())));
            if (s == Attempt::Impossible && learn(*attempt_)) {
                // A Requirement learned: the planner plans to meet it.
            } else if (s == Attempt::Impossible) {
                const Facts now = factsOf(world, *survey_);
                if (!(now == ruledAt_))
                    ruled_.clear();
                ruledAt_ = now;
                ruled_.insert({step_.action, step_.arg});

            }
            attempt_.reset();
            continue;
        }
        if (!atRest(world) || ticks_ < retryAt_)
            return 0;
        if (!planner_) {
            survey_->see(world);
            const Facts now = factsOf(world, *survey_);
            if (!(now == ruledAt_))
                ruled_.clear();
            planner_ = std::make_unique<Planner>(*survey_, now, ruled_, requires_);
        }
        if (!planner_->run(work))
            return 0;
        plan_ = planner_->plan();
        planner_.reset();
        char buf[64];
        snprintf(buf, sizeof(buf), "\"found\":%s,\"nodes\":%d,\"ms\":%.0f,",
                 plan_.found ? "true" : "false", plan_.nodes, plan_.ms);
        log_.write(ticks_, "plan", buf + std::string("\"plan\":") + SimLog::quote(plan_.describe()));
        if (!plan_.found) {
            note_ = "no plan";
            retryAt_ = ticks_ + RETRY_TICKS;
            return 0;
        }
        step_ = plan_.steps.front();
        note_ = step_.what;
        attempt_ = step_.action->attempt(step_, *survey_);
        log_.write(ticks_, "start", "\"step\":" + SimLog::quote(step_.what));
        if (work.out())
            return 0;
    }
}

bool SimBot::learn(const Attempt &a)
{
    const std::vector<unsigned> dead = a.requiresDead();
    if (dead.empty())
        return false;
    uint64_t bits = 0;
    for (unsigned id : dead) {
        size_t i = 0;
        while (i < survey_->foes().size() && survey_->foes()[i] != id)
            i++;
        if (i == survey_->foes().size())
            return false;   // one no bomb kills (a thrower): no way to meet it
        bits |= (uint64_t)1 << i;
    }
    requires_[{step_.action, step_.arg}] |= bits;
    std::string names;
    for (size_t i = 0; i < survey_->foes().size(); i++)
        if (bits >> i & 1)
            names += (names.empty() ? "f" : " f") + std::to_string(i);
    log_.write(ticks_, "requires", "\"step\":" + SimLog::quote(step_.what) +
                                       ",\"dead\":" + SimLog::quote(names));
    return true;
}

std::vector<bot::Thought> SimBot::thoughts() const
{
    return {{"step", note_}, {"plan", plan_.describe()}};
}

}  // namespace simbot
