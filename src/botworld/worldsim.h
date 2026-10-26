/* WorldSim: the whole level stepped forward from a view, one engine tick at a
 * time, the player doing what it is told: the bot's copy of the game's rules.
 *
 * Built apart from the planner and the tactics, and measured against the game
 * (KAROO_BOT_SIM_LOG, src/bot/checks.h; tools/bot/simcheck.py) until it is
 * right enough to plan with.  Its state is a BotView, the shape perception
 * gives, so a predicted state and a seen one compare field by field
 * (compare.h).
 *
 * The player's input is the presses held each tick (policy.h), as the game
 * gets them; or a script (script.h), run by the motor's own Executor on the
 * predicted views, so the presses are the ones the bot would make.
 *
 * So far it holds no rules: the clock runs and nothing else moves.  That is
 * the baseline the rules are added against, one part of the world at a time. */
#pragma once

#include <vector>
#include "botview.h"
#include "script.h"

namespace bot {

/* Something that happened in a tick, for the caller to ask after. */
struct SimEvent {
    enum Kind { PlayerDied, GemTaken, BombDropped, Blast, FoeKilled, FoeFell, BlockSpent,
                SwitchWorked, BridgeDone, TileFell, FoeSpawned, ScriptFailed };
    Kind   kind;
    double ms;            // the clock's
    Step   at{-1, -1};
    int    who = -1;      // a foe's index, a bridge's...
};

class WorldSim {
public:
    /* The starting point: the view as it stands, nothing held. */
    explicit WorldSim(const BotView &start);

    /* One engine tick with presses held. */
    void tick(unsigned presses);

    const BotView &view() const { return view_; }
    double now() const { return view_.now; }
    /* What happened since the start, in order. */
    const std::vector<SimEvent> &events() const { return events_; }

private:
    BotView               view_;
    std::vector<SimEvent> events_;
};

/* Runs script from the sim's state until the clock's untilMs, the Executor
 * pressing on the predicted views; after it ends, nothing is held.  Whether
 * the script ran to its end without failing. */
bool runScript(WorldSim &sim, const Script &script, double untilMs);

}  // namespace bot
