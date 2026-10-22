#include "goals.h"

namespace bot {

std::vector<Goal> selectGoals(const Senses &senses)
{
    std::vector<Goal> goals;
    // The foe dealt with before it meets the player.
    if (senses.bombs > 0 && senses.foes > 0 && senses.foeMeetsMs >= 0)
        goals.push_back({REMOVE_FOE, senses.foeMeetsMs});
    // A follower out after the catchers: kept out of its way until it has
    // bombed them, met soon or not.
    else if (senses.hunted > 0)
        goals.push_back({REMOVE_FOE, -1});
    // A foe that would drop the only way to the exit: dealt with first.
    else if (senses.wreckers > 0)
        goals.push_back({REMOVE_FOE, -1, senses.guard});
    // Gems to be had only from the foes: any foe about is one to blast.
    else if (senses.farm && senses.foes > 0)
        goals.push_back({REMOVE_FOE, -1});
    goals.push_back({FINISH, -1});
    return goals;
}

}  // namespace bot
