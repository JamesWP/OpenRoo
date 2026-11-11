#include "planner.h"
#include <stdio.h>
#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <stdlib.h>
#include "actions.h"
#include "survey.h"
#include "tile.h"
#include "world.h"

namespace simbot {

namespace {

/* What an estimate counts a leg there is no way for. */
const double NO_WAY_MS = 600000.0;
const double ESTIMATE_WEIGHT = 1.5;
/* What an estimate counts fetching a glider, before it is planned. */
const double GLIDER_ESTIMATE_MS = 2000;

/* Random edits to a plan's gem order (polish) at most, at most so many in
 * a row none quicker, and the seed. */
const int      POLISH_EDITS = 20000;
const int      POLISH_STALE = 3000;
const unsigned POLISH_SEED  = 1;
/* What a polished order must save to be taken. */
const double POLISH_MIN_MS = 1;

/* The time of a leg by MoveGraph::timeTo, for a given number of charges:
 * walking, and the quickest way (gliding as it may) with the charges it
 * spends. */
struct Leg {
    float   walk = -1, any = -1;
    uint8_t glides = 0;
    double ms(int &charges) const
    {
        if (charges > 0 && any >= 0 && glides <= charges) {
            charges -= glides;
            return any;
        }
        return walk;
    }
};

Leg legOf(const MoveGraph &g, int from, Cell to)
{
    Leg l;
    l.walk = (float)g.timeTo(from, to, 0);
    int used;
    l.any = (float)g.timeTo(from, to, 255, &used);
    l.glides = (uint8_t)used;
    return l;
}

}  // namespace

const Action &collectAction();
const Action &exitAction();

Facts factsOf(const World &w, const Survey &survey)
{
    Facts f;
    f.at   = survey.graph().find(placeOf(w));
    f.gems = w.player()->gemsCollected();
    f.done = w.completed();
    for (size_t i = 0; i < survey.gems().size(); i++) {
        const Cell c = survey.gems()[i];
        if (w.map()->tile(c.u, c.v)->contents() != CONTENTS_CRYSTAL)
            f.taken.add(i);
    }
    for (size_t i = 0; i < survey.pickups().size(); i++) {
        const Cell c = survey.pickups()[i];
        if (w.map()->tile(c.u, c.v)->contents() != CONTENTS_GRANT_09)
            f.pickups.add(i);
    }
    f.bombs = w.player()->bombsCarried();
    for (size_t i = 0; i < survey.gliders().size(); i++) {
        const Cell c = survey.gliders()[i];
        if (w.map()->tile(c.u, c.v)->contents() != CONTENTS_PARAGLIDER)
            f.gliders.add(i);
    }
    f.glides = w.player()->glides();
    for (size_t i = 0; i < survey.foes().size(); i++)
        if (!foeDead(w, survey.foes()[i]))
            f.alive |= (uint64_t)1 << i;
    return f;
}

std::string Plan::describe() const
{
    std::string out;
    for (const Step &s : steps) {
        char buf[32];
        snprintf(buf, sizeof(buf), " ~%.1fs", s.ms / 1000);
        out += (out.empty() ? "" : " -> ") + s.what + buf;
    }
    return out;
}

Planner::Planner(const Survey &survey, const Facts &start, const std::vector<SubGoal> &subGoals,
                 const Ruled &out, const Requires &needs, const std::vector<Step> &rest)
    : survey_(survey), start_(start), subGoals_(subGoals), out_(out), requires_(needs), rest_(rest)
{
    begin(start);
}

void Planner::begin(const Facts &f)
{
    nodes_.clear();
    seen_.clear();
    open_ = {};
    subGoalNodes_ = 0;
    best_ = 0;
    Node root;
    root.facts = f;
    nodes_.push_back(root);
    seen_[f] = 0;
    open_.push({0, -f.gems, 0});
}

std::vector<Step> Planner::pathTo(int at) const
{
    std::vector<Step> back;
    for (int n = at; n > 0; n = nodes_[n].parent)
        back.push_back(nodes_[n].step);
    return {back.rbegin(), back.rend()};
}

std::vector<int> Planner::nearest(const Facts &start, const std::vector<Step> &steps) const
{
    std::vector<int> least;
    for (size_t a = 0; a + 1 < subGoals_.size(); a++) {
        int l = subGoals_[a].left(start);
        for (const Step &st : steps)
            l = std::min(l, subGoals_[a].left(st.after));
        least.push_back(l);
    }
    return least;
}

void Planner::end(int at, Work &work)
{
    const bool last = subGoal_ + 1 == subGoals_.size();
    if (!last) {
        // Not met: as near as it came.
        if (at < 0)
            at = best_;
        const std::vector<Step> steps = pathTo(at);
        before_.insert(before_.end(), steps.begin(), steps.end());
        beforeMs_ += nodes_[at].ms;
        subGoal_++;
        const Facts from = nodes_[at].facts;   // begin clears the nodes
        begin(from);
        return;
    }
    if (at < 0) {
        // Not met from where the sub-goals before it end: from the start, once.
        if (!before_.empty() && !again_) {
            again_ = true;
            before_.clear();
            beforeMs_ = 0;
            begin(start_);
            return;
        }
        finished_ = true;
        return;
    }
    plan_.found = true;
    std::vector<Step> steps = pathTo(at);
    double ms = nodes_[at].ms;
    plan_.polished = polish(nodes_[0].facts, steps, ms, work);
    plan_.steps = before_;
    plan_.steps.insert(plan_.steps.end(), steps.begin(), steps.end());
    plan_.ms = beforeMs_ + ms;
    // The last plan's rest, if it comes as near each sub-goal, and no slower.
    std::vector<Step> kept;
    double keptMs;
    if (!rest_.empty() && reoffer(start_, rest_, kept, keptMs) && kept.back().after.done &&
        keptMs <= plan_.ms) {
        const std::vector<int> a = nearest(start_, kept), b = nearest(start_, plan_.steps);
        bool asNear = true;
        for (size_t i = 0; i < a.size(); i++)
            asNear = asNear && a[i] <= b[i];
        if (asNear) {
            plan_.steps = std::move(kept);
            plan_.ms = keptMs;
            plan_.polished = 0;
            plan_.kept = true;
        }
    }
    finished_ = true;
}

double Planner::estimate(const Facts &f) const
{
    if (f.done)
        return 0;
    if (f.at < 0)
        return NO_WAY_MS;
    // Regions (MoveGraph::region) are left for good: the most gems to be
    // had on a way through them that ends at the exit, from each region
    // with gems (sinks first, so each one's are known before it).
    const MoveGraph &g = survey_.graph();
    const std::vector<Cell> &gems = survey_.gems();
    const Cell exit = survey_.exit();
    const int ANY = 255;   // glider charges: as many as it likes
    const int needed = survey_.quota() - f.gems;
    const int here = g.region(f.at);
    // A cell's places may be in more than one region (a facing may lead off
    // a ledge): each of its regions, any of them.
    auto regionsOf = [&](Cell c) {
        std::set<int> rs;
        for (int p : g.on(c))
            rs.insert(g.region(p));
        return rs;
    };
    const std::set<int> outs = regionsOf(exit);
    auto reachesExit = [&](int r) {
        for (int o : outs)
            if (g.regionReaches(r, o))
                return true;
        return false;
    };
    if (!reachesExit(here))
        return NO_WAY_MS;
    std::map<int, int> count;   // gems left, by region: in each of a gem's
    for (size_t i = 0; i < gems.size(); i++)
        if (!f.taken.has(i))
            for (int r : regionsOf(gems[i]))
                count[r]++;
    std::map<int, int> most;    // by region
    auto mostFrom = [&](int r) {
        int m = 0;
        for (const auto &[d, n] : most)
            if (d != r && g.regionReaches(r, d) && n > m)
                m = n;
        auto c = count.find(r);
        return m + (c == count.end() ? 0 : c->second);
    };
    for (const auto &[r, n] : count)   // ascending: sinks first
        if (reachesExit(r))
            most[r] = mostFrom(r);
    if (mostFrom(here) < needed)
        return NO_WAY_MS;
    // A tour on the MoveGraph's times to each cell: the nearest gem in the
    // region it is in, else the nearest in a region with enough left.
    int at = f.at;
    IndexSet taken = f.taken;
    double ms = 0;
    int glides = 0;   // charges the tour spends
    for (int need = needed; need > 0; need--) {
        const int r = g.region(at);
        int bestGem = -1, bestAt = -1, bestRank = 0, bestSpent = 0;
        double best = 0;
        for (size_t i = 0; i < gems.size(); i++) {
            if (taken.has(i))
                continue;
            int spent;
            double t;
            const int p = g.arrive(at, gems[i], &t, ANY, &spent);
            if (p < 0)
                continue;
            const int ri = g.region(p);
            auto m = most.find(ri);
            const int rank = ri == r ? 2 : m != most.end() && m->second >= need ? 1 : 0;
            if (bestGem >= 0 && (rank < bestRank || (rank == bestRank && t >= best)))
                continue;
            bestGem  = (int)i;
            bestAt   = p;
            bestRank = rank;
            bestSpent = spent;
            best     = t;
        }
        if (bestGem < 0)
            break;   // stuck on the way: the tour so far
        ms += best;
        glides += bestSpent;
        at = bestAt;
        taken.add(bestGem);
    }
    int spent;
    const double last = g.timeTo(at, exit, ANY, &spent);
    if (last >= 0) {
        ms += last;
        glides += spent;
    }
    // The gliders to fetch for the charges it lacks.
    if (glides > f.glides) {
        int left = 0;
        for (size_t i = 0; i < survey_.gliders().size(); i++)
            left += !f.gliders.has(i);
        if (glides - f.glides > left)
            return ms + NO_WAY_MS;
        ms += (glides - f.glides) * GLIDER_ESTIMATE_MS;
    }
    return ms;
}

bool Planner::run(Work &work)
{
    while (!finished_ && !work.out()) {
        const SubGoal &sub = subGoals_[subGoal_];
        const bool last = subGoal_ + 1 == subGoals_.size();
        if (open_.empty() || subGoalNodes_ >= sub.maxNodes) {
            end(-1, work);
            continue;
        }
        const int at = std::get<2>(open_.top());
        open_.pop();
        const Node &node = nodes_[at];
        if (seen_[node.facts] < node.ms)
            continue;   // come to sooner since
        const int left = sub.left(node.facts);
        if (left == 0) {
            end(at, work);
            continue;
        }
        // Taken by the clock: the first with the least left is the soonest.
        if (left < sub.left(nodes_[best_].facts))
            best_ = at;
        if (node.facts.done)
            continue;   // nothing after the level is done
        subGoalNodes_++;
        plan_.nodes++;
        work.spend(PLAN_NODE_WORK);
        std::vector<Step> next;
        for (const Action *a : actions())
            a->offer(nodes_[at].facts, survey_, next);
        for (Step &s : next) {
            if (out_.count({s.action, s.arg}) && at == 0 && nodes_[0].facts == start_)
                continue;   // impossible now, from here
            auto r = requires_.find({s.action, s.arg});
            if (r != requires_.end() && (r->second & nodes_[at].facts.alive))
                continue;   // a foe it needs dead is alive
            Node n;
            n.facts  = s.after;
            n.ms     = nodes_[at].ms + s.ms;
            n.parent = at;
            n.step   = std::move(s);
            auto it = seen_.find(n.facts);
            if (it != seen_.end() && it->second <= n.ms)
                continue;
            seen_[n.facts] = n.ms;
            const double f = n.ms + (last ? ESTIMATE_WEIGHT * estimate(n.facts) : 0);
            const int gems = n.facts.gems;
            nodes_.push_back(std::move(n));
            open_.push({f, -gems, (int)nodes_.size() - 1});
        }
    }
    return finished_;
}

}  // namespace simbot

namespace simbot {

double Planner::polish(const Facts &start, std::vector<Step> &steps, double &planMs,
                      Work &work) const
{
    // The gems the plan collects first, one after another, and what comes
    // after them: the exit (its leg counted), nothing, or another step (the
    // last gem kept last, so that step goes as planned).
    size_t n = 0;
    while (n < steps.size() && steps[n].action == &collectAction())
        n++;
    const bool exit = n < steps.size() && steps[n].action == &exitAction();
    const bool keepLast = n < steps.size() && !exit;
    if (n < 3)
        return 0;
    const MoveGraph &g = survey_.graph();
    const std::vector<Cell> &gems = survey_.gems();
    // Legs between them (from the place a gem is come to: MoveGraph::arrive),
    // from the start, and to the exit; index n is the start.
    std::vector<int> gem(n);
    for (size_t i = 0; i < n; i++)
        gem[i] = steps[i].arg;
    std::vector<std::vector<Leg>> leg(n + 1, std::vector<Leg>(n + 1));
    std::vector<Leg> toExit(n);
    for (size_t i = 0; i <= n; i++) {
        const int from = i == n ? start.at : g.on(gems[gem[i]]).front();
        for (size_t j = 0; j < n; j++)
            if (j != i)
                leg[i][j] = legOf(g, from, gems[gem[j]]);
        if (exit && i < n)
            toExit[i] = legOf(g, from, survey_.exit());
    }
    work.spend(PLAN_NODE_WORK * (long)n);
    const double NONE = 1e18;
    auto tour = [&](const std::vector<int> &order) {
        int charges = start.glides;
        int at = (int)n;
        double ms = 0;
        for (int i : order) {
            const double t = leg[at][i].ms(charges);
            if (t < 0)
                return NONE;
            ms += t;
            at = i;
        }
        if (exit) {
            const double t = toExit[at].ms(charges);
            if (t < 0)
                return NONE;
            ms += t;
        }
        return ms;
    };
    std::vector<int> order(n);
    for (size_t i = 0; i < n; i++)
        order[i] = (int)i;
    const double planned = tour(order);
    double best = planned;
    // Random edits, each kept if quicker: a gem moved elsewhere, a stretch
    // turned about, two gems swapped; till POLISH_EDITS, or POLISH_STALE in
    // a row none quicker.  A fixed seed: the same plan polishes the same.
    const int m = (int)n - (keepLast ? 1 : 0);
    std::mt19937 rng(POLISH_SEED);
    std::vector<int> o;
    int stale = 0;
    for (int e = 0; e < POLISH_EDITS && stale < POLISH_STALE && m >= 2; e++) {
        int i = (int)(rng() % (unsigned)m), j = (int)(rng() % (unsigned)(m - 1));
        if (j >= i)
            j++;
        o = order;
        switch (rng() % 3) {
        case 0: {
            const int x = o[i];
            o.erase(o.begin() + i);
            o.insert(o.begin() + j, x);
            break;
        }
        case 1:
            std::reverse(o.begin() + std::min(i, j), o.begin() + std::max(i, j) + 1);
            break;
        default:
            std::swap(o[i], o[j]);
        }
        const double t = tour(o);
        if (t < best - POLISH_MIN_MS) {
            best = t;
            order.swap(o);
            stale = 0;
        } else {
            stale++;
        }
        if (e % m == m - 1)
            work.spend(PLAN_NODE_WORK);
    }
    if (best >= planned)
        return 0;
    // The steps offered again in the new order, then the rest as planned.
    std::vector<Step> in;
    for (int i : order)
        in.push_back(steps[i]);
    in.insert(in.end(), steps.begin() + n, steps.end());
    std::vector<Step> out;
    double ms;
    if (!reoffer(start, in, out, ms) || ms >= planMs)
        return 0;   // offered no more that way, or no quicker: the plan as found
    steps = std::move(out);
    planMs = ms;
    return planned - best;
}

}  // namespace simbot

namespace simbot {

bool Planner::reoffer(Facts f, const std::vector<Step> &steps, std::vector<Step> &out,
                      double &ms) const
{
    out.clear();
    ms = 0;
    for (const Step &st : steps) {
        auto r = requires_.find({st.action, st.arg});
        if (out_.count({st.action, st.arg}) ||
            (r != requires_.end() && (r->second & f.alive)))
            return false;
        std::vector<Step> offered;
        st.action->offer(f, survey_, offered, st.arg);
        auto it = std::find_if(offered.begin(), offered.end(),
                               [&](const Step &o) { return o.arg == st.arg; });
        if (it == offered.end())
            return false;
        f = it->after;
        ms += it->ms;
        out.push_back(std::move(*it));
    }
    return !out.empty();
}

}  // namespace simbot
