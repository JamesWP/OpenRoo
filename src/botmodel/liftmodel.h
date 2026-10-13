/* The lift model (liftobject.cpp, platformobject.cpp): when a lift or a
 * platform is parked, and where.
 *
 * A lift rises from its base to its top at 200 ms a height, parks there 1.5 s,
 * falls to its base at the same rate and parks there 1.5 s, and so on.  The
 * player can step on or off it only while it is parked: moving, its height
 * changes under the step.  So a lift is, to the nav model, a cell that is open
 * at one height or the other in turn.  A platform is the same along its track:
 * its two end cells are open, at its height, while it is parked there. */
#pragma once

#include <vector>
#include "botview.h"

namespace bot {

/* A spell the lift stands parked: ms from the view's clock. */
struct Park {
    double from = 0, to = 0;
    int    height = 0;
};

/* The spells a platform stands parked at the two ends of its track: ms from the
 * view's clock, as above; the height is the platform's. */
void platformSpells(const BotView &view, const PlatformView &platform, double horizonMs,
                    std::vector<Park> *atOrigin, std::vector<Park> *atLimit);

/* The spells the lift parks within the next horizonMs of the view's clock, in
 * order. */
std::vector<Park> parkSpells(const BotView &view, const LiftView &lift, double horizonMs);

}  // namespace bot
