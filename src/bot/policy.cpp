#include "policy.h"
#include <stdio.h>
#include "explore.h"
#include "motor.h"

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

/* Walks a fixed path with the motor, then stands. */
class Path : public Policy {
public:
    explicit Path(std::vector<Step> path) { motor_.follow(std::move(path)); }
    unsigned act(const BotView &view) override { return motor_.act(view); }

private:
    Motor motor_;
};

/* "u,v;u,v;...": waypoints, each in line with the one before (same u or
 * same v), joined by the cells between.  Empty if malformed. */
std::vector<Step> parse_waypoints(const std::string &spec)
{
    std::vector<Step> path;
    size_t at = 0;
    while (at < spec.size()) {
        size_t end = spec.find(';', at);
        if (end == std::string::npos)
            end = spec.size();
        int u, v;
        if (sscanf(spec.substr(at, end - at).c_str(), "%d,%d", &u, &v) != 2)
            return {};
        if (!path.empty()) {
            const Step from = path.back();
            if (from.u != u && from.v != v)
                return {};
            const int du = u > from.u ? 1 : u < from.u ? -1 : 0;
            const int dv = v > from.v ? 1 : v < from.v ? -1 : 0;
            for (Step c = from; c.u != u || c.v != v;) {
                c.u += du;
                c.v += dv;
                path.push_back(c);
            }
        } else {
            path.push_back({u, v});
        }
        at = end + 1;
    }
    return path;
}

}  // namespace

std::unique_ptr<Policy> makePolicy(const std::string &name)
{
    if (name.compare(0, 5, "path:") == 0) {
        std::vector<Step> path = parse_waypoints(name.substr(5));
        if (path.empty())
            return nullptr;
        return std::make_unique<Path>(std::move(path));
    }
    if (name == "idle")
        return std::make_unique<Idle>();
    if (name == "explore")
        return std::make_unique<Explore>();
    if (name == "wander")
        return std::make_unique<Wander>();
    return nullptr;
}

}  // namespace bot
