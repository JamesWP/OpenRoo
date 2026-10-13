/* The executive (docs/BOT_DESIGN.md, section 3.7): the plan policy.  It asks
 * the planner for a plan, the tactical layer for a safe way to do its first
 * step, hands that to the executor, and watches it run.
 *
 * Deciding, from rest: the planner's cheapest plan (goap.h) over the board
 * (board.h).  If the tactical layer (tactics.h) has no safe way to do its
 * first step, the planner is asked again with that step's action kept out of
 * the first place, until a plan's first step can be done or no plan is left.
 * Then, with nothing safe to do, the player approaches the first plan's goal
 * as far as is safe, or evades to a nearby cell.  With no plan at all (no
 * route to the quota or the exit) it waits for the terrain to change.
 *
 * Watching: it decides again when the gems or bombs change, the script ends
 * or fails, or (with nothing to do) the terrain changes; while a script runs,
 * the rest of its schedule is checked again every quarter second and the
 * script stopped if it has turned unsafe.  Its note (policy.h) is the
 * decision it last made, or why it has none.
 *
 * KAROO_BOT_MU=<ms> is the planner's weight on risk, in ms a foe-second (0).
 * Foes are also hunted: while bombs are held and the script under way would
 * meet a foe within a few seconds, every half second the tactical layer is
 * asked for an ambush that kills a foe at the centre of its blast, and one
 * found (it is safe, and the foe is there as the bomb goes off) is taken,
 * wherever the script was; waiting for the foe to be on the plan leaves only
 * ambushes that catch it on the way by.
 * KAROO_BOT_REACH=<file> writes, once, a grid of where the player can come to
 * rest by the model: # as things stand, + once every block is spent, . never.
 * KAROO_BOT_DECISIONS=<file> logs every decision, one JSON object a line: the
 * player's state, the plan, each kind of option offered and found safe, and
 * the one taken. */
#pragma once

#include <fstream>
#include <string>
#include "worlds.h"
#include "foemodel.h"
#include "option.h"
#include "policy.h"
#include "script.h"

namespace bot {

class Executive : public Policy {
public:
    Executive();
    unsigned act(const BotView &view) override;
    std::string note() const override { return note_; }

private:
    void decide(const BotView &view);
    /* Whether the script's rest, and standing at its end lingerMs, meets no
     * foe or bomb. */
    bool stillSafe(const BotView &view, double lingerMs);
    /* An ambush that takes a foe at its blast's centre, if one can be had
     * now: taken in place of what the script is doing. */
    bool hunt(const BotView &view);

    Executor  exec_;
    FoeModel  foes_;
    Worlds    worlds_;
    double    mu_ = 0;
    Option    chosen_;
    double    chosenAt_ = 0;
    bool      decided_ = false;
    bool      stuck_ = false;        // no route: wait for the terrain to change
    unsigned long long terrain_ = 0;
    int       gems_ = -1, bombs_ = -1;
    unsigned long ticks_ = 0;
    std::string note_;
    std::ofstream log_;
    std::ofstream reach_;
    bool      reachDone_ = false;
};

}  // namespace bot
