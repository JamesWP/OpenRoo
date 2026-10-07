/* The tick step: GameTick's `dt` argument, a double (GameTick zeroes it while
 * paused, state 5).  Every level object keeps a pointer to the Game's and
 * copies it to its own each tick. */
#pragma once

struct TickStep {
    double value{};
};
