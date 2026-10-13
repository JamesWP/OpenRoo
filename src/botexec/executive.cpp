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
    (void)board;
    for (const PlanStep &s : plan.steps) {
        if (s.kind == COLLECT)
            return s.gems.front();
        if (s.goes)
            return {s.end.u, s.end.v};
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
    const std::string reach = sysdev::getEnv("KAROO_BOT_REACH");
    if (!reach.empty())
        reach_.open(sysdev::nativePath(reach));
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
    worlds_.update(view);
    const Board board(view, worlds_);
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
    if (reach_.is_open() && !reachDone_) {
        // Where the player can come to rest, as the model sees it, once.
        reachDone_ = true;
        const Paths &from = board.worlds().base().from(s.view.player.u, s.view.player.v,
                                                       s.view.player.facing);
        const Paths &everything = board.worlds().get(board.everything()).from(
            s.view.player.u, s.view.player.v, s.view.player.facing);
        for (int v = 0; v < view.extentV; v++) {
            for (int u = 0; u < view.extentU; u++)
                printTo(reach_, "%c", from.cost(u, v) >= 0 ? '#' : everything.cost(u, v) >= 0 ? '+' : '.');
            printTo(reach_, "\n");
        }
    }

    if (log_.is_open()) {
        const PlayerView &p = view.player;
        printTo(log_, "{\"now\":%.1f,\"cell\":[%d,%d,%d],\"facing\":%d,\"gems\":%d,"
                "\"bombs\":%d,\"foes\":%d,\"plan\":\"%s\",\"nodes\":%d,\"taken_from\":\"%s\"",
                view.now, p.u, p.v, p.h, p.facing, p.gems, p.bombs, (int)view.foes.size(),
                first.describe().c_str(), first.nodes, planned.c_str());
        static const char *const names[TALLIES] = {"collect", "fetch", "blast", "ambush", "exit",
                                                   "approach", "evade"};
        for (int i = 0; i < TALLIES; i++)
            printTo(log_, ",\"%s\":[%d,%d]", names[i], tally[i].offered, tally[i].safe);
        // The gems no way leads to, as things stand and with every block
        // spent.
        std::string cut;
        for (const Step &g : board.gems(board.everything()))
            if (board.time(board.everything(), board.start(), g) < 0)
                cut += (cut.empty() ? "" : ",") + std::string("[") + std::to_string(g.u) + "," +
                       std::to_string(g.v) + "]";
        std::string cutPickups;
        for (const Pickup &p : board.pickups())
            if (board.time(0, board.start(), p.cell) < 0)
                cutPickups += (cutPickups.empty() ? "" : ",") + std::string("[") +
                              std::to_string(p.cell.u) + "," + std::to_string(p.cell.v) + "]";
        printTo(log_, ",\"cut_off\":[%s],\"cut_off_pickups\":[%s],\"exit_cut\":%d", cut.c_str(),
                cutPickups.c_str(), board.time(board.everything(), board.start(), board.exit()) < 0);
        std::string predicted;
        for (const FoeModel::Kill &k : chosen_.killed)
            predicted += (predicted.empty() ? "" : ",") + std::string("[") +
                         std::to_string((long)k.ms) + "," + std::to_string(k.u) + "," +
                         std::to_string(k.v) + "]";
        printTo(log_, ",\"predicted_kills\":[%s]", predicted.c_str());
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
        // A gem or a pickup is news; a bomb the script dropped is not.
        const bool changed = view.player.gems != gems_ || view.player.bombs > bombs_;
        if (view.player.bombs < bombs_)
            bombs_ = view.player.bombs;
        const bool cornered = idle && !stuck_ && chosen_.script.empty();
        if (!decided_ || changed ||
            (idle && !stuck_ && (!cornered || ticks_ % CHECK_TICKS == 0)) ||
            (idle && stuck_ && navHash(view) != terrain_))
            decide(view);
    }
    return exec_.act(view);
}

}  // namespace bot
