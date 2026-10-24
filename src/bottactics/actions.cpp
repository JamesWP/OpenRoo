#include "action.h"

namespace bot {

const Action &collectAction();
const Action &exitAction();
const Action &bombsAction();
const Action &blastAction();
const Action &ambushAction();
const Action &goadAction();
const Action &releaseAction();
const Action &awaitAction();
const Action &lureAction();
const Action &baitAction();
const Action &sendAction();
const Action &switchAction();
const Action &gliderAction();
const Action &glideAction();
const Action &crossAction();
const Action &freezeAction();

const std::vector<const Action *> &actions()
{
    static const std::vector<const Action *> all{&collectAction(), &exitAction(), &bombsAction(),
                                                 &blastAction(), &releaseAction(), &awaitAction(), &lureAction(), &baitAction(), &sendAction(), &ambushAction(), &goadAction(), &switchAction(),
                                                 &gliderAction(), &glideAction(), &crossAction(), &freezeAction()};
    return all;
}

const std::vector<const PlanAction *> &planActions()
{
    static const std::vector<const PlanAction *> all(actions().begin(), actions().end());
    return all;
}

}  // namespace bot
