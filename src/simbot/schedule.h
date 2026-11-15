/* Schedule: when waiting is worth it, worked out from the game's rules
 * rather than played.  The lifts and platforms run their tracks on the clock
 * alone (liftobject.cpp, platformobject.cpp), parking at each end; a sticky
 * pad lets the player go STUCK_MS after it caught it (movableentity.cpp); a
 * fallen falling tile comes back RESPAWN_MS after it was armed
 * (fallingtile.cpp).
 * A wait is made until one of these, never in steps: riding a lift or a
 * platform, a wait until it passes a height or a cell is how the player gets
 * off part way. */
#pragma once

#include <vector>
#include "moves.h"

class World;

namespace simbot {

/* The clock times, soonest first, worth waiting until with the player on
 * cell c: when each lift or platform beside c (on it, or a step from it)
 * next parks at each end of its run, and when it next passes each height or
 * cell beside c each way (a lift not ridden: only the heights a step from
 * the player's, or a drop), timed for a step on or off it then (a step off one
 * moving is let through only as it passes a whole height or cell); when
 * each fallen tile comes back (wherever it is: one may be the way back);
 * and when a sticky pad holding the player lets it go. */
std::vector<double> waitTimes(const World &w, Cell c);

}  // namespace simbot
