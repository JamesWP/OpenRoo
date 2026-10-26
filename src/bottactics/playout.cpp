#include "playout.h"

namespace bot {

std::function<Step(double)> keepingTo(const BotView &view, const std::vector<Timed> &schedule)
{
    return [&view, &schedule](double clock) { return entryAt(schedule, view.now, clock).cell; };
}

void playOut(FoeModel &foes, const BotView &view, const std::vector<Timed> &schedule,
             double untilMs)
{
    foes.load(view);
    foes.run(view.now + untilMs, keepingTo(view, schedule));
}

}  // namespace bot
