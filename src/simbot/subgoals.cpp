#include "subgoals.h"
#include <map>
#include <set>
#include "survey.h"

namespace simbot {

namespace {

/* What an estimate counts a leg there is no way for. */
const double NO_WAY_MS = 600000.0;
/* The estimate weighed so many times over: only the first step is acted
 * on, so a good plan found soon beats the best one. */
const double ESTIMATE_WEIGHT = 1.5;
/* What an estimate counts fetching a glider, before it is planned. */
const double GLIDER_ESTIMATE_MS = 2000;
/* The search for kills, at most. */
const int FOES_NODES = 1000;

/* The level done: the rest of the quota (nearest gem first) and the exit,
 * timed on the MoveGraph. */
double estimate(const Survey &survey, const Facts &f)
{
    if (f.done)
        return 0;
    if (f.at < 0)
        return NO_WAY_MS;
    // Regions (MoveGraph::region) are left for good: the most gems to be
    // had on a way through them that ends at the exit, from each region
    // with gems (sinks first, so each one's are known before it).
    const MoveGraph &g = survey.graph();
    const std::vector<Cell> &gems = survey.gems();
    const Cell exit = survey.exit();
    const int ANY = 255;   // glider charges: as many as it likes
    const int needed = survey.quota() - f.gems;
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
        for (size_t i = 0; i < survey.gliders().size(); i++)
            left += !f.gliders.has(i);
        if (glides - f.glides > left)
            return ms + NO_WAY_MS;
        ms += (glides - f.glides) * GLIDER_ESTIMATE_MS;
    }
    return ms;
}

}  // namespace

std::vector<SubGoal> subGoals(const Survey &survey)
{
    // A tie goes to the state with more gems.
    auto moreGems = [](const Facts &f) { return -f.gems; };
    SubGoal foes;
    foes.left     = [](const Facts &f) { return __builtin_popcountll(f.alive); };
    foes.tie      = moreGems;
    foes.maxNodes = FOES_NODES;
    SubGoal done;
    done.left     = [](const Facts &f) { return f.done ? 0 : 1; };
    done.estimate = [&survey](const Facts &f) { return ESTIMATE_WEIGHT * estimate(survey, f); };
    done.tie      = moreGems;
    return {foes, done};
}

}  // namespace simbot
