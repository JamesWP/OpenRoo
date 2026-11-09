#include "worldsim.h"
#include "world.h"
#include "worldview.h"

namespace bot {

WorldSim::WorldSim(const World &start)
    : world_(std::make_unique<World>(start)), dt_(start.tickStep()->value)
{
}

WorldSim::WorldSim(const WorldSim &other)
    : world_(std::make_unique<World>(*other.world_)), dt_(other.dt_)
{
}

WorldSim::~WorldSim() = default;

void WorldSim::tick(unsigned presses, double dt)
{
    PlayEvents unheard;
    world_->tick(dt != 0 ? dt : dt_, presses, true, unheard);
}

BotView WorldSim::view() const
{
    return viewOf(*world_);
}

double WorldSim::now() const
{
    return world_->now();
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
