/* A predicted view against the one seen: where they differ, by part of the
 * world, for measuring the sim (worldsim.h) against the game. */
#pragma once

#include <string>
#include <vector>
#include "botview.h"

namespace bot {

/* The parts compared.  Each is right or wrong as a whole for one look-ahead;
 * a part neither view has (no foes, say) is not counted. */
enum SimPart {
    PART_PLAYER_CELL,    // u, v, h
    PART_PLAYER_FACING,
    PART_PLAYER_MOVING,  // moving, falling
    PART_PLAYER_ALIVE,
    PART_PLAYER_ITEMS,   // gems, bombs, glides
    PART_FOE_COUNT,      // not dying
    PART_FOE_CELLS,      // each foe's cell, by index, the count right
    PART_BOMBS,          // the bombs' cells
    PART_BRIDGES,        // extended, moving
    PART_FALLINGS,       // armed, fallen
    PART_LIFTS,          // state, atTop
    PART_PLATFORMS,      // state, atLimit
    PART_CELLS,          // the grid, but for occupant and blastHeight
    PART_COUNT
};

const char *partName(SimPart part);

struct PartResult {
    bool        counted = false;
    bool        right = false;
    std::string detail;  // where wrong: a short note of the first difference
};

std::vector<PartResult> compareViews(const BotView &predicted, const BotView &seen);

}  // namespace bot
