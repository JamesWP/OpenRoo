/* Walks as options (option.h): the tactics' common move. */
#pragma once

#include <utility>
#include <vector>
#include "option.h"
#include "tactics.h"

namespace bot {

/* Appends a walk to target by paths' quickest route, setting off when the
 * paths do (Paths::startMs) and after a further wait, to o's script and
 * schedule, and sets its cost to the time until it arrives.  False if
 * target is unreachable. */
bool walkTo(const Situation &s, const Paths &paths, Step target, double wait, Option &o);

/* leg, a walk on from where o ends, appended to o, whose cost becomes
 * leg's. */
void append(Option &o, const Option &leg);

/* nav with the falling tiles armed that the schedule steps on. */
NavModel armed(const NavModel &nav, const std::vector<Timed> &schedule);

/* A walk over nav, its falling tiles armed by o, from where o ends (at, its
 * last cell, as it faces then) to target after a wait, appended to o.
 * False, o as it was, if target is unreachable. */
bool walkOn(const Situation &s, const NavModel &nav, Option &o, Step at, Step target,
            double wait = 0);

/* A wait on at, where o ends, until ms from now, which becomes o's cost. */
void waitTill(const BotView &view, Option &o, double ms, Step at);

/* The facing the player has after a script, from facing: each Turn's, and
 * after each Walk the motor's (Motor::facingAfter), which turns its own way
 * where the route's turns tie. */
int facingAfter(int facing, const Script &script);

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
