/* Walks as options (option.h): the tactics' common move. */
#pragma once

#include <utility>
#include <vector>
#include "option.h"
#include "tactics.h"

namespace bot {

/* Appends a walk to target by paths' quickest route, setting off when the
 * paths do (Paths::startMs) and after a further wait, to o's script and
 * schedule, and sets its cost to the time until it arrives (target the
 * start, a wait there or none).  False if target is unreachable. */
bool walkTo(const Situation &s, const Paths &paths, Step target, double wait, Option &o);

/* o waited out where it ends till ms from now: a wait in its script, an end
 * to its schedule there then, and its cost.  Nothing if o ends no sooner. */
void waitTill(const BotView &view, Option &o, double ms);

/* Appends leg, a walk from where o ends, to o: its script, its schedule past
 * the first entry (where o ends) and its cost. */
void append(Option &o, const Option &leg);

/* The quickest routes on from the end of a schedule: over a copy of nav with
 * the falling tiles the schedule goes over armed (its walk arms them), from
 * rest on at facing facing, setting off setOffMs from now, within bound. */
class RoutesOn {
public:
    RoutesOn(const NavModel &nav, const std::vector<Timed> &schedule, Step at, int facing,
             double setOffMs, const Bound &bound = Bound());
    RoutesOn(const RoutesOn &) = delete;
    RoutesOn &operator=(const RoutesOn &) = delete;

    const Paths &paths() const { return paths_; }

private:
    NavModel nav_;
    Paths    paths_;
};

/* o walked on, from where it ends (at, facing as its script leaves the
 * player, when it ends: its cost), to target after a wait, over nav as
 * RoutesOn has it.  False, o as it was, if target cannot be reached. */
bool walkOn(const Situation &s, const NavModel &nav, Option &o, Step at, Step target,
            double wait = 0);

/* Getting clear of a blast: the cells to walk away to are within RETREAT_MS
 * of setting off, and a wait for the blast ends CLEAR_MARGIN_MS after its
 * zone clears. */
const double RETREAT_MS = 2500;
const double CLEAR_MARGIN_MS = 200;

/* The nearest most cells paths come to rest on within RETREAT_MS of setting
 * off, nearest first: none on a platform or a lift, which may carry the
 * player along.  Cells as near in grid order if stable, else as std::sort
 * leaves them (the ambushes': which of a tie is taken changes outcomes). */
std::vector<Step> retreatCells(const BotView &view, const Paths &paths, size_t most, bool stable);

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
