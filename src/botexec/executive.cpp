#include "executive.h"
#include <stdlib.h>
#include "binio.h"
#include "board.h"
#include "danger.h"
#include "goap.h"
#include "sysdev.h"
#include "tactics.h"

namespace bot {

namespace {

/* Ticks between checks of the running script, and between looks for a way
 * out when nothing was safe. */
const unsigned long CHECK_TICKS = 15;

/* The tallies the log keeps: one per action kind, then these. */
enum { APPROACH = ACTION_KINDS, EVADE, TALLIES };

/* Where the plan first goes: Collect's first gem, or where the first step
 * that goes anywhere ends. */
Step goal_of(const Board &board, const Plan &plan)
{
    for (const PlanStep &s : plan.steps) {
        if (s.kind == COLLECT)
            return board.point(s.gems.front()).cell;
        if (s.point != Board::PLAYER)
            return board.point(s.point).cell;
    }
    return {-1, -1};
}

std::string quoted(const std::string &s)
{
    std::string out;
    for (char c : s)
        out += c == '"' ? '\'' : c;
    return out;
}

}  // namespace

Executive::Executive()
{
    const std::string mu = sysdev::getEnv("KAROO_BOT_MU");
    if (!mu.empty())
        mu_ = atof(mu.c_str());
    const std::string path = sysdev::getEnv("KAROO_BOT_DECISIONS");
    if (!path.empty())
        log_.open(sysdev::nativePath(path));
}

void Executive::decide(const BotView &view)
{
    decided_ = true;
    stuck_   = false;
    gems_    = view.player.gems;
    bombs_   = view.player.bombs;
    terrain_ = navHash(view);

    Situation s(view, foes_);
    distances_.update(view);
    const Board board(view, distances_, s.from);
    PlanOptions options;
    options.mu = mu_;
    const Plan first = planGoal(board, options);

    Tally tally[TALLIES];
    for (Tally &t : tally)
        t.all = log_.is_open();
    std::string planned;
    bool have = false;
    if (!first.found) {
        note_  = board.noRoute();
        stuck_ = true;
        chosen_ = Option();
    } else {
        // The cheapest plan whose first step can be done safely now.
        for (Plan plan = first; plan.found && !have; plan = planGoal(board, options)) {
            const ActionKind kind = plan.steps.front().kind;
            have = realise(s, board, plan, &chosen_, &tally[kind]);
            if (have)
                planned = plan.describe();
            options.notFirst |= 1u << kind;
        }
        // Nothing safe: keep safe, as near the way as may be.
        const Step goal = goal_of(board, first);
        if (!have && goal.u >= 0)
            have = approach(s, goal, &chosen_, &tally[APPROACH]);
        if (!have)
            have = evade(s, &chosen_, &tally[EVADE]);
        if (have) {
            chosenAt_ = view.now;
            note_     = chosen_.what;
        } else {
            note_   = "no safe option";
            chosen_ = Option();
        }
    }
    exec_.start(chosen_.script);

    if (log_.is_open()) {
        const PlayerView &p = view.player;
        printTo(log_, "{\"now\":%.1f,\"cell\":[%d,%d,%d],\"facing\":%d,\"gems\":%d,"
                "\"bombs\":%d,\"foes\":%d,\"plan\":\"%s\",\"nodes\":%d,\"taken_from\":\"%s\"",
                view.now, p.u, p.v, p.h, p.facing, p.gems, p.bombs, (int)view.foes.size(),
                first.describe().c_str(), first.nodes, planned.c_str());
        static const char *const names[TALLIES] = {"collect", "fetch", "ambush", "exit",
                                                   "approach", "evade"};
        for (int i = 0; i < TALLIES; i++)
            printTo(log_, ",\"%s\":[%d,%d]", names[i], tally[i].offered, tally[i].safe);
        printTo(log_, ",\"chose\":\"%s\"}\n", quoted(note_).c_str());
    }
}

bool Executive::stillSafe(const BotView &view)
{
    if (exec_.status() != Executor::Running || chosen_.schedule.empty())
        return true;
    // The rest of the schedule, from now; the cell the player is on first.
    const double gone = view.now - chosenAt_;
    std::vector<Timed> rest{{0, {view.player.u, view.player.v}}};
    for (const Timed &m : chosen_.schedule)
        if (m.t > gone)
            rest.push_back({m.t - gone, m.cell});
    return !checkPlan(foes_, view, rest, chosen_.lingerMs, chosen_.blasts).hit;
}

unsigned Executive::act(const BotView &view)
{
    ++ticks_;
    if (ticks_ % CHECK_TICKS == 0 && !view.foes.empty() && !stillSafe(view))
        exec_.halt();
    if (view.player.atRest()) {
        const bool idle = exec_.status() != Executor::Running;
        const bool changed = view.player.gems != gems_ || view.player.bombs != bombs_;
        const bool cornered = idle && !stuck_ && chosen_.script.empty();
        if (!decided_ || changed ||
            (idle && !stuck_ && (!cornered || ticks_ % CHECK_TICKS == 0)) ||
            (idle && stuck_ && navHash(view) != terrain_))
            decide(view);
    }
    return exec_.act(view);
}

}  // namespace bot
