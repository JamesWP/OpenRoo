/* A fallback (docs/BOT_DESIGN.md, section 3.7): what the player does when no
 * goal's plan has a first step that can be done safely, to keep safe while
 * things change.  Like an action (action.h) it offers options, but nothing
 * plans with it; each says for itself when it applies.  Deliberation
 * (deliberate.h) tries them in the list's order, the first with a safe
 * option taken; a fallback is added by writing its class and adding it to
 * the list (fallbacks.cpp). */
#pragma once

#include <vector>
#include "board.h"
#include "goap.h"
#include "option.h"
#include "tactics.h"

namespace bot {

class Fallback {
public:
    virtual ~Fallback() = default;
    virtual const char *name() const = 0;
    /* Its options, unchecked; none where it does not apply.  finish is the
     * plan to finish the level, found or not. */
    virtual void offer(Situation &s, const Board &board, const Plan &finish,
                       std::vector<Option> &out) const = 0;
};

/* The fallbacks, in the order they are tried. */
const std::vector<const Fallback *> &fallbacks();

}  // namespace bot
