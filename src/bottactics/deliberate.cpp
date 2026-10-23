#include "deliberate.h"
#include <memory>
#include "action.h"
#include "danger.h"

namespace bot {

namespace {

/* Where the plan first heads for. */
Step goalOf(const Plan &plan)
{
    for (const PlanStep &s : plan.steps)
        if (s.heads.u >= 0)
            return s.heads;
    return {-1, -1};
}

/* The tallies past the actions'. */
size_t approachTally() { return actions().size(); }
size_t evadeTally() { return actions().size() + 1; }

/* What the goals' plans in one situation came to. */
struct Planned {
    bool        have = false;
    bool        detour = false;   // not the way first planned: that one was found unsafe
    Option      option;
    GoalKind    goal = FINISH;
    Plan        first, finish;
    std::string planned;
};

Planned planGoals(Situation &s, const Board &board, const std::vector<Goal> &goals,
                  const Deliberation &how, std::vector<Tally> &tally)
{
    Planned out;
    for (const Goal &wanted : goals) {
        const GoalKind g = wanted.kind;
        if (how.midway && g == how.under)
            break;  // only a goal above the one under way breaks it off
        PlanOptions options;
        options.goal    = g;
        options.startBy = wanted.by;
        options.mu      = how.mu;
        Choice c;
        bool have = choose(s, board, options, &c, tally);
        if (g == goals.front().kind)
            out.first = c.first;
        if (g == FINISH)
            out.finish = c.first;
        // Not yet: the steps before the one that does it can wait, at the
        // cell to make for.
        if (have && wanted.toward.u >= 0 && c.taken.steps.size() != 1)
            have = false;
        if (!have && wanted.toward.u >= 0 && c.first.found) {
            have = approach(s, board, wanted.toward, &c.option, &tally[approachTally()]);
            c.taken = c.first;
        }
        if (!have)
            continue;
        out.have    = true;
        out.planned = c.taken.describe();
        out.option  = std::move(c.option);
        out.goal    = g;
        out.detour  = c.first.steps.empty() || c.taken.steps.empty() ||
                      c.first.steps.front().action != c.taken.steps.front().action ||
                      c.first.steps.front().end < c.taken.steps.front().end ||
                      c.taken.steps.front().end < c.first.steps.front().end;
        break;
    }
    return out;
}

}  // namespace

const char *tallyName(size_t i)
{
    return i == approachTally() ? "approach" : i == evadeTally() ? "evade" : actions()[i]->name();
}

Decision deliberate(const BotView &view, FoeModel &foes, const Board &board,
                    const std::vector<Goal> &goals, const Deliberation &how)
{
    Decision d;
    d.tally.resize(evadeTally() + 1);
    for (Tally &t : d.tally)
        t.all = how.tallyAll;

    Situation plain(view, foes);
    Planned p = planGoals(plain, board, goals, how, d.tally);
    // No safe way past a thrower near by, or only another way than the one
    // planned: the ways round it, if they cost less.
    std::unique_ptr<Situation> around;
    bool roundIt = false;
    if ((!p.have || p.detour) && !view.foes.empty()) {
        around = std::make_unique<Situation>(view, foes, true);
        if (around->nav.anyShut()) {
            Planned r = planGoals(*around, board, goals, how, d.tally);
            roundIt = r.have && (!p.have || r.option.cost < p.option.cost);
            if (roundIt || !p.have)
                p = std::move(r);
        }
    }
    Situation &s = roundIt ? *around : plain;
    d.first   = p.first;
    d.finish  = p.finish;
    d.planned = p.planned;
    d.goal    = p.goal;
    d.have    = p.have;
    d.option  = std::move(p.option);
    if (how.midway && !d.have)
        return d;  // the script under way carries on

    if (!d.have && !d.finish.found) {
        d.note   = board.noRoute();
        d.stuck  = true;
        d.option = Option();
        // Where the foes are to give the gems: those they have left, the
        // nearest first, meanwhile; else keep out of their way, while what
        // they do (a gem a carrier leaves, a bridge switched) may open a way.
        if (gemsFromSpawners(view) && fetchGem(s, board, &d.option, &d.tally[approachTally()]))
            d.note += "; " + d.option.what;
        else if (!view.foes.empty() && evade(s, board, &d.option, &d.tally[evadeTally()]))
            d.note += "; " + d.option.what;
        return d;
    }
    // Nothing safe: keep safe, as near the way as may be.
    const Step to = goalOf(d.finish);
    if (!d.have && to.u >= 0)
        d.have = approach(s, board, to, &d.option, &d.tally[approachTally()]);
    if (!d.have)
        d.have = evade(s, board, &d.option, &d.tally[evadeTally()]);
    if (!d.have) {
        d.note   = "no safe option";
        d.option = Option();
    } else {
        d.note = d.option.what;
    }
    return d;
}

Progress Progress::of(const BotView &view)
{
    return {view.player.gems, view.player.bombs, navHash(view)};
}

bool Progress::news(const BotView &view)
{
    const bool gained = view.player.gems != gems || view.player.bombs > bombs;
    if (view.player.bombs < bombs)
        bombs = view.player.bombs;
    return gained;
}

bool Progress::mayOpen(const BotView &view, bool periodic) const
{
    return navHash(view) != terrain || (!view.foes.empty() && periodic);
}

bool stillSafe(FoeModel &foes, const BotView &view, const std::vector<Timed> &ahead,
               const Option &option)
{
    return option.schedule.empty() ||
           !checkPlan(foes, view, ahead, option.lingerMs, option.blasts).hit;
}

}  // namespace bot
