#include "planner.h"
#include <stdio.h>
#include <algorithm>
#include <map>
#include "actions.h"

namespace simbot {

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
                 const Ruled &out, const Requires &needs, const std::vector<Step> &rest,
                 Polish polish)
    : survey_(survey), start_(start), subGoals_(subGoals), out_(out), requires_(needs), rest_(rest),
      polish_(std::move(polish))
{
    begin(start);
}

int Planner::tie(const Facts &f) const
{
    const SubGoal &sub = subGoals_[subGoal_];
    return sub.tie ? sub.tie(f) : 0;
}

bool Planner::met(const Facts &f) const
{
    return subGoals_.back().left(f) == 0;
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
    open_.push({0, tie(f), 0});
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
    if (polish_) {
        // Polished, then offered again in its order: taken if quicker.
        std::vector<Step> in = steps, out;
        polish_(nodes_[0].facts, in, work);
        double inMs;
        if (reoffer(nodes_[0].facts, in, out, inMs) && inMs < ms) {
            plan_.polished = ms - inMs;
            steps = std::move(out);
            ms = inMs;
        }
    }
    plan_.steps = before_;
    plan_.steps.insert(plan_.steps.end(), steps.begin(), steps.end());
    plan_.ms = beforeMs_ + ms;
    // The last plan's rest, if it comes as near each sub-goal, and no slower.
    std::vector<Step> kept;
    double keptMs;
    if (!rest_.empty() && reoffer(start_, rest_, kept, keptMs) && met(kept.back().after) &&
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

bool Planner::run(Work &work)
{
    while (!finished_ && !work.out()) {
        const SubGoal &sub = subGoals_[subGoal_];
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
        if (met(node.facts))
            continue;   // nothing after the last sub-goal is met
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
            const double f = n.ms + (sub.estimate ? sub.estimate(n.facts) : 0);
            const int t = tie(n.facts);
            nodes_.push_back(std::move(n));
            open_.push({f, t, (int)nodes_.size() - 1});
        }
    }
    return finished_;
}


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
