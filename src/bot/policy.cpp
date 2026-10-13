#include "policy.h"

namespace bot {

namespace {

class Idle : public Policy {
public:
    unsigned act(const BotView &) override { return 0; }
};

/* Forward until a second passes on one cell, then a quarter turn right. */
class Wander : public Policy {
public:
    unsigned act(const BotView &view) override
    {
        const int u = view.player.u, v = view.player.v;
        if (u != lastU_ || v != lastV_) {
            lastU_ = u;
            lastV_ = v;
            since_ = view.now;
        }
        if (view.now - since_ > 1000.0) {
            since_ = view.now;
            return ACT_RIGHT;
        }
        return ACT_FORWARD;
    }

private:
    int    lastU_ = -1, lastV_ = -1;
    double since_ = 0;
};

}  // namespace

std::unique_ptr<Policy> makePolicy(const std::string &name)
{
    if (name == "idle")
        return std::make_unique<Idle>();
    if (name == "wander")
        return std::make_unique<Wander>();
    return nullptr;
}

}  // namespace bot
