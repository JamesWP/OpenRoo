#include "movegraph.h"
#include <deque>
#include <memory>
#include <queue>
#include <set>
#include "schedule.h"
#include "tile.h"
#include "whatif.h"
#include "world.h"

namespace simbot {

uint64_t onceDown(const World &w)
{
    uint64_t bits = 0;
    int i = 0;
    for (const FallingTile &f : w.fallings()) {
        if (i < 64 && f.noRespawn() &&
            (f.armed() || w.map()->tile(f.cellU(), f.cellV())->objectMarker() != TILE_FALLING))
            bits |= (uint64_t)1 << i;
        i++;
    }
    return bits;
}

MoveGraph::MoveGraph(const World &start)
{
    // Each place's World, until its moves are played.
    std::deque<std::pair<int, std::unique_ptr<World>>> open;
    auto add = [&](const Place &p) {
        auto it = index_.find(p);
        if (it != index_.end())
            return -1;
        const int i = (int)places_.size();
        places_.push_back(p);
        edges_.emplace_back();
        index_[p] = i;
        byCell_[p.cell()].push_back(i);
        return i;
    };
    auto first = std::make_unique<World>(start);
    whatif::removeFoes(*first);
    open.push_back({add(placeOf(*first)), std::move(first)});
    while (!open.empty()) {
        auto [at, world] = std::move(open.front());
        open.pop_front();
        whatif::restoreFallings(*world);
        const uint64_t down0 = onceDown(*world);
        const double t0 = world->now();
        std::set<int> reached{at};
        // An edge to where next is, if that place is not reached from at yet.
        auto reach = [&](std::unique_ptr<World> next, int w, Move m) {
            const Place to = placeOf(*next);
            int i = add(to);
            const bool fresh = i >= 0;
            if (!fresh)
                i = index_[to];
            if (!reached.insert(i).second)
                return;
            const uint64_t breaks = onceDown(*next) & ~down0;
            breaking_ += breaks != 0;
            edges_[at].push_back({i, w, m, next->now() - t0, breaks});
            if (fresh)
                open.push_back({i, std::move(next)});
        };
        // waited: at's World after w waits, each till the next wait time.
        auto waited = std::make_unique<World>(*world);
        const std::vector<double> times = waitTimes(*world, places_[at].cell());
        for (int w = 0; w <= (int)times.size(); w++) {
            if (w > 0) {
                if (times[w - 1] <= waited->now())
                    continue;   // passed while the last one settled
                played_++;
                if (waitUntil(*waited, times[w - 1]).kind != Played::Moved)
                    break;
                reach(std::make_unique<World>(*waited), w, WAIT);
            }
            for (int m = 0; m < MOVES; m++) {
                auto next = std::make_unique<World>(*waited);
                const Played p = play(*next, (Move)m);
                played_++;
                if (p.kind == Played::Moved)
                    reach(std::move(next), w, (Move)m);
            }
        }
    }
}

int MoveGraph::find(const Place &p) const
{
    auto it = index_.find(p);
    if (it != index_.end())
        return it->second;
    const std::vector<int> &here = on(p.cell());
    return here.empty() ? -1 : here.front();
}

const std::vector<int> &MoveGraph::on(Cell c) const
{
    static const std::vector<int> none;
    auto it = byCell_.find(c);
    return it == byCell_.end() ? none : it->second;
}

const std::vector<double> &MoveGraph::times(int from) const
{
    auto it = times_.find(from);
    if (it != times_.end())
        return it->second;
    std::vector<double> &t = times_[from];
    t.assign(places_.size(), -1);
    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    t[from] = 0;
    open.push({0, from});
    while (!open.empty()) {
        const auto [ms, at] = open.top();
        open.pop();
        if (ms > t[at])
            continue;
        for (const Edge &e : edges_[at]) {
            const double next = ms + e.ms;
            if (t[e.to] < 0 || next < t[e.to]) {
                t[e.to] = next;
                open.push({next, e.to});
            }
        }
    }
    return t;
}

const std::vector<double> &MoveGraph::timesTo(Cell c) const
{
    auto it = timesTo_.find(c);
    if (it != timesTo_.end())
        return it->second;
    std::vector<std::vector<std::pair<int, double>>> into(places_.size());
    for (size_t i = 0; i < places_.size(); i++)
        for (const Edge &e : edges_[i])
            into[e.to].push_back({(int)i, e.ms});
    std::vector<double> &t = timesTo_[c];
    t.assign(places_.size(), -1);
    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    for (int i : on(c)) {
        t[i] = 0;
        open.push({0, i});
    }
    while (!open.empty()) {
        const auto [ms, at] = open.top();
        open.pop();
        if (ms > t[at])
            continue;
        for (const auto &[from, edgeMs] : into[at]) {
            const double next = ms + edgeMs;
            if (t[from] < 0 || next < t[from]) {
                t[from] = next;
                open.push({next, from});
            }
        }
    }
    return t;
}

std::vector<int> MoveGraph::path(int from, const std::function<bool(int)> &goal,
                                 const std::function<bool(int)> &blocked) const
{
    std::vector<double> t(places_.size(), -1);
    std::vector<int> parent(places_.size(), -1);
    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    t[from] = 0;
    open.push({0, from});
    while (!open.empty()) {
        const auto [ms, at] = open.top();
        open.pop();
        if (ms > t[at])
            continue;
        if (goal(at)) {
            std::vector<int> out;
            for (int p = at; p >= 0; p = parent[p])
                out.push_back(p);
            return {out.rbegin(), out.rend()};
        }
        for (const Edge &e : edges_[at]) {
            const double next = ms + e.ms;
            if (blocked(e.to) || (t[e.to] >= 0 && next >= t[e.to]))
                continue;
            t[e.to] = next;
            parent[e.to] = at;
            open.push({next, e.to});
        }
    }
    return {};
}

double MoveGraph::timeTo(int at, Cell c) const
{
    double best = -1;
    if (at < 0)
        return best;
    for (const Cell &d : {Cell{0, 0}, Cell{1, 0}, Cell{-1, 0}, Cell{0, 1}, Cell{0, -1}}) {
        const double t = timesTo({c.u + d.u, c.v + d.v})[at];
        if (t >= 0 && (best < 0 || t < best))
            best = t;
    }
    return best;
}

int MoveGraph::arrive(int from, Cell c, double *ms) const
{
    const std::vector<double> &t = times(from);
    int best = -1;
    for (int i : on(c))
        if (t[i] >= 0 && (best < 0 || t[i] < t[best]))
            best = i;
    *ms = best < 0 ? -1 : t[best];
    return best;
}

}  // namespace simbot
