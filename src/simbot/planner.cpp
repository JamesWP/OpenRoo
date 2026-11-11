#include "planner.h"
#include <stdio.h>
#include <map>
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
/* What an estimate counts killing a foe, and fetching bombs, before it is
 * planned. */
const double KILL_ESTIMATE_MS   = 5000;
const double PICKUP_ESTIMATE_MS = 5000;
const double GLIDER_ESTIMATE_MS = 2000;

}  // namespace

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

Planner::Planner(const Survey &survey, const Facts &start, const Ruled &out,
                 const Requires &needs, int maxNodes)
    : survey_(survey), out_(out), requires_(needs), maxNodes_(maxNodes)
{
    Node root;
    root.facts = start;
    nodes_.push_back(root);
    seen_[start] = 0;
    open_.push({ESTIMATE_WEIGHT * estimate(start), -start.gems, 0});
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
    const int out = g.on(survey_.exit()).empty() ? -1 : g.region(g.on(survey_.exit()).front());
    if (!g.regionReaches(here, out))
        return NO_WAY_MS;
    std::map<int, int> count;   // gems left, by region
    // A gem's region: that of the first place on its cell (-1: none).
    auto regionOf = [&](Cell c) { return g.on(c).empty() ? -1 : g.region(g.on(c).front()); };
    for (size_t i = 0; i < gems.size(); i++)
        if (!f.taken.has(i) && regionOf(gems[i]) >= 0)
            count[regionOf(gems[i])]++;
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
        if (g.regionReaches(r, out))
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
            const double t = g.timeTo(at, gems[i], ANY, &spent);
            if (t < 0 || g.on(gems[i]).empty())
                continue;
            const int p = g.on(gems[i]).front();
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
    // The foes: the risk on the walk to come, or killing them first,
    // whichever is less.
    const double risk = foeRisk(f, survey_) * ms;
    if (risk > 0) {
        const int alive = __builtin_popcountll(f.alive);
        const double remove = alive * KILL_ESTIMATE_MS + (f.bombs >= alive ? 0 : PICKUP_ESTIMATE_MS);
        ms += risk < remove ? risk : remove;
    }
    return ms;
}

bool Planner::run(Work &work)
{
    while (!finished_ && !work.out()) {
        if (open_.empty() || plan_.nodes >= maxNodes_) {
            finished_ = true;
            break;
        }
        const int at = std::get<2>(open_.top());
        open_.pop();
        if (seen_[nodes_[at].facts] < nodes_[at].ms)
            continue;   // come to sooner since
        if (nodes_[at].facts.done) {
            plan_.found = true;
            plan_.ms    = nodes_[at].ms;
            std::vector<Step> back;
            for (int n = at; n > 0; n = nodes_[n].parent)
                back.push_back(nodes_[n].step);
            plan_.steps.assign(back.rbegin(), back.rend());
            finished_ = true;
            break;
        }
        plan_.nodes++;
        work.spend(PLAN_NODE_WORK);
        std::vector<Step> next;
        for (const Action *a : actions())
            a->offer(nodes_[at].facts, survey_, next);
        for (Step &s : next) {
            if (out_.count({s.action, s.arg}) && at == 0)
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
            const double f = n.ms + ESTIMATE_WEIGHT * estimate(n.facts);
            const int gems = n.facts.gems;
            nodes_.push_back(std::move(n));
            open_.push({f, -gems, (int)nodes_.size() - 1});
        }
    }
    return finished_;
}

}  // namespace simbot
