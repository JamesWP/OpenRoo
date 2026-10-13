/* A script: what the player is to do, step by step, and the executor that
 * does it.  The planner (planner.h) chooses an option and hands its script
 * here; actions never press keys themselves.
 *
 * The steps:
 *   Turn  to a facing, the shorter way;
 *   Drop  a bomb, once the last one's 2 s are up;
 *   Walk  a path with the motor (motor.h);
 *   Wait  until the clock reaches a time.
 * Each starts with the player at rest, except Walk, which the motor runs. */
#pragma once

#include <string>
#include <vector>
#include "botview.h"
#include "motor.h"

namespace bot {

struct ScriptStep {
    enum Kind { Turn, Drop, Walk, Wait };
    Kind              kind = Wait;
    int               facing = 0;   // Turn
    std::vector<Step> path;         // Walk
    double            untilMs = 0;  // Wait
};

using Script = std::vector<ScriptStep>;

class Executor {
public:
    enum Status { Idle, Running, Done, Failed };

    void   start(Script script);
    /* Stops after the move under way. */
    void   halt();
    Status status() const { return status_; }
    /* The step under way, from 0; the script's size when done. */
    size_t at() const { return at_; }
    /* The path of the Walk under way, and how far along it the motor is. */
    const Script &script() const { return script_; }

    /* The presses to hold this tick (policy.h). */
    unsigned act(const BotView &view);

private:
    void next();

    Script   script_;
    size_t   at_ = 0;
    Status   status_ = Idle;
    Motor    motor_;
    bool     stepStarted_ = false;
    int      bombsBefore_ = 0;
    int      ticks_ = 0;        // in the current step
};

}  // namespace bot
