#include "decisionlog.h"
#include "binio.h"

namespace bot {

namespace {

/* Where and when the option expects its kills, as JSON. */
std::string killsJson(const Option &o)
{
    std::string out;
    for (const FoeModel::Kill &k : o.killed)
        out += (out.empty() ? "" : ",") + std::string("[") + std::to_string((long)k.ms) + "," +
               std::to_string(k.u) + "," + std::to_string(k.v) + "]";
    return "[" + out + "]";
}

/* The option's schedule, as JSON: each cell with when it is entered, on
 * the game's clock (tools/bot/schedulecheck.py). */
std::string scheduleJson(const Option &o, double now)
{
    std::string out;
    for (const Timed &m : o.schedule)
        out += (out.empty() ? "" : ",") + std::string("[") + std::to_string(now + m.t) + "," +
               std::to_string(m.cell.u) + "," + std::to_string(m.cell.v) + "]";
    return "[" + out + "]";
}

/* The option's script, as JSON: each step its kind's letter (Turn, Drop,
 * Walk, wAit, StepOff) and what it takes: a facing, the clock ms, a path. */
std::string scriptJson(const Option &o)
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

std::string cells(const std::vector<Step> &cs)
{
    std::string out;
    for (const Step &c : cs)
        out += (out.empty() ? "" : ",") + std::string("[") + std::to_string(c.u) + "," +
               std::to_string(c.v) + "]";
    return out;
}

}  // namespace

void logDecision(std::ostream &out, const BotView &view, const Board &board,
                 const std::vector<Goal> &goals, bool midway, const Decision &d,
                 const std::string &note)
{
    const PlayerView &p = view.player;
    printTo(out, "{\"now\":%.1f,\"cell\":[%d,%d,%d],\"facing\":%d,\"gems\":%d,"
            "\"bombs\":%d,\"foes\":%d,\"midway\":%d,\"goals\":\"",
            view.now, p.u, p.v, p.h, p.facing, p.gems, p.bombs, (int)view.foes.size(), midway);
    for (size_t i = 0; i < goals.size(); i++)
        printTo(out, "%s%s", i ? ", " : "", goalName(goals[i].kind));
    printTo(out, "\",\"goal\":\"%s\",\"plan\":\"%s\",\"plan_ms\":%.0f,\"nodes\":%d,\"taken_from\":\"%s\"",
            goalName(d.goal), d.first.describe().c_str(), d.first.ms, d.first.nodes, d.planned.c_str());
    // Where each of the plan's steps ends.
    printTo(out, ",\"plan_ends\":[");
    for (size_t i = 0; i < d.first.steps.size(); i++)
        printTo(out, "%s[%d,%d]", i ? "," : "", d.first.steps[i].end.u, d.first.steps[i].end.v);
    printTo(out, "]");
    for (size_t i = 0; i < d.tally.size(); i++)
        printTo(out, ",\"%s\":[%d,%d]", tallyName(i), d.tally[i].offered, d.tally[i].safe);
    // The gems no way leads to, as things stand and with every block spent.
    std::vector<Step> cut, cutPickups;
    for (const Step &g : board.gems(board.everything()))
        if (board.time(board.everything(), board.start(), g) < 0)
            cut.push_back(g);
    for (const Pickup &k : board.pickups())
        if (board.time(0, board.start(), k.cell) < 0)
            cutPickups.push_back(k.cell);
    if (d.finish.found && goals.front().kind != FINISH)
        printTo(out, ",\"finish_plan\":\"%s\"", d.finish.describe().c_str());
    printTo(out, ",\"cut_off\":[%s],\"cut_off_pickups\":[%s],\"exit_cut\":%d", cells(cut).c_str(),
            cells(cutPickups).c_str(), board.time(board.everything(), board.start(), board.exit()) < 0);
    printTo(out, ",\"predicted_kills\":%s", killsJson(d.option).c_str());
    printTo(out, ",\"schedule\":%s", scheduleJson(d.option, view.now).c_str());
    printTo(out, ",\"script\":%s", scriptJson(d.option).c_str());
    printTo(out, ",\"chose\":\"%s\"}\n", quoted(note).c_str());
}

void writeReach(std::ostream &out, const BotView &view, const Board &board)
{
    const PlayerView &p = view.player;
    const Paths &from = board.worlds().base().from(p.u, p.v, p.facing);
    const Paths &everything = board.worlds().get(board.everything()).from(p.u, p.v, p.facing);
    for (int v = 0; v < view.extentV; v++) {
        for (int u = 0; u < view.extentU; u++)
            printTo(out, "%c", from.cost(u, v) >= 0 ? '#' : everything.cost(u, v) >= 0 ? '+' : '.');
        printTo(out, "\n");
    }
}

}  // namespace bot
