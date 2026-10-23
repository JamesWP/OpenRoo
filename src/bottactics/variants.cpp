#include "variant.h"

namespace bot {

namespace {

/* Round the throwers: no safe way past a thrower near by, or only another
 * way than the one planned, and the routes go round the throwers near the
 * player (nav.h, aroundThrowers): the quickest way past one is usually
 * safe, as it steps aside, but not always. */
class AroundThrowers : public Variant {
public:
    const char *name() const override { return "around throwers"; }
    bool wanted(const BotView &view, bool have, bool detour) const override
    {
        return (!have || detour) && !view.foes.empty();
    }
    std::unique_ptr<Situation> make(const BotView &view, FoeModel &foes) const override
    {
        auto s = std::make_unique<Situation>(view, foes, true);
        if (!s->nav.anyShut())
            return nullptr;
        return s;
    }
};

}  // namespace

const std::vector<const Variant *> &variants()
{
    static const AroundThrowers around;
    static const std::vector<const Variant *> all{&around};
    return all;
}

}  // namespace bot
