#include "planner.h"
#include <stdio.h>
#include "survey.h"
#include "tile.h"
#include "world.h"

namespace simbot {

namespace {

/* What an estimate counts a leg there is no way for. */
const double NO_WAY_MS = 600000.0;
const double ESTIMATE_WEIGHT = 1.5;

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

Planner::Planner(const Survey &survey, const Facts &start, const Ruled &out, int maxNodes)
    : survey_(survey), out_(out), maxNodes_(maxNodes)
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
    const MoveGraph &g = survey_.graph();
    int at = f.at;
    GemSet taken = f.taken;
    double ms = 0;
    for (int need = survey_.quota() - f.gems; need > 0; need--) {
        int bestGem = -1, bestAt = -1;
        double best = 0;
        for (size_t i = 0; i < survey_.gems().size(); i++) {
            if (taken.has(i))
                continue;
            double t;
            const int p = g.arrive(at, survey_.gems()[i], &t);
            if (p >= 0 && (bestGem < 0 || t < best)) {
                bestGem = (int)i;
                bestAt  = p;
                best    = t;
            }
        }
        if (bestGem < 0)
            return ms + NO_WAY_MS;
        ms += best;
        at = bestAt;
        taken.add(bestGem);
    }
    double t;
    return g.arrive(at, survey_.exit(), &t) < 0 ? ms + NO_WAY_MS : ms + t;
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
