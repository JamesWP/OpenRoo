#include "worldsim.h"

namespace bot {

WorldSim::WorldSim(const BotView &start) : view_(start)
{
}

void WorldSim::tick(unsigned)
{
    view_.now += view_.tickMs;
    view_.timeElapsedMs += (unsigned)view_.tickMs;
}

bool runScript(WorldSim &sim, const Script &script, double untilMs)
{
    Executor exec;
    exec.start(script);
    while (sim.now() < untilMs) {
        const unsigned presses = exec.status() == Executor::Running ? exec.act(sim.view()) : 0;
        sim.tick(presses);
    }
    return exec.status() != Executor::Failed;
}

}  // namespace bot
