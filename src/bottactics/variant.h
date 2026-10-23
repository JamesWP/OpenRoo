/* A situation variant (docs/BOT_DESIGN.md, section 3.7): another situation
 * (tactics.h) to plan a decision in besides the plain one, when what was
 * found in that may be bettered.  Deliberation (deliberate.h) plans in each
 * variant wanted and takes the cheapest option of all; a variant is added
 * by writing its class and adding it to the list (variants.cpp). */
#pragma once

#include <memory>
#include <vector>
#include "tactics.h"

namespace bot {

class Variant {
public:
    virtual ~Variant() = default;
    virtual const char *name() const = 0;
    /* Whether to plan in it: have, an option was found so far; detour, it
     * is not the way first planned (that one was found unsafe). */
    virtual bool wanted(const BotView &view, bool have, bool detour) const = 0;
    /* The situation; null if it would be no different from the plain one. */
    virtual std::unique_ptr<Situation> make(const BotView &view, FoeModel &foes) const = 0;
};

/* The variants, in the order they are tried. */
const std::vector<const Variant *> &variants();

}  // namespace bot
