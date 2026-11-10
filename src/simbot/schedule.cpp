#include "schedule.h"
#include <algorithm>
#include <stdlib.h>
#include "world.h"

namespace simbot {

namespace {

// A tick or so past the reckoned time, so it has been ticked.
const double MARGIN_MS = 20;

bool beside(int u, int v, Cell c)
{
    return abs(u - c.u) + abs(v - c.v) <= 1;
}

/* The next two parkings of a thing that runs cells each way at perMs and
 * parks parkMs: in state (0 parked, else moving) since phaseStart. */
void addParks(std::vector<double> &out, int state, double phaseStart, int cells, float perMs,
              double parkMs)
{
    const double travel = cells / (double)perMs;
    const double first = (state == 0 ? phaseStart + parkMs : phaseStart) + travel;
    out.push_back(first + MARGIN_MS);
    out.push_back(first + parkMs + travel + MARGIN_MS);
}

}  // namespace

std::vector<double> waitTimes(const World &w, Cell c)
{
    std::vector<double> out;
    for (const LiftObject &l : w.lifts())
        if (beside(l.cellU(), l.cellV(), c))
            addParks(out, l.state(), l.phaseStart(), l.topHeight() - l.baseHeight(),
                     LiftObject::HEIGHT_PER_MS, LiftObject::PARK_MS);
    for (const PlatformObject &p : w.platforms()) {
        const bool alongU = p.kind() == 0x0a;
        const int from = alongU ? p.originU() : p.originV();
        bool near = false;
        for (int i = from; i <= p.limit() && !near; i++)
            near = alongU ? beside(i, p.originV(), c) : beside(p.originU(), i, c);
        if (near)
            addParks(out, p.state(), p.phaseStart(), p.limit() - from,
                     PlatformObject::CELLS_PER_MS, PlatformObject::PARK_MS);
    }
    for (const FallingTile &f : w.fallings())
        if (f.respawnPending())
            out.push_back(f.armedAt() + FallingTile::RESPAWN_MS + MARGIN_MS);
    const double stuck = w.player()->stuckSince();
    if (stuck != 0)
        out.push_back(stuck + MovableEntity::STUCK_MS + MARGIN_MS);
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace simbot
