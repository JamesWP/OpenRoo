/* The moves the actions build their options from (option.h), past the walks
 * (walk.h): which cells to go to, going there, waiting there on the foes,
 * and an option checked and taken. */
#pragma once

#include <functional>
#include <string>
#include <vector>
#include "option.h"
#include "tactics.h"

namespace bot {

/* A cell and the ms to it. */
struct Near {
    double ms;
    Step   cell;
};

/* The cells paths come to rest on soonest, within withinMs past their start,
 * (Paths::cost), the nearest most of them, nearest first, where keep (if given) says: not
 * where they start, onto a platform or a lift (it would carry the player
 * off), or, unless switches, onto a switch (it works a bridge no plan asked
 * for). */
std::vector<Near> nearCells(const Situation &s, const Paths &paths, double withinMs, size_t most,
                            const std::function<bool(Step)> &keep = {}, bool switches = false);

/* o: standing where the player is, if at is there, else the walk to it. */
bool goTo(const Situation &s, Step at, Option &o);

/* o, which ends on at, with a wait there until the foe model has came say
 * so, the player keeping to o (checked a tick at a time, from o's end, for
 * maxMs at most): false, o as it was, if it does not. */
bool waitForFoes(Situation &s, Option &o, Step at, double maxMs,
                 const std::function<bool(FoeModel &)> &came);

/* o taken as the step's whole doing: it lasts stepMs, its end stays safe for
 * lingerMs (danger.h), and it is described as what.  False if unsafe. */
bool commit(Situation &s, Option &o, double stepMs, double lingerMs, std::string what);

/* printf, to a string: an option's description. */
std::string describe(const char *format, ...);

}  // namespace bot
