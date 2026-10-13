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
};

/* The policy KAROO_BOT names, or null if it names none:
 *   idle    holds nothing;
 *   wander  walks forward and turns right whenever a step does not move it,
 *           a placeholder that exercises the harness (deaths, stuck). */
std::unique_ptr<Policy> makePolicy(const std::string &name);

}  // namespace bot
