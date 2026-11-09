#include "planner.h"
#include <map>
#include <queue>
#include "tile.h"
#include "world.h"

namespace simbot {

Planning::Planning(const World &start)
{
    const LevelMap *map = start.map();
    for (int v = 0; v < map->extentV(); v++)
        for (int u = 0; u < map->extentU(); u++)
            if (map->tile(u, v)->contents() == CONTENTS_CRYSTAL)
                gems_.push_back({u, v});
    exit_ = {start.player()->markerCellU(), start.player()->markerCellV()};
    const Place p = placeOf(start);
    start_ = {p.u, p.v};
    samples_[{p.u, p.v}] = std::make_unique<World>(start);
    const Reach r(start, targets());
    for (int a : r.arrivals())
        samples_[{r.place(a).u, r.place(a).v}] = std::make_unique<World>(r.world(a));
}

std::vector<Cell> Planning::targets() const
{
    std::vector<Cell> t = gems_;
    t.push_back(exit_);
    return t;
}

double Planning::legMs(Cell from, Cell to)
{
    const Key f{from.u, from.v}, t{to.u, to.v};
    if (f == t)
        return 0;
    auto it = legs_.find(f);
    if (it == legs_.end()) {
        std::map<Key, double> &legs = legs_[f];
        auto sample = samples_.find(f);
        if (sample != samples_.end()) {
            const Reach r(*sample->second, targets());
            for (int a : r.arrivals())
                legs[{r.place(a).u, r.place(a).v}] = r.ms(a);
        }
        it = legs_.find(f);
    }
    auto leg = it->second.find(t);
    return leg == it->second.end() ? -1 : leg->second;
}

Planning::~Planning() = default;

void Planning::begin(const World &w)
{
    from_ = &w;
    reach_.reset();
}

const Reach &Planning::reach()
{
    if (!reach_)
        reach_ = std::make_unique<Reach>(*from_, targets());
    return *reach_;
}

Facts factsOf(const World &w, const Planning &planning)
{
    Facts f;
    f.at = placeOf(w);
    for (size_t i = 0; i < planning.gems().size() && i < 64; i++) {
        const Cell c = planning.gems()[i];
        if (w.map()->tile(c.u, c.v)->contents() != CONTENTS_CRYSTAL)
            f.taken |= (uint64_t)1 << i;
    }
    return f;
}

std::string Plan::describe() const
{
    std::string out;
    for (const PlanStep &s : steps)
        out += (out.empty() ? "" : ", ") + s.what;
    return out;
}

namespace {

struct State {
    std::unique_ptr<World> world;
    Facts  facts;
    double ms = 0;      // from the start
    int    parent = -1;
    const char *action = "";
    std::string what;
    std::vector<unsigned> presses;   // from the parent
};

}  // namespace

Plan makePlan(const World &start, const std::vector<const Action *> &actions, const Goal &goal,
              int maxStates)
{
    Plan plan;
    Planning planning(start);
    std::vector<State> states;
    State root;
    root.world = std::make_unique<World>(start);
    root.facts = factsOf(start, planning);
    states.push_back(std::move(root));

    // By f = ms + estimate, ties to the state made first.
    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    std::map<Facts, double> seen;   // the least ms each facts were come to in
    open.push({goal.estimate(start, planning), 0});
    seen[states[0].facts] = 0;

    int found = -1;
    while (!open.empty() && plan.expanded < maxStates) {
        const int at = open.top().second;
        open.pop();
        if (seen[states[at].facts] < states[at].ms)
            continue;   // come to sooner since
        const World &from = *states[at].world;   // states grows below; the World stays put
        if (goal.reached(from)) {
            found = at;
            break;
        }
        plan.expanded++;
        planning.begin(from);
        std::vector<Action::Successor> next;
        for (const Action *a : actions) {
            const size_t before = next.size();
            a->expand(from, planning, next);
            for (size_t i = before; i < next.size(); i++) {
                Action::Successor &n = next[i];
                State t;
                t.facts   = factsOf(*n.world, planning);
                t.ms      = n.world->now() - start.now();
                t.parent  = at;
                t.action  = a->name();
                t.what    = std::move(n.what);
                t.presses = std::move(n.presses);
                auto it = seen.find(t.facts);
                if (it != seen.end() && it->second <= t.ms)
                    continue;
                seen[t.facts] = t.ms;
                const double f = t.ms + goal.estimate(*n.world, planning);
                t.world = std::move(n.world);
                states.push_back(std::move(t));
                open.push({f, (int)states.size() - 1});
            }
        }
        // Its successors are made: only they need a World now.
        if (at != 0)
            states[at].world.reset();
    }
    if (found < 0)
        return plan;

    plan.found = true;
    plan.ms    = states[found].ms;
    std::vector<int> chain;
    for (int n = found; n > 0; n = states[n].parent)
        chain.push_back(n);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const State &s = states[*it];
        plan.steps.push_back({s.action, s.what, s.ms - states[s.parent].ms});
        plan.presses.insert(plan.presses.end(), s.presses.begin(), s.presses.end());
    }
    return plan;
}

}  // namespace simbot
