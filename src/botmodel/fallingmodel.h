/* The falling tile model (fallingtile.cpp): a falling tile arms when anything
 * stands on it, and falls 1.5 s later, taking whoever is on it then with it. */
#pragma once

#include "botview.h"

namespace bot {

/* When the falling tile on c falls if the player steps on at enterMs: 1.5 s
 * later, or sooner if it is armed already.  Both in ms from the view's
 * clock; < 0 if c holds no falling tile. */
double fallsAt(const BotView &view, Step c, double enterMs);

}  // namespace bot
