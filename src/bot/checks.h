/* The checks: the bot's models measured against the game as a run goes,
 * each on when its log is named.
 *
 * KAROO_BOT_ACTIONS=<file> logs every move the player makes, rest to rest or
 * step to step, with the keys that started it, where it went and how long it
 * took, one JSON object a line (tools/bot/calibrate.py).
 *
 * KAROO_BOT_FOE_LOG=<file> checks the foe model (foemodel.h): every tenth of a
 * second it runs the sim from the views 0.1, 0.5, 1 and 2 s ago, with the
 * player where it really went since, and logs each foe's predicted cell beside
 * its real one (tools/bot/foecheck.py).
 *
 * KAROO_BOT_BOMB_LOG=<file> checks the bomb model (bombmodel.h): each bomb's
 * predicted blast cell, made when it is first seen, beside the cell it is on
 * when its fuse runs out (tools/bot/bombcheck.py). */
#pragma once

#include <deque>
#include <fstream>
#include <map>
#include <tuple>
#include "bombmodel.h"
#include "botview.h"
#include "foemodel.h"

namespace bot {

class Checks {
public:
    /* Opens the logs the environment names. */
    Checks();

    /* Each tick the policy acts on: the view, the run's frame and the presses
     * held since the last such tick. */
    void observe(const BotView &view, unsigned long frame, unsigned held);

private:
    void logAction(const BotView &view);
    void checkFoes(const BotView &view);
    void checkBombs(const BotView &view);
    void writeAction(const BotView &view, const PlayerView &to);

    unsigned long frame_ = 0;
    unsigned      held_ = 0;
    BotView       last_;
    bool          haveLast_ = false;

    std::ofstream actions_;  // KAROO_BOT_ACTIONS: the action log
    unsigned      startKeys_ = 0;
    bool          started_ = false;
    PlayerView    from_;
    double        fromMs_ = 0, moveMs_ = 0;
    unsigned      fromKind_ = 0;

    std::ofstream foeLog_;   // KAROO_BOT_FOE_LOG: the foe model against the game
    FoeModel      sim_;
    std::deque<std::pair<double, Step>>           track_;  // the player's cell by clock
    std::deque<std::pair<unsigned long, BotView>> past_;   // the views the check runs from

    std::ofstream bombLog_;  // KAROO_BOT_BOMB_LOG: the bomb model against the game
    std::map<double, std::tuple<Blast, bool, double>> bombs_;  // by drop time
};

}  // namespace bot
