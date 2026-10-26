/* The foes played out (foemodel.h) against the player keeping to a
 * schedule (nav.h, Timed): how the tactics ask what the foes would do. */
#pragma once

#include <functional>
#include <vector>
#include "foemodel.h"
#include "nav.h"

namespace bot {

/* The player's cell at a clock's ms, keeping to schedule (ms from the
 * view's now), as the foe model asks it.  It holds schedule by reference. */
std::function<Step(double)> keepingTo(const BotView &view, const std::vector<Timed> &schedule);

/* The foes loaded from the view and run until untilMs from now, the player
 * keeping to schedule. */
void playOut(FoeModel &foes, const BotView &view, const std::vector<Timed> &schedule,
             double untilMs);

}  // namespace bot
