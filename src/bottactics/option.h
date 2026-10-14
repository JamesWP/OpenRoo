/* An option: one way to do something now (tactics.h).  A script for the
 * executor (script.h), the player's place over time while it runs, the
 * blasts of the bombs it drops and its cost. */
#pragma once

#include <string>
#include <vector>
#include "bombmodel.h"
#include "foemodel.h"
#include "nav.h"
#include "script.h"

namespace bot {

struct Option {
    std::string        what;          // the decision, for the note and the log
    Script             script;
    std::vector<Timed> schedule;      // ms from now
    std::vector<Blast> blasts;        // of the bombs it drops
    double             cost = 0;      // ms, with the rest of the plan after it
    double             lingerMs = 1000;  // how long the end must stay safe
    bool               checked = false;  // found safe already (an ambush is, to be one)
    double             committedUntil = 0;  // the clock's ms: not to be broken off for another before
    int                kills = 0;
    int                offCentre = 0;     // cells its kills are off its blast's centre, summed
    std::vector<FoeModel::Kill> killed;  // where and when it expects them (the clock's ms)

    /* The schedule still ahead, elapsedMs into it, from here: in ms from now. */
    std::vector<Timed> ahead(double elapsedMs, Step here) const;
};

}  // namespace bot
