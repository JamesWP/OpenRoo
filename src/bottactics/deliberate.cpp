#include "deliberate.h"
#include <memory>
#include "action.h"
#include "danger.h"
#include "fallback.h"
#include "variant.h"

namespace bot {

namespace {

/* Where the fallbacks' tallies start, past the actions'. */
size_t fallbackTally(size_t i) { return actions().size() + i; }

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
        // cell to make for (tallied with the plan's first action).
        if (have && wanted.toward.u >= 0 && c.taken.steps.size() != 1)
            have = false;
        if (!have && wanted.toward.u >= 0 && c.first.found && !c.first.steps.empty()) {
            have = approach(s, board, wanted.toward, &c.option,
                            &tally[c.first.steps.front().action]);
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
    return i < actions().size() ? actions()[i]->name() : fallbacks()[i - actions().size()]->name();
}

Decision deliberate(const BotView &view, FoeModel &foes, const Board &board,
                    const std::vector<Goal> &goals, const Deliberation &how)
{
    Decision d;
    d.tally.resize(fallbackTally(fallbacks().size()));
    for (Tally &t : d.tally)
        t.all = how.tallyAll;

    // In the plain situation, then each variant wanted: the cheapest option
    // of all, and the situation it was found in.
    std::vector<std::unique_ptr<Situation>> situations;
    situations.push_back(std::make_unique<Situation>(view, foes));
    Situation *s = situations.front().get();
    Planned p = planGoals(*s, board, goals, how, d.tally);
    for (const Variant *variant : variants()) {
        if (!variant->wanted(view, p.have, p.detour))
            continue;
        std::unique_ptr<Situation> other = variant->make(view, foes);
        if (!other)
            continue;
        Planned r = planGoals(*other, board, goals, how, d.tally);
        const bool better = r.have && (!p.have || r.option.cost < p.option.cost);
        if (better)
            s = other.get();
        if (better || !p.have)
            p = std::move(r);
        situations.push_back(std::move(other));
    }
    d.first   = p.first;
    d.finish  = p.finish;
    d.planned = p.planned;
    d.goal    = p.goal;
    d.have    = p.have;
    d.option  = std::move(p.option);
    if (d.have) {
        d.note = d.option.what;
        return d;
    }
    if (how.midway)
        return d;  // the script under way carries on

    // Nothing safe: the fallbacks, in turn.  With no way to finish at all
    // the player is stuck, whatever it does meanwhile.
    d.stuck  = !d.finish.found;
    d.option = Option();
    bool kept = false;
    for (size_t i = 0; i < fallbacks().size() && !kept; i++) {
        std::vector<Option> options;
        fallbacks()[i]->offer(*s, board, d.finish, options);
        kept = pick(*s, options, &d.option, &d.tally[fallbackTally(i)]);
    }
    if (d.stuck) {
        d.note = board.noRoute();
        if (kept)
            d.note += "; " + d.option.what;
    } else if (kept) {
        d.have = true;
        d.note = d.option.what;
    } else {
        d.note = "no safe option";
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
