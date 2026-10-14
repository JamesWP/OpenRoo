#include "action.h"

namespace bot {

const Action &collectAction();
const Action &exitAction();
const Action &bombsAction();
const Action &blastAction();
const Action &ambushAction();
const Action &switchAction();

const std::vector<const Action *> &actions()
{
    static const std::vector<const Action *> all{&collectAction(), &exitAction(), &bombsAction(),
                                                 &blastAction(), &ambushAction(), &switchAction()};
    return all;
}

const std::vector<const PlanAction *> &planActions()
{
    static const std::vector<const PlanAction *> all(actions().begin(), actions().end());
    return all;
}

}  // namespace bot
