#include "option.h"
#include <cmath>

namespace bot {

std::vector<Timed> Option::ahead(double elapsedMs, Step here) const
{
    std::vector<Timed> rest{{0, here}};
    // Where it is now as the schedule has it, nearest in time (the player
    // runs a little ahead of the schedule or behind it): aloft, if so.
    double nearest = -1;
    for (const Timed &m : schedule)
        if (m.cell.u == here.u && m.cell.v == here.v &&
            (nearest < 0 || std::fabs(m.t - elapsedMs) < nearest)) {
            nearest = std::fabs(m.t - elapsedMs);
            rest.front().aloft = m.aloft;
        }
    for (const Timed &m : schedule)
        if (m.t > elapsedMs)
            rest.push_back({m.t - elapsedMs, m.cell, m.aloft});
    return rest;
}

}  // namespace bot
