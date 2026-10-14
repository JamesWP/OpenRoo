#include "goals.h"

namespace bot {

std::vector<Goal> selectGoals(const Senses &senses)
{
    std::vector<Goal> goals;
    // The foe dealt with before it meets the player.
    if (senses.bombs > 0 && senses.foes > 0 && senses.foeMeetsMs >= 0)
        goals.push_back({REMOVE_FOE, senses.foeMeetsMs});
    goals.push_back({FINISH, -1});
    return goals;
}

}  // namespace bot
