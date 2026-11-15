/* Facts: the level as the planner sees it.  Where the player stands (a
 * place of the MoveGraph), which of the survey's gems are taken and how
 * many the player holds, which of its bomb pickups and gliders are taken
 * and how many bombs and glider charges the player holds, which of its foes are alive, and whether the
 * level is done. */
#pragma once

#include <stdint.h>
#include <array>
#include <functional>
#include <string>
#include <tuple>

class World;

namespace simbot {

class Survey;

/* A set of the survey's gems or pickups, by index. */
struct IndexSet {
    static const int MAX = 256;
    std::array<uint64_t, MAX / 64> bits{};
    bool has(int i) const { return bits[i / 64] >> (i % 64) & 1; }
    void add(int i) { bits[i / 64] |= (uint64_t)1 << (i % 64); }
    bool operator<(const IndexSet &o) const { return bits < o.bits; }
    bool operator==(const IndexSet &o) const { return bits == o.bits; }
};

struct Facts {
    int      at = -1;     // MoveGraph place, -1: none of them
    IndexSet taken;
    int      gems = 0;    // held
    IndexSet pickups;     // taken
    int      bombs = 0;   // held
    IndexSet gliders;     // taken
    int      glides = 0;  // glider charges held
    uint64_t alive = 0;   // a bit per Survey::foes
    bool     done = false;

    auto key() const { return std::make_tuple(at, taken, gems, pickups, bombs, gliders, glides, alive, done); }
    bool operator<(const Facts &o) const { return key() < o.key(); }
    bool operator==(const Facts &o) const { return key() == o.key(); }
};

/* A condition on Facts a step needs before it is offered, learned by an
 * Attempt that found no way; the name is for the log ("dead f2"). */
struct Requirement {
    std::string name;
    std::function<bool(const Facts &)> holds;   // empty: no Requirement
    explicit operator bool() const { return (bool)holds; }
};

/* That the survey's foe with Listed id is dead; none if it is not one of
 * them (a thrower: no bomb kills it). */
Requirement foeDead(const Survey &survey, unsigned id);

/* The facts of a World. */
Facts factsOf(const World &w, const Survey &survey);

}  // namespace simbot
