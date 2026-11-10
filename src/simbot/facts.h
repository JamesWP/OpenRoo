/* Facts: the level as the planner sees it.  Where the player stands (a
 * place of the MoveGraph), which of the survey's gems are taken and how
 * many the player holds, which of its bomb pickups are taken and how many
 * bombs the player holds, which of its foes are alive, and whether the
 * level is done. */
#pragma once

#include <stdint.h>
#include <array>
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
    uint64_t alive = 0;   // a bit per Survey::foes, but for the spared
    bool     done = false;

    auto key() const { return std::make_tuple(at, taken, gems, pickups, bombs, alive, done); }
    bool operator<(const Facts &o) const { return key() < o.key(); }
    bool operator==(const Facts &o) const { return key() == o.key(); }
};

/* The facts of a World. */
Facts factsOf(const World &w, const Survey &survey);

}  // namespace simbot
