/* Facts: the level as the planner sees it.  Where the player stands (a
 * place of the MoveGraph), which of the survey's gems are taken and how
 * many the player holds, and whether the level is done. */
#pragma once

#include <stdint.h>
#include <tuple>

class World;

namespace simbot {

class Survey;

struct Facts {
    int      at = -1;     // MoveGraph place, -1: none of them
    uint64_t taken = 0;   // a bit per Survey::gems
    int      gems = 0;    // held
    bool     done = false;

    auto key() const { return std::make_tuple(at, taken, gems, done); }
    bool operator<(const Facts &o) const { return key() < o.key(); }
    bool operator==(const Facts &o) const { return key() == o.key(); }
};

/* The facts of a World. */
Facts factsOf(const World &w, const Survey &survey);

}  // namespace simbot
