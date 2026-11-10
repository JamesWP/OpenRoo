/* What-ifs: edits that turn a copy of the World into a hypothetical one, for
 * a bot to play ahead on (src/simbot): "were this foe gone", "were this
 * block spent".  They are not the game's rules and no game code calls them;
 * each is named whatif::... so it is never taken for one. */
#pragma once

class World;

namespace whatif {

/* Every foe gone, its claims on its tiles freed, and no timed spawner
 * spawning another. */
void removeFoes(World &w);

/* Every falling tile back in place and unarmed, as at the start, but for
 * one armed under the player: it still falls. */
void restoreFallings(World &w);

}  // namespace whatif
