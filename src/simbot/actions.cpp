#include "actions.h"
#include "survey.h"

namespace simbot {

const Action &collectAction();
const Action &exitAction();
const Action &pickupAction();
const Action &killAction();
const Action &gliderAction();
const Action &trapAction();

const std::vector<const Action *> &actions()
{
    static const std::vector<const Action *> all = {&collectAction(), &exitAction(),
                                                    &pickupAction(), &killAction(),
                                                    &gliderAction(), &trapAction()};
    return all;
}

}  // namespace simbot
