/* What the tactics do with a thrower that bombs the player (FoeView::kind
 * 3): its bomb spends a block for the player (bait.cpp), so it is drawn on,
 * led and sent through a pad (send.cpp) to where its bomb lands. */
#pragma once

#include <vector>
#include "bombmodel.h"
#include "option.h"
#include "tactics.h"

namespace bot {

/* Whether a thrower that bombs the player stands, in the foe model as it
 * runs, within near of by (chebyshev). */
bool throwerNear(const FoeModel &foes, Step by, int near);

/* The first bomb the foe model has a thrower drop that spends a block, the
 * player keeping to going and then standing at its end for waitMs: false if
 * none does, or it is dropped before the player is there. */
bool baitedBomb(Situation &s, const Option &going, double waitMs, Blast *out);

/* o, which ends on from, walked on to to, a thrower led along a leg at a
 * time (the chase of a player too far off gives up, foe.cpp), the player
 * waiting after each until it keeps up, and sent through any pad on the way
 * before the player follows: false if it does not keep up. */
bool leadThrower(Situation &s, Option &o, Step from, Step to);

/* o, which ends on at, with a wait there until the foe model has a thrower
 * come within near of by, maxMs at most: false, o as it was, if none does. */
bool drawThrower(Situation &s, Option &o, Step at, Step by, int near, double maxMs);

}  // namespace bot
