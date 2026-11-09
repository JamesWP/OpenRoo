/* SimBot: a player bot that plays ahead on copies of the level (world.h)
 * and nothing else: no model of the game's rules of its own.
 *
 * From rest it plans (planner.h) the presses of every tick to the goal, plays
 * them out on a copy to know the World it expects before each, and then
 * presses them.  It plans again whenever the game is not where it expected
 * (on the variable clock of interactive play, say), and, with no plan,
 * every PLAN_TICKS while at rest. */
#pragma once

#include <string>
#include <vector>
#include "policy.h"

namespace simbot {

class SimBot : public bot::Policy {
public:
    unsigned act(const bot::BotView &view, const World &world) override;
    std::string note() const override { return note_; }
    std::vector<bot::Thought> thoughts() const override;

private:
    /* What must match before each press: the clock, and the player's cell,
     * height, facing and move under way. */
    struct Expect {
        double now;
        int    u, v, h, facing, moveDir;
        bool operator==(const Expect &o) const;
    };
    static Expect expectOf(const World &w);
    void plan(const World &world);

    std::vector<unsigned> presses_;
    std::vector<Expect>   expect_;
    size_t                at_ = 0;
    unsigned long         ticks_ = 0, lastTry_ = 0;
    bool                  tried_ = false;
    int                   plans_ = 0;
    std::string           note_, planned_;
    double                planMs_ = 0;
};

}  // namespace simbot
