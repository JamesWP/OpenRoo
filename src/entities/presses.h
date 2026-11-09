/* The player's controls held in a tick, a bit each: what the World is given
 * as its input (World::applyPresses).  The game sets them from the keys bound
 * to the actions (sequencer/inputsetup.cpp); the bot holds them itself. */
#pragma once

enum PlayerPress : unsigned {
    PRESS_FORWARD  = 1u << 0,
    PRESS_BACK     = 1u << 1,
    PRESS_LEFT     = 1u << 2,
    PRESS_RIGHT    = 1u << 3,
    PRESS_BOMB     = 1u << 4,
    PRESS_HARAKIRI = 1u << 5,
};
