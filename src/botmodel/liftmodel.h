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

/* The moments, ms from the view's clock and within horizonMs, at which the
 * platform under way stands exactly over the cell `along` its track, strictly
 * between its ends.  A step off it there is taken only close to one: within
 * 0.15 of a cell past the cell (movableentity.cpp), so just after it when
 * advancing and just before it when retreating. */
std::vector<double> platformPasses(const BotView &view, const PlatformView &platform, int along,
                                   double horizonMs);

/* The platform whose track runs through (u, v), or null. */
const PlatformView *platformThrough(const BotView &view, int u, int v);

/* The cell the platform stands on (or is passing) atMs from the view's clock:
 * where what rides it is.  False beyond the model's reach. */
bool platformCellAt(const BotView &view, const PlatformView &platform, double atMs, Step *cell);

/* The lift on (u, v), or null. */
const LiftView *liftAt(const BotView &view, int u, int v);

/* The lift's height atMs from the view's clock, parked or on its way (the
 * height it has passed).  -1 beyond the model's reach. */
int liftHeightAt(const BotView &view, const LiftView &lift, double atMs);

/* A whole cycle of the lift's, ms: up, parked, down and parked. */
double liftCycleMs(const LiftView &lift);

/* The spells the lift parks within the next horizonMs of the view's clock, in
 * order. */
std::vector<Park> parkSpells(const BotView &view, const LiftView &lift, double horizonMs);

}  // namespace bot
