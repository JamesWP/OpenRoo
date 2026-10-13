/* A bot policy: what the bot holds down each tick, as presses of the player's
 * controls, from what it knows (botview.h).  The run (botrun.h) turns the
 * presses into the keys they are bound to.  This is the seam every bot plugs
 * into (docs/BOT_DESIGN.md). */
#pragma once

#include <string>

#include "botview.h"

namespace bot {

/* The player's controls, as Input_Setup registers them in mode 1. */
enum Press : unsigned {
    PRESS_FORWARD = 1u << 0,
    PRESS_BACK    = 1u << 1,
    PRESS_LEFT    = 1u << 2,
    PRESS_RIGHT   = 1u << 3,
    PRESS_BOMB    = 1u << 4,
};

class Policy {
public:
    virtual ~Policy() {}
    /* The presses to hold this tick, while the level is being played. */
    virtual unsigned act(const BotView &view) = 0;
    /* The run is over (botrun.h): the view it ended on. */
    virtual void end(const BotView &) {}
    /* What the policy last made of the level, for the run's summary: why it
     * stopped, say.  Empty when it has nothing to say. */
    virtual std::string note() const { return {}; }
};

}  // namespace bot
