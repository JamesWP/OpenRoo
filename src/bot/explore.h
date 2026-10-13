/* The explore policy: random single moves from rest, to test the nav model
 * against the game.
 *
 * At rest it picks a move (forward, back, a turn) at random, seeded by
 * KAROO_SEED so a run repeats, presses it for one tick and waits for the
 * player to come to rest again.  It avoids a move the model says kills, nine
 * times in ten, so deaths are tested too.  With bombs it drops one now and
 * then, for the bomb check (botrun.h).  With KAROO_BOT_MODEL_LOG set it
 * logs each move: where from, the model's prediction (nav.h) and what the
 * game did, one JSON object a line (tools/bot/modelcheck.py). */
#pragma once

#include <fstream>
#include "nav.h"
#include "policy.h"

namespace bot {

class Explore : public Policy {
public:
    Explore();
    unsigned act(const BotView &view) override;
    void end(const BotView &view) override;

private:
    unsigned random();
    void     finish(const BotView &view, const char *kind, const char *cause);

    std::ofstream log_;
    unsigned seed_;
    int      restTicks_ = 0;   // consecutive ticks at rest
    bool     pending_ = false; // a move is under way
    bool     moved_ = false;   // it has started moving
    int      waited_ = 0;      // ticks since it was pressed
    const char *act_ = "";
    PlayerView from_;
    int      fromKind_ = 0;
    Outcome  predicted_;
    double   startMs_ = 0;
};

}  // namespace bot
