/* The collect policy: the gem quota by the quickest route, then the exit.
 *
 * From rest it plans with the nav model (nav.h): the quickest routes from the
 * player and between the gems and the exit, then which gems to take and in
 * what order (route.h), and walks to the first with the motor.  It plans
 * again from rest whenever the gem count changes (a gem picked up on the way
 * counts) or the motor arrives or is blocked.  The routes between gems are
 * kept until the terrain changes (a block blown, a tile fallen).  Foes are not
 * seen yet.
 *
 * Its note (policy.h) says why it stopped: no route to enough gems, or none
 * to the exit. */
#pragma once

#include <string>
#include <vector>
#include "motor.h"
#include "policy.h"

namespace bot {

class Collect : public Policy {
public:
    unsigned act(const BotView &view) override;
    std::string note() const override { return note_; }

private:
    void plan(const BotView &view);
    /* The routes between the gems and to the exit, again if the terrain has
     * changed since they were found. */
    void survey(const BotView &view);

    Motor motor_;
    bool  planned_ = false;
    bool  noRoute_ = false;   // the last plan found none: wait for the terrain to change
    int   gems_ = -1;
    std::string note_;

    unsigned long long terrain_ = 0;   // a hash of what the routes depend on
    bool               surveyed_ = false;
    std::vector<Step>  gemCells_;      // every gem on the map when surveyed
    std::vector<std::vector<double>> between_;  // gem i to gem j, and to the exit (last)
};

}  // namespace bot
