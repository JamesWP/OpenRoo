/* The ambush maneuver: turn, drop a bomb, walk away, so that a foe chasing
 * the player walks into the blast.
 *
 * Each facing (as it is, a quarter turn either way, about) and each cell to
 * walk away to (the nearest within reach) is a candidate.  The bomb model
 * (bombmodel.h) gives the blast and the foe model (foemodel.h), with the foes
 * chasing the player along the walk, whether a foe dies in it; only the
 * candidates where one does, and the player is safe until the blast is over
 * (danger.h), are offered.  They are checked here, to be offered at all. */
#pragma once

#include <vector>
#include "option.h"
#include "tactics.h"

namespace bot {

void offerAmbushes(Situation &s, std::vector<Option> &out);

}  // namespace bot
