/* Walks as options (option.h): the tactics' common move. */
#pragma once

#include <utility>
#include <vector>
#include "option.h"
#include "tactics.h"

namespace bot {

/* Appends a walk to target by paths' quickest route, starting startMs from
 * now and after a further wait, to o's script and schedule, and sets its
 * cost to the time until it arrives.  False if target is unreachable. */
bool walkTo(const Situation &s, const Paths &paths, Step target, double wait, double startMs,
            Option &o);

/* Walks from the player to each target, each costed with the ms after it
 * (target, after); with waits, after each of a few waits too. */
void offerWalks(const Situation &s, const std::vector<std::pair<Step, double>> &targets,
                const char *what, bool waits, std::vector<Option> &out);

/* Targets for offerWalks: the planned one first, costed with the rest of
 * plan after it, then the nearest others (if traps is given, not traps:
 * cells the quota and the exit cannot be had after; nor, against the clock,
 * those that would make the plan too late), costed with the whole plan
 * still to do. */
std::vector<std::pair<Step, double>> targets(const Situation &s, const Plan &plan, Step planned,
                                             const std::vector<Step> &others,
                                             const Board *traps = nullptr);

}  // namespace bot
