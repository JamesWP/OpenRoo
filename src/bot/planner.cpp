#include "planner.h"
#include <algorithm>
#include "danger.h"
#include "binio.h"
#include "logger.h"
#include "sysdev.h"

namespace bot {

namespace {

/* Ticks between checks of the running script, and between looks for a way
 * out when nothing was safe. */
const unsigned long CHECK_TICKS = 15;

}  // namespace

Planner::Planner()
{
    const std::string path = sysdev::getEnv("KAROO_BOT_DECISIONS");
    if (!path.empty())
        log_.open(sysdev::nativePath(path));
}

void Planner::decide(const BotView &view)
{
    decided_ = true;
    stuck_   = false;
    gems_    = view.player.gems;
    bombs_   = view.player.bombs;
    terrain_ = navHash(view);

    PlanContext ctx(view, foes_);
    std::vector<Option> options;
    proposeProgress(ctx, distances_, options);
    if (!ctx.stuck.empty()) {
        note_  = ctx.stuck;
        stuck_ = true;
        exec_.start({});
        return;
    }
    proposeAmbush(ctx, options);
    proposeBombs(ctx, options);
    proposeAdvance(ctx, options);
    proposeRefuge(ctx, options);
    std::stable_sort(options.begin(), options.end(), [](const Option &a, const Option &b) {
        return a.tier != b.tier ? a.tier < b.tier : a.cost < b.cost;
    });
    int offered[TIERS] = {}, safe[TIERS] = {};
    const Option *take = nullptr;
    for (Option &o : options) {
        offered[o.tier]++;
        if (!o.checked && checkPlan(foes_, view, o.schedule, o.lingerMs, o.blasts).hit)
            continue;
        safe[o.tier]++;
        if (!take)
            take = &o;
        if (!log_.is_open())
            break;  // the counts are only for the log
    }
    if (take) {
        chosen_   = *take;
        chosenAt_ = view.now;
        note_     = chosen_.what;
    } else {
        note_  = "no safe option";
        chosen_ = Option();
    }
    exec_.start(chosen_.script);
    if (log_.is_open()) {
        const PlayerView &p = view.player;
        std::string what;
        for (char c : note_)
            what += c == '"' ? '\'' : c;
        printTo(log_, "{\"now\":%.1f,\"cell\":[%d,%d,%d],\"facing\":%d,\"gems\":%d,"
                "\"bombs\":%d,\"foes\":%d,\"progress\":[%d,%d],\"ambush\":[%d,%d],"
                "\"fetch\":[%d,%d],\"advance\":[%d,%d],\"refuge\":[%d,%d],\"chose\":\"%s\"}\n",
                view.now, p.u, p.v, p.h, p.facing, p.gems, p.bombs, (int)view.foes.size(),
                offered[PROGRESS], safe[PROGRESS], offered[AMBUSH], safe[AMBUSH],
                offered[BOMBS], safe[BOMBS], offered[ADVANCE], safe[ADVANCE],
                offered[REFUGE], safe[REFUGE], what.c_str());
    }
}

bool Planner::stillSafe(const BotView &view)
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

unsigned Planner::act(const BotView &view)
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
