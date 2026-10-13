/* Perception: the BotView (botview.h) of the game as it stands, read from a
 * const Game.  The only place the bot reads the game. */
#pragma once

#include "botview.h"

class Game;

namespace bot {

BotView capture(const Game &game);

}  // namespace bot
