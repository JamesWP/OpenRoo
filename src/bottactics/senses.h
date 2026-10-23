/* Sensing for goal selection (docs/BOT_DESIGN.md, section 3.3): what the
 * view and the models say about the goals worth having (goals.h). */
#pragma once

#include <vector>
#include "foemodel.h"
#include "goals.h"
#include "worlds.h"

namespace bot {

/* The goals worth planning for now, the most relevant first: the foes
 * sensed along ahead, the player's course (danger.h). */
std::vector<Goal> senseGoals(const BotView &view, FoeModel &foes, Worlds &worlds,
                             const std::vector<Timed> &ahead);

}  // namespace bot
