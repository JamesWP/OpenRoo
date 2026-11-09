/* The bot's actions and its goal (planner.h).
 *
 *   collect gem   while the quota is short: walk to a gem not yet taken,
 *                 picking it up (and any on the way);
 *   exit          with the quota: walk onto the exit, and stand there until
 *                 the level is complete.
 *
 *   complete      the goal: the level is complete. */
#pragma once

#include <vector>
#include "planner.h"

namespace simbot {

const std::vector<const Action *> &actions();
const Goal &completeLevel();

}  // namespace simbot
