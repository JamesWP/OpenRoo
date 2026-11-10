#include "actions.h"

namespace simbot {

const Action &collectAction();
const Action &exitAction();

const std::vector<const Action *> &actions()
{
    static const std::vector<const Action *> all = {&collectAction(), &exitAction()};
    return all;
}

}  // namespace simbot
