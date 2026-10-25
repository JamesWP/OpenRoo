/* The executive (docs/BOT_DESIGN.md, section 3.7): the plan policy, the
 * agent loop.  It knows nothing of the game: it asks sensing for the goals
 * worth having (senses.h), deliberation for the option to take for them
 * (deliberate.h), hands its script to the executor, and watches it run.
 *
 * Watching: it decides again on news (Progress), when the script ends or
 * fails, or (stuck) when a way may have opened; while a script runs,
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
 * the one taken.
 *
 * Its thoughts (policy.h) are the last decision, set out: the goals, the
 * plan for the first, the plan taken, the options offered and found safe,
 * and the wall-clock time deciding took. */
#pragma once

#include <fstream>
#include <string>
#include "worlds.h"
#include "foemodel.h"
#include "deliberate.h"
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
    std::vector<Thought> thoughts() const override;

private:
    /* The goals worth planning for now, the most relevant first (goals.h),
     * the foes sensed along the script under way. */
    std::vector<Goal> goals(const BotView &view);
    /* Plans for the goals in turn and starts the first safe option found.
     * midway: a script is under way, and only a goal above it is tried; if
     * none can be done, false, and the script carries on.  From rest, with
     * no goal doable, the fallbacks (fallback.h). */
    bool decide(const BotView &view, const std::vector<Goal> &goals, bool midway);
    /* Sets out decision d, which took tookMs, as the thoughts. */
    void note(const BotView &view, const std::vector<Goal> &goals, bool midway,
              const Decision &d, double tookMs);
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
    Progress  progress_;             // what was last decided from (deliberate.h)
    unsigned long ticks_ = 0;
    std::string note_;
    std::vector<Thought> thinking_;  // the last decision, for thoughts()
    /* Wall-clock ms the decisions took: sensing the goals, then deciding
     * (board and deliberation), the last, the slowest and the total. */
    struct Timing {
        double last = 0, most = 0, total = 0;
        unsigned long count = 0;
        void add(double ms);
        std::string describe() const;
    };
    Timing    senseTime_, decideTime_;
    std::ofstream log_;
    std::ofstream reach_;
    bool      reachDone_ = false;
};

}  // namespace bot
