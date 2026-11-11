#include "actions.h"
#include "survey.h"

namespace simbot {

namespace {

/* The ms a step's ms counts again for each foe alive. */
const double FOE_RISK = 1.0;

}  // namespace

double foeRisk(const Facts &f, const Survey &survey)
{
    if (!f.alive)
        return 0;
    bool bombs = f.bombs > 0;
    for (size_t i = 0; i < survey.pickups().size() && !bombs; i++)
        bombs = !f.pickups.has(i);
    return bombs ? FOE_RISK * __builtin_popcountll(f.alive) : 0;
}

const Action &collectAction();
const Action &exitAction();
const Action &pickupAction();
const Action &killAction();
const Action &gliderAction();

const std::vector<const Action *> &actions()
{
    static const std::vector<const Action *> all = {&collectAction(), &exitAction(),
                                                    &pickupAction(), &killAction(),
                                                    &gliderAction()};
    return all;
}

}  // namespace simbot
