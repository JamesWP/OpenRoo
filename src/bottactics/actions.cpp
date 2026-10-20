#include "action.h"

namespace bot {

const Action &collectAction();
const Action &exitAction();
const Action &bombsAction();
const Action &blastAction();
const Action &ambushAction();
const Action &releaseAction();
const Action &awaitAction();
const Action &baitAction();
const Action &switchAction();
const Action &gliderAction();
const Action &glideAction();
const Action &crossAction();

const std::vector<const Action *> &actions()
{
    static const std::vector<const Action *> all{&collectAction(), &exitAction(), &bombsAction(),
                                                 &blastAction(), &releaseAction(), &awaitAction(), &baitAction(), &ambushAction(), &switchAction(),
                                                 &gliderAction(), &glideAction(), &crossAction()};
    return all;
}

const std::vector<const PlanAction *> &planActions()
{
    static const std::vector<const PlanAction *> all(actions().begin(), actions().end());
    return all;
}

}  // namespace bot
