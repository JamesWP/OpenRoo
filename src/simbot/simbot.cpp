#include "simbot.h"
#include "subgoals.h"
#include <stdio.h>
#include <chrono>
#include "logger.h"
#include "moves.h"
#include "tile.h"
#include "world.h"

namespace simbot {

SimBot::SimBot() = default;
SimBot::~SimBot() = default;

unsigned SimBot::act(const bot::BotView &, const World &world)
{
    ticks_++;
    if (!survey_) {
        // Wall time, for the log only: nothing is decided by it.
        const auto began = std::chrono::steady_clock::now();
        survey_ = std::make_unique<Survey>(world);
        subGoals_ = subGoals(*survey_);
        const double surveyMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began)
                .count();
        // The gems the MoveGraph has no way to from the start, gliding as
        // it may.
        std::string unreached;
        const MoveGraph &g = survey_->graph();
        const int from = g.find(placeOf(world));
        for (const Cell &c : survey_->gems()) {
            double ms;
            if (from < 0 || g.arrive(from, c, &ms, 255) < 0)
                unreached += (unreached.empty() ? "" : " ") + std::to_string(c.u) + "," +
                             std::to_string(c.v);
        }
        // What moves under the player, and the gliders to be had.
        int gliders = 0;
        const LevelMap *map = world.map();
        for (int v = 0; v < map->extentV(); v++)
            for (int u = 0; u < map->extentU(); u++)
                gliders += map->tile(u, v)->contents() == CONTENTS_PARAGLIDER;
        char buf[320];
        snprintf(buf, sizeof(buf),
                 "\"places\":%zu,\"moves\":%ld,\"breaking\":%ld,\"gems\":%zu,\"quota\":%d,"
                 "\"pickups\":%zu,\"foes\":%zu,\"lifts\":%zu,\"platforms\":%zu,"
                 "\"bridges\":%zu,\"gliders\":%d,\"glides\":%d,\"glided\":%ld,\"ms\":%.0f,",
                 g.size(), g.played(), g.breaking(), survey_->gems().size(), survey_->quota(),
                 survey_->pickups().size(), survey_->foes().size(), world.lifts().size(),
                 world.platforms().size(), world.bridges().size(), gliders,
                 (int)world.player()->glides(), g.glided(), surveyMs);
        log_.write(ticks_, "survey", buf + std::string("\"unreached\":") + SimLog::quote(unreached));
    }
    Work work;
    pressing_ = false;
    const unsigned presses = decide(world, work);
    if (pressing_ != pressed_)
        log_.write(ticks_, "pressing", pressing_ ? "\"on\":true" : "\"on\":false");
    pressed_ = pressing_;
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
            if (s == Attempt::Pressing) {
                pressing_ = true;
                return presses;
            }
            if (s == Attempt::Thinking)
                return 0;
            log_.write(ticks_, s == Attempt::Done ? "done" : "impossible",
                       "\"step\":" + SimLog::quote(step_.what) +
                           (attempt_->why().empty() ? ""
                                                    : ",\"why\":" + SimLog::quote(attempt_->why())));
            // Done, the rest of the plan is kept to, unless a new one is
            // quicker.
            rest_.clear();
            if (s == Attempt::Done && !plan_.steps.empty())
                rest_.assign(plan_.steps.begin() + 1, plan_.steps.end());
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
            const Survey &survey = *survey_;
            planner_ = std::make_unique<Planner>(
                survey, now, subGoals_, ruled_, requires_, rest_,
                [&survey](const Facts &start, std::vector<Step> &steps, Work &work) {
                    polishGems(survey, start, steps, work);
                });
        }
        if (!planner_->run(work))
            return 0;
        plan_ = planner_->plan();
        planner_.reset();
        char buf[128];
        snprintf(buf, sizeof(buf), "\"found\":%s,\"nodes\":%d,\"ms\":%.0f,\"polished\":%.0f,\"kept\":%s,",
                 plan_.found ? "true" : "false", plan_.nodes, plan_.ms, plan_.polished,
                 plan_.kept ? "true" : "false");
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
    Requirement r = a.requirement();
    if (!r)
        return false;
    log_.write(ticks_, "requires", "\"step\":" + SimLog::quote(step_.what) +
                                       ",\"requirement\":" + SimLog::quote(r.name));
    requires_[{step_.action, step_.arg}].push_back(std::move(r));
    return true;
}

std::vector<bot::Thought> SimBot::thoughts() const
{
    return {{"step", note_}, {"plan", plan_.describe()}};
}

}  // namespace simbot
