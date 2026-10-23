/* The executive's diagnostics (executive.h): each decision as one JSON
 * object a line, and the reach grid. */
#pragma once

#include <ostream>
#include <vector>
#include "deliberate.h"

namespace bot {

/* The decision d made for goals in view, its note what the agent loop
 * reports. */
void logDecision(std::ostream &out, const BotView &view, const Board &board,
                 const std::vector<Goal> &goals, bool midway, const Decision &d,
                 const std::string &note);

/* Where the player can come to rest, as the model sees it: # as things
 * stand, + once every block is spent, . never; a line a row. */
void writeReach(std::ostream &out, const BotView &view, const Board &board);

}  // namespace bot
