#include "executive.h"
#include <stdlib.h>
#include <memory>
#include "binio.h"
#include "board.h"
#include "danger.h"
#include "goals.h"
#include "goap.h"
#include "sysdev.h"
#include "action.h"
#include "tactics.h"

namespace bot {

namespace {

/* Ticks between checks of the running script, and between looks for a way
 * out when nothing was safe. */
const unsigned long CHECK_TICKS = 15;
/* Ticks between looks at the goals while a script runs. */
const unsigned long GOAL_TICKS = 30;

/* Where the plan first heads for. */
Step goal_of(const Plan &plan)
{
    for (const PlanStep &s : plan.steps)
        if (s.heads.u >= 0)
            return s.heads;
    return {-1, -1};
}

/* Where and when the option expects its kills, as JSON. */
std::string kills_json(const Option &o)
{
    std::string out;
    for (const FoeModel::Kill &k : o.killed)
        out += (out.empty() ? "" : ",") + std::string("[") + std::to_string((long)k.ms) + "," +
               std::to_string(k.u) + "," + std::to_string(k.v) + "]";
    return "[" + out + "]";
}

/* The option's schedule, as JSON: each cell with when it is entered, on
 * the game's clock (tools/bot/schedulecheck.py). */
std::string schedule_json(const Option &o, double now)
{
    std::string out;
    for (const Timed &m : o.schedule)
        out += (out.empty() ? "" : ",") + std::string("[") + std::to_string(now + m.t) + "," +
               std::to_string(m.cell.u) + "," + std::to_string(m.cell.v) + "]";
    return "[" + out + "]";
}

/* The option's script, as JSON: each step its kind's letter (Turn, Drop,
 * Walk, wAit, StepOff) and what it takes: a facing, the clock ms, a path. */
std::string script_json(const Option &o)
{
    std::string out;
    for (const ScriptStep &k : o.script) {
        std::string step = "[\"" + std::string(1, "TDWAS"[k.kind]) + "\"";
        if (k.kind == ScriptStep::Turn)
            step += "," + std::to_string(k.facing);
        if (k.kind == ScriptStep::Wait || k.kind == ScriptStep::StepOff)
            step += "," + std::to_string(k.untilMs);
        for (const Step &c : k.path)
            step += ",[" + std::to_string(c.u) + "," + std::to_string(c.v) + "]";
        out += (out.empty() ? "" : ",") + step + "]";
    }
    return "[" + out + "]";
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

std::vector<Goal> Executive::goals(const BotView &view)
{
    Senses senses;
    senses.bombs = view.player.bombs;
    senses.foes  = (int)view.foes.size();
    if (!view.foes.empty()) {
        senses.foeMeetsMs = foeMeets(foes_, view, ahead(view), FOE_HORIZON_MS);
        senses.hunted     = catchersHunted(foes_, view);
    }
    return selectGoals(senses);
}

bool Executive::decide(const BotView &view, const std::vector<Goal> &goals, bool midway)
{
    Situation plain(view, foes_);
    worlds_.update(view);
    const Board board(view, worlds_);

    // A tally per action, then approach's and evade's.
    const size_t APPROACH = actions().size(), EVADE = APPROACH + 1;
    std::vector<Tally> tally(EVADE + 1);
    for (Tally &t : tally)
        t.all = log_.is_open();
    Plan first;            // the plan for the goal tried first
    std::string planned;
    Option chosen;
    GoalKind goal = FINISH;
    bool have = false;
    Plan finish;
    auto plan = [&](Situation &s) {
        for (const Goal &goal_wanted : goals) {
            const GoalKind g = goal_wanted.kind;
            if (midway && g == goal_)
                break;  // only a goal above the one under way breaks it off
            PlanOptions options;
            options.goal    = g;
            options.startBy = goal_wanted.by;
            options.mu      = mu_;
            Choice c;
            have = choose(s, board, options, &c, tally);
            if (g == goals.front().kind)
                first = c.first;
            if (g == FINISH)
                finish = c.first;
            if (have) {
                planned = c.taken.describe();
                chosen  = std::move(c.option);
                goal    = g;
                break;
            }
        }
    };
    plan(plain);
    // No safe way past a thrower near by: the ways round it.
    std::unique_ptr<Situation> around;
    if (!have && !view.foes.empty()) {
        around = std::make_unique<Situation>(view, foes_, true);
        if (around->nav.anyShut())
            plan(*around);
    }
    Situation &s = have && around ? *around : plain;
    if (midway && !have)
        return false;  // the script under way carries on

    if (!midway) {
        decided_ = true;
        stuck_   = false;
        gems_    = view.player.gems;
        bombs_   = view.player.bombs;
        terrain_ = navHash(view);
    }
    if (!have && !finish.found) {
        note_  = board.noRoute();
        stuck_ = true;
        chosen = Option();
        // Stuck among foes: keep out of their way, while what they do (a gem
        // a carrier leaves, a bridge switched) may open a way.
        if (!view.foes.empty() && evade(s, board, &chosen, &tally[EVADE]))
            note_ += "; " + chosen.what;
    } else {
        // Nothing safe: keep safe, as near the way as may be.
        const Step to = goal_of(finish);
        if (!have && to.u >= 0)
            have = approach(s, board, to, &chosen, &tally[APPROACH]);
        if (!have)
            have = evade(s, board, &chosen, &tally[EVADE]);
        if (!have) {
            note_  = "no safe option";
            chosen = Option();
        }
    }
    chosen_   = std::move(chosen);
    chosenAt_ = view.now;
    goal_     = goal;
    if (have)
        note_ = chosen_.what;
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
                "\"bombs\":%d,\"foes\":%d,\"midway\":%d,\"goals\":\"",
                view.now, p.u, p.v, p.h, p.facing, p.gems, p.bombs, (int)view.foes.size(), midway);
        for (size_t i = 0; i < goals.size(); i++)
            printTo(log_, "%s%s", i ? ", " : "", goalName(goals[i].kind));
        printTo(log_, "\",\"goal\":\"%s\",\"plan\":\"%s\",\"plan_ms\":%.0f,\"nodes\":%d,\"taken_from\":\"%s\"",
                goalName(goal_), first.describe().c_str(), first.ms, first.nodes, planned.c_str());
        for (size_t i = 0; i < tally.size(); i++)
            printTo(log_, ",\"%s\":[%d,%d]",
                    i == APPROACH ? "approach" : i == EVADE ? "evade" : actions()[i]->name(),
                    tally[i].offered, tally[i].safe);
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
        if (finish.found && goals.front().kind != FINISH)
            printTo(log_, ",\"finish_plan\":\"%s\"", finish.describe().c_str());
        printTo(log_, ",\"cut_off\":[%s],\"cut_off_pickups\":[%s],\"exit_cut\":%d", cut.c_str(),
                cutPickups.c_str(), board.time(board.everything(), board.start(), board.exit()) < 0);
        printTo(log_, ",\"predicted_kills\":%s", kills_json(chosen_).c_str());
        printTo(log_, ",\"schedule\":%s", schedule_json(chosen_, view.now).c_str());
        printTo(log_, ",\"script\":%s", script_json(chosen_).c_str());
        printTo(log_, ",\"chose\":\"%s\"}\n", quoted(note_).c_str());
    }
    return true;
}

bool Executive::stillSafe(const BotView &view)
{
    if (exec_.status() != Executor::Running || chosen_.schedule.empty())
        return true;
    return !checkPlan(foes_, view, ahead(view), chosen_.lingerMs, chosen_.blasts).hit;
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
    // A goal above the one under way breaks it off, unless the option under
    // way is not to be broken off.
    if (ticks_ % GOAL_TICKS == 0 && decided_ && !stuck_ && exec_.status() == Executor::Running &&
        view.now >= chosen_.committedUntil) {
        const std::vector<Goal> now = goals(view);
        if (now.front().kind != goal_ && decide(view, now, true))
            return exec_.act(view);
    }
    // A retreat from the player's own bomb is not stopped short: standing
    // still in its blast is worse than whatever turned the rest unsafe.
    if (ticks_ % CHECK_TICKS == 0 && view.now >= chosen_.committedUntil && !stillSafe(view))
        exec_.halt();
    if (view.player.atRest()) {
        const bool idle = exec_.status() != Executor::Running;
        // A gem or a pickup is news; a bomb the script dropped is not.  News
        // waits while the option under way is not to be broken off (a
        // retreat from the player's own bomb).
        const bool committed = !idle && view.now < chosen_.committedUntil;
        const bool changed = !committed && (view.player.gems != gems_ || view.player.bombs > bombs_);
        if (view.player.bombs < bombs_)
            bombs_ = view.player.bombs;
        const bool cornered = idle && !stuck_ && chosen_.script.empty();
        if (!decided_ || changed ||
            (idle && !stuck_ && (!cornered || ticks_ % CHECK_TICKS == 0)) ||
            (idle && stuck_ && (navHash(view) != terrain_ ||
                                (!view.foes.empty() && ticks_ % CHECK_TICKS == 0))))
            decide(view, goals(view), false);
    }
    return exec_.act(view);
}

}  // namespace bot
