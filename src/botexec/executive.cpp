#include "executive.h"
#include <stdlib.h>
#include "board.h"
#include "decisionlog.h"
#include "deliberate.h"
#include "senses.h"
#include "sysdev.h"

namespace bot {

namespace {

/* Ticks between checks of the running script, and between looks for a way
 * out when nothing was safe. */
const unsigned long CHECK_TICKS = 15;
/* Ticks between looks at the goals while a script runs. */
const unsigned long GOAL_TICKS = 30;

}  // namespace

Executive::Executive()
{
    const std::string mu = sysdev::getEnv("KAROO_BOT_MU");
    if (!mu.empty())
        mu_ = atof(mu.c_str());
    const std::string reach = sysdev::getEnv("KAROO_BOT_REACH");
    if (!reach.empty())
        reach_.open(sysdev::nativePath(reach));
    const std::string path = sysdev::getEnv("KAROO_BOT_DECISIONS");
    if (!path.empty())
        log_.open(sysdev::nativePath(path));
}

std::vector<Goal> Executive::goals(const BotView &view)
{
    return senseGoals(view, foes_, worlds_, ahead(view));
}

bool Executive::decide(const BotView &view, const std::vector<Goal> &goals, bool midway)
{
    worlds_.update(view);
    const Board board(view, worlds_);
    Deliberation how;
    how.midway   = midway;
    how.under    = goal_;
    how.mu       = mu_;
    how.tallyAll = log_.is_open();
    Decision d = deliberate(view, foes_, board, goals, how);
    if (midway && !d.have)
        return false;  // the script under way carries on

    if (!midway) {
        decided_  = true;
        progress_ = Progress::of(view);
    }
    stuck_    = d.stuck;
    note_     = d.note;
    goal_     = d.goal;
    chosenAt_ = view.now;
    if (reach_.is_open() && !reachDone_) {
        reachDone_ = true;
        writeReach(reach_, view, board);
    }
    if (log_.is_open())
        logDecision(log_, view, board, goals, midway, d, note_);
    chosen_ = std::move(d.option);
    exec_.start(chosen_.script);
    return true;
}

std::vector<Timed> Executive::ahead(const BotView &view) const
{
    const Step here{view.player.u, view.player.v};
    if (exec_.status() != Executor::Running)
        return {{0, here}};
    return chosen_.ahead(view.now - chosenAt_, here);
}

unsigned Executive::act(const BotView &view)
{
    ++ticks_;
    const bool running = exec_.status() == Executor::Running;
    const bool committed = view.now < chosen_.committedUntil;
    // A goal above the one under way breaks it off, unless the option under
    // way is committed.
    if (ticks_ % GOAL_TICKS == 0 && decided_ && !stuck_ && running && !committed) {
        const std::vector<Goal> now = goals(view);
        if (now.front().kind != goal_ && decide(view, now, true))
            return exec_.act(view);
    }
    // A committed option is not stopped short (a retreat from the player's
    // own bomb: standing still in its blast is worse).
    if (ticks_ % CHECK_TICKS == 0 && !committed && running &&
        !stillSafe(foes_, view, ahead(view), chosen_))
        exec_.halt();
    if (view.player.atRest()) {
        const bool idle = exec_.status() != Executor::Running;
        // News waits while the option under way is committed.
        const bool news = progress_.news(view) && (idle || !committed);
        const bool periodic = ticks_ % CHECK_TICKS == 0;
        const bool cornered = idle && !stuck_ && chosen_.script.empty();
        if (!decided_ || news || (idle && !stuck_ && (!cornered || periodic)) ||
            (idle && stuck_ && progress_.mayOpen(view, periodic)))
            decide(view, goals(view), false);
    }
    return exec_.act(view);
}

}  // namespace bot
