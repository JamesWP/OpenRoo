/* The BotView (botview.h) of a World as it stands: what perception reads of
 * the game's level (bot/capture.cpp adds the Game's part, the level's name),
 * and what the world sim (worldsim.h) shows of its own. */
#pragma once

#include "botview.h"

class World;

namespace bot {

BotView viewOf(const World &world);

}  // namespace bot
