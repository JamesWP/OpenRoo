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

}  // namespace whatif
