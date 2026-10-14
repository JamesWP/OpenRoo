/* Goal selection (docs/BOT_DESIGN.md, section 3.3): which goals are worth
 * planning for now, the most relevant first.
 *
 *   finish       always: the level is the point;
 *   remove foe   bombs held, and a foe meets the player soon on the course
 *                it is on (sensed with the danger check, danger.h), above
 *                finishing: a foe met is better met with a bomb than avoided.
 *
 * Each goal is planned for in turn (goap.h) until one can be done safely;
 * the executive asks again as things change, and a goal above the one
 * under way breaks it off (executive.h). */
#pragma once

#include <vector>
#include "goap.h"

namespace bot {

/* How far ahead the foes are sensed: a foe met later is no threat yet. */
const double FOE_HORIZON_MS = 6000;

/* What goal selection goes by. */
struct Senses {
    int    bombs = 0;
    int    foes = 0;
    double foeMeetsMs = -1;   // a foe meets the player this soon, or -1: none soon
};

struct Goal {
    GoalKind kind = FINISH;
    double   by = -1;         // ms: when it must be under way by (-1: no limit)
};

/* The relevant goals, the most relevant first. */
std::vector<Goal> selectGoals(const Senses &senses);

}  // namespace bot
