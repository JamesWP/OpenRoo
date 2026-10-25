#include "executive.h"
#include <stdio.h>
#include <stdlib.h>
#include <chrono>
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

double wallMs()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

std::string format(const char *fmt, double a, double b = 0, double c = 0)
{
    char buf[96];
    snprintf(buf, sizeof buf, fmt, a, b, c);
    return buf;
}

}  // namespace

void Executive::Timing::add(double ms)
{
    last = ms;
    most = ms > most ? ms : most;
    total += ms;
    count++;
}

std::string Executive::Timing::describe() const
{
    if (!count)
        return "-";
    return format("%.2f ms last, %.2f mean, %.2f most", last, total / count, most) + " (" +
           std::to_string(count) + ")";
}

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
    const double t0 = wallMs();
    std::vector<Goal> out = senseGoals(view, foes_, worlds_, ahead(view));
    senseTime_.add(wallMs() - t0);
    return out;
}

bool Executive::decide(const BotView &view, const std::vector<Goal> &goals, bool midway)
{
    const double t0 = wallMs();
    worlds_.update(view);
    const Board board(view, worlds_);
    Deliberation how;
    how.midway   = midway;
    how.under    = goal_;
    how.mu       = mu_;
    how.tallyAll = log_.is_open();
    Decision d = deliberate(view, foes_, board, goals, how);
    const double took = wallMs() - t0;
    decideTime_.add(took);
    note(view, goals, midway, d, took);
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

namespace bot {

void Executive::note(const BotView &view, const std::vector<Goal> &goals, bool midway,
                     const Decision &d, double tookMs)
{
    std::vector<Thought> t;
    std::string names;
    for (const Goal &g : goals)
        names += (names.empty() ? "" : ", ") + std::string(goalName(g.kind));
    t.push_back({"at", format("%.1f s", view.now / 1000.0) + (midway ? ", midway" : ", from rest")});
    t.push_back({"goals", names});
    t.push_back({"first plan", d.first.found ? d.first.describe() : "none found"});
    if (d.first.found)
        t.push_back({"", format("%.0f ms of play, cost %.0f, %.0f nodes", d.first.ms, d.first.cost,
                                d.first.nodes)});
    for (const PlanStep &s : d.first.steps)
        t.push_back({"", std::string("  ") + s.name +
                             format(" to %.0f,%.0f in %.0f ms", s.end.u, s.end.v, s.ms) +
                             (s.risk > 0 ? format(", risk %.2f", s.risk) : "")});
    if (d.finish.found && !goals.empty() && goals.front().kind != FINISH)
        t.push_back({"finish plan", d.finish.describe()});
    t.push_back({"taken", d.have ? std::string(goalName(d.goal)) + ": " + d.planned
                                 : midway ? "nothing above the goal under way" : "none"});
    std::string tally;
    for (size_t i = 0; i < d.tally.size(); i++)
        if (d.tally[i].offered)
            tally += (tally.empty() ? "" : ", ") + std::string(tallyName(i)) + " " +
                     std::to_string(d.tally[i].safe) + "/" + std::to_string(d.tally[i].offered);
    t.push_back({"safe/offered", tally.empty() ? "-" : tally});
    if (!d.note.empty())
        t.push_back({"note", d.note});
    t.push_back({"took", format("%.2f ms", tookMs)});
    thinking_ = std::move(t);
}

std::vector<Thought> Executive::thoughts() const
{
    std::vector<Thought> t = thinking_;
    t.push_back({"running", exec_.status() == Executor::Running ? note_ : "idle"});
    if (stuck_)
        t.push_back({"stuck", "waiting for the terrain to change"});
    t.push_back({"sense time", senseTime_.describe()});
    t.push_back({"decide time", decideTime_.describe()});
    return t;
}

}  // namespace bot
