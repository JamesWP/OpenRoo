/* The bot's sub-goals, and the polish of its plans: what the planner is
 * given to plan for, knowing nothing of the game itself. */
#pragma once

#include <vector>
#include "planner.h"

namespace simbot {

class Survey;

/* As few foes alive as can be, then the level done (its estimate: the rest
 * of the quota, nearest gem first, and the exit, on the MoveGraph's times).
 * They keep a reference to survey. */
std::vector<SubGoal> subGoals(const Survey &survey);

/* The gems steps (from start) collect first, put in a quicker order by
 * random edits (one moved, a stretch turned about, two swapped) kept when
 * quicker by the MoveGraph's times (collect.cpp).  A Polish. */
void polishGems(const Survey &survey, const Facts &start, std::vector<Step> &steps, Work &work);

}  // namespace simbot
