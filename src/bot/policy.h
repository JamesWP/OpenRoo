/* A bot policy: what the bot holds down each tick, as player actions, from
 * what it knows (botview.h).  The run (botrun.h) turns the actions into the
 * keys they are bound to.  This is the seam the planner plugs into
 * (docs/BOT_DESIGN.md). */
#pragma once

#include <memory>
#include <string>

#include "botview.h"

namespace bot {

/* The player actions, as Input_Setup registers them in mode 1. */
enum Action : unsigned {
    ACT_FORWARD = 1u << 0,
    ACT_BACK    = 1u << 1,
    ACT_LEFT    = 1u << 2,
    ACT_RIGHT   = 1u << 3,
    ACT_BOMB    = 1u << 4,
};

class Policy {
public:
    virtual ~Policy() {}
    /* The actions to hold this tick, while the level is being played. */
    virtual unsigned act(const BotView &view) = 0;
    /* The run is over (botrun.h): the view it ended on. */
    virtual void end(const BotView &) {}
    /* What the policy last made of the level, for the run's summary: why it
     * stopped, say.  Empty when it has nothing to say. */
    virtual std::string note() const { return {}; }
};

/* The policy KAROO_BOT names, or null if it names none:
 *   idle    holds nothing;
 *   wander  walks forward and turns right whenever a step does not move it,
 *           a placeholder that exercises the harness (deaths, stuck);
 *   explore random single moves, logging the nav model's predictions
 *           against the game (explore.h);
 *   collect the gem quota by the quickest route, then the exit (collect.h);
 *   path:u,v;u,v;...
 *           walks the waypoints with the motor (motor.h), then stands.  Each
 *           waypoint is in line with the one before; the cells between are
 *           walked.  The first is normally the player's start. */
std::unique_ptr<Policy> makePolicy(const std::string &name);

}  // namespace bot
