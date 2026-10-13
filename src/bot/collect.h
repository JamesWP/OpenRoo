/* The collect policy: the gem quota by the quickest safe route, then the exit.
 *
 * From rest it plans with the nav model (nav.h): the quickest routes from the
 * player and between the gems and the exit, then which gems to take and in
 * what order (route.h).  Before setting off it checks the way to the first
 * against the foes and bombs (danger.h): if that meets one, it tries setting
 * off a little later, then the other gems in order of cost, then the exit,
 * and failing all of those steps away to the nearest cell it can stand on
 * safely for a while.  While walking it checks the rest of the way again
 * every quarter second, and stops to plan afresh if it has become unsafe.
 *
 * It plans again from rest whenever the gem count changes, the motor is done,
 * or (after finding no route) the terrain changes.  The routes between gems
 * are kept until the terrain changes.  Its note (policy.h) says why it
 * stopped, or what it last had to do to stay safe. */
#pragma once

#include <string>
#include <vector>
#include "foemodel.h"
#include "motor.h"
#include "nav.h"
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
    /* Takes the first of the targets whose way is safe, setting off at once
     * or (with waits) after one of the waits; true if one was. */
    bool goSafely(const BotView &view, const Paths &from, const std::vector<Step> &targets,
                  const char *what, bool waits = true);
    /* Whether the rest of the way being walked is still safe. */
    bool stillSafe(const BotView &view);

    Motor  motor_;
    FoeModel sim_;
    bool   planned_ = false;
    bool   noRoute_ = false;   // the last plan found none: wait for the terrain to change
    int    gems_ = -1;
    Step   target_{-1, -1};
    unsigned long ticks_ = 0;
    std::string note_;

    unsigned long long terrain_ = 0;   // a hash of what the routes depend on
    bool               surveyed_ = false;
    std::vector<Step>  gemCells_;      // every gem on the map when surveyed
    std::vector<std::vector<double>> between_;  // gem i to gem j, and to the exit (last)
};

}  // namespace bot
