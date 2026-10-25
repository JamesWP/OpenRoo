/* A bot policy: what the bot holds down each tick, as presses of the player's
 * controls, from what it knows (botview.h).  The run (botrun.h) turns the
 * presses into the keys they are bound to.  This is the seam every bot plugs
 * into (docs/BOT_DESIGN.md). */
#pragma once

#include <string>
#include <vector>

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

/* A line of what a policy is thinking, for the debug UI's Bot section. */
struct Thought {
    std::string label, text;
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
    /* What the policy is thinking, as of its last tick, for the debug UI:
     * empty when it has nothing to show. */
    virtual std::vector<Thought> thoughts() const { return {}; }
};

}  // namespace bot
