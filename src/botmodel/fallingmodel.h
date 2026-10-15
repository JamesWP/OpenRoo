/* The falling tile model (fallingtile.cpp): a falling tile arms when anything
 * stands on it, and falls 1.5 s later, taking whoever is on it then with it. */
#pragma once

#include "botview.h"

namespace bot {

/* When the falling tile on c falls if the player steps on at enterMs: 1.5 s
 * later, or sooner if it is armed already.  Both in ms from the view's
 * clock; < 0 if c holds no falling tile. */
double fallsAt(const BotView &view, Step c, double enterMs);

/* Whether the falling tile on c is gone at atMs from the view's clock: it has
 * fallen and is not back yet (it comes back 5 s after it was armed, if it
 * comes back). */
bool goneAt(const BotView &view, Step c, double atMs);

/* The spell the falling tile on c is gone for if it is armed at armedMs:
 * [*from, *back), ms from the view's clock (*back huge if it never returns).
 * False if c holds no falling tile. */
bool goneSpell(const BotView &view, Step c, double armedMs, double *from, double *back);

/* When the fallen tile on c is back, ms from the view's clock; < 0 if it is
 * not fallen, a huge number if it never returns. */
double backAt(const BotView &view, Step c);

}  // namespace bot
