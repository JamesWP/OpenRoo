/* The bomb maneuvers: drop a bomb, then walk clear of its blast.
 *
 *   ambush  from where the player stands: turn to a facing (as it is, a
 *           quarter turn either way, about), drop, walk away to one of the
 *           nearest cells and wait there, so that a foe chasing the player
 *           walks into the blast.  The bomb model (bombmodel.h) gives the blast and the foe
 *           model (foemodel.h), with the foes chasing the player along the
 *           walk, whether a foe dies in it; only the candidates where one
 *           does are offered (two, where the gems are farmed from the
 *           foes the spawners make).
 *   blast   from a given site: walk there, turn to its facing, drop (at
 *           once, or after a wait if there is no way clear yet, a falling
 *           tile behind not back), walk away and wait for the blast to
 *           clear and the blocks it spends to be gone.
 * Both wait the blast out: the foes are predicted chasing the player where
 * the plan puts it, and a new plan would put it elsewhere.
 * In both the player must be safe until the blast is over (danger.h), and
 * the options are checked here, to be offered at all. */
#pragma once

#include <vector>
#include "option.h"
#include "tactics.h"

namespace bot {

void offerAmbushes(Situation &s, std::vector<Option> &out);

/* Whether the bombs are short: carriers must die for the gems and the
 * bombs are fewer than the foes, so each must take several, bunched up in
 * open ground (and the gems lying about are had first, the foes chasing). */
bool bombsShort(const BotView &view);
void offerBlast(Situation &s, Place site, std::vector<Option> &out);

}  // namespace bot
