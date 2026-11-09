/* WorldSim: the whole level stepped forward, one engine tick at a time, the
 * player doing what it is told.  It runs the game's own rules: a copy of the
 * World (world.h), ticked by World::tick with play running and no one
 * listening to its sounds.
 *
 * Measured against the game (KAROO_BOT_SIM_LOG, src/bot/checks.h;
 * tools/bot/simcheck.py): its views (worldview.h) and the ones seen compare
 * field by field (compare.h).
 *
 * The player's input is the presses held each tick (policy.h), as the game
 * gets them; or a script (script.h), run by the motor's own Executor on the
 * predicted views, so the presses are the ones the bot would make. */
#pragma once

#include <memory>
#include "botview.h"
#include "script.h"

class World;

namespace bot {

class WorldSim {
public:
    /* The starting point: a copy of the world as it stands.  Its ticks are
     * as long as the last one it took, unless told otherwise. */
    explicit WorldSim(const World &start);
    WorldSim(const WorldSim &other);
    ~WorldSim();

    /* One engine tick, dt ms (the start's tick if 0), with presses held. */
    void tick(unsigned presses, double dt = 0);

    const World &world() const { return *world_; }
    /* The world as the bot would see it. */
    BotView view() const;
    double now() const;

private:
    std::unique_ptr<World> world_;  // 2.4 MB, so not kept inline
    double                 dt_;
};

/* Runs script from the sim's state until the clock's untilMs, the Executor
 * pressing on the predicted views; after it ends, nothing is held.  Whether
 * the script ran to its end without failing. */
bool runScript(WorldSim &sim, const Script &script, double untilMs);

}  // namespace bot
