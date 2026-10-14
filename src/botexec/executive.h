/* The executive (docs/BOT_DESIGN.md, section 3.7): the plan policy.  It asks
 * goal selection which goals are worth having (goals.h), the planner for a
 * plan to each, the tactical layer for a safe way to do its first step,
 * hands that to the executor, and watches it run.
 *
 * Deciding, from rest: for each goal, most relevant first, the planner's
 * cheapest plan (goap.h) over the board (board.h).  If the tactical layer
 * (tactics.h) has no safe way to do its first step, the planner is asked
 * again with that step's action kept out of the first place, until a plan's
 * first step can be done or no plan is left; then the next goal.  With
 * nothing safe to do, the player approaches the way to finishing as far as
 * is safe, or evades to a nearby cell.  With no plan to finish at all (no
 * route to the quota or the exit) it waits for the terrain to change.
 *
 * Watching: it decides again when the gems or bombs change, the script ends
 * or fails, or (with nothing to do) the terrain changes; while a script runs,
 * the rest of its schedule is checked again every quarter second and the
 * script stopped if it has turned unsafe.  Every half second the goals are
 * selected again, the foes sensed along the script; a goal above the one
 * under way is planned for, and if it can be done safely, taken in its
 * place.  An option still to see its bomb go off on the chase it was
 * predicted with is not broken off (Option::committedUntil).  Its note
 * (policy.h) is the decision it last made, or why it has none.
 *
 * KAROO_BOT_MU=<ms> is the planner's weight on risk, in ms a foe-second (0).
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
#include "goals.h"
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
    /* The goals worth planning for now, the most relevant first (goals.h),
     * the foes sensed along the script under way. */
    std::vector<Goal> goals(const BotView &view);
    /* Plans for the goals in turn and starts the first safe option found.
     * midway: a script is under way, and only a goal above it is tried; if
     * none can be done, false, and the script carries on.  From rest, with
     * no goal doable, the player approaches or evades for finishing. */
    bool decide(const BotView &view, const std::vector<Goal> &goals, bool midway);
    /* Whether the script's rest, and standing at its end, meets no foe or
     * bomb. */
    bool stillSafe(const BotView &view);
    /* The script's schedule still ahead from where the player is, or only
     * where it is if no script runs. */
    std::vector<Timed> ahead(const BotView &view) const;

    Executor  exec_;
    FoeModel  foes_;
    Worlds    worlds_;
    double    mu_ = 0;
    Option    chosen_;
    GoalKind  goal_ = FINISH;     // what chosen_ is for
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
