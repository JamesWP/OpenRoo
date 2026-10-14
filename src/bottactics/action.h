/* An action (docs/BOT_DESIGN.md, sections 3.4 and 3.5): what the planner
 * plans with, and how the tactical layer does it.  One class an action, in
 * its own file, has both halves:
 *
 *   expand   (PlanAction, goap.h) from a state of the facts, the states it
 *            leads to and the steps there, at estimated costs;
 *   offer    the ways to do a plan's first step, its own, now: options for
 *            the executor (option.h), checked against the foes or to be.
 *
 * The planner, the tactics' realise and the executive go by the list of
 * actions, never by one; an action is added by writing its class and adding
 * it to the list (actions.cpp). */
#pragma once

#include <vector>
#include "board.h"
#include "goap.h"
#include "option.h"
#include "tactics.h"

namespace bot {

class Action : public PlanAction {
public:
    virtual void offer(Situation &s, const Board &board, const Plan &plan,
                       std::vector<Option> &out) const = 0;
};

/* The actions, in the order the planner expands them. */
const std::vector<const Action *> &actions();
/* The same, as the planner takes them. */
const std::vector<const PlanAction *> &planActions();

}  // namespace bot
