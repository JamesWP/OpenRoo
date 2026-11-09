#include "reach.h"
#include <algorithm>
#include <map>
#include <queue>
#include "world.h"

namespace simbot {

Reach::Reach(const World &start, const std::vector<Cell> &targets, size_t maxPlaces)
{
    start_ = start.now();
    auto targeted = [&](const Place &p) {
        for (const Cell &c : targets)
            if (c.u == p.u && c.v == p.v)
                return true;
        return false;
    };
    // The open places by clock, ties to the earlier found.
    using Entry = std::pair<double, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    std::map<Place, int> best;   // the node reaching each place first

    Node root;
    root.now   = start.now();
    root.place = placeOf(start);
    root.world = std::make_unique<World>(start);
    nodes_.push_back(std::move(root));
    best[nodes_[0].place] = 0;
    open.push({nodes_[0].now, 0});

    while (!open.empty()) {
        const int at = open.top().second;
        open.pop();
        // A way to its place found sooner since it was queued.
        if (best[nodes_[at].place] != at) {
            nodes_[at].world.reset();
            continue;
        }
        // The quickest arrival on each target cell.
        const Place &here = nodes_[at].place;
        const bool first = targeted(here) && at != 0 &&
            std::none_of(arrivals_.begin(), arrivals_.end(), [&](int a) {
                return nodes_[a].place.u == here.u && nodes_[a].place.v == here.v;
            });
        if (first)
            arrivals_.push_back(at);

        for (int m = 0; m < MOVES && nodes_.size() < maxPlaces; m++) {
            auto next = std::make_unique<World>(*nodes_[at].world);
            const Played p = play(*next, (Move)m);
            if (p.kind != Played::Moved)
                continue;
            const Place place = placeOf(*next);
            auto it = best.find(place);
            if (it != best.end() && nodes_[it->second].now <= next->now())
                continue;
            Node n;
            n.parent = at;
            n.move   = (Move)m;
            n.ticks  = p.ticks;
            n.now    = next->now();
            n.place  = place;
            n.world  = std::move(next);
            const int id = (int)nodes_.size();
            nodes_.push_back(std::move(n));
            best[place] = id;
            open.push({nodes_[id].now, id});
        }
        // Played on: kept only where asked for.
        if (at != 0 && !first)
            nodes_[at].world.reset();
    }
}

Reach::~Reach() = default;


double Reach::ms(int at) const
{
    return nodes_[at].now - start_;
}

std::vector<unsigned> Reach::presses(int at) const
{
    std::vector<int> chain;
    for (int n = at; n > 0; n = nodes_[n].parent)
        chain.push_back(n);
    std::vector<unsigned> out;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const Node &n = nodes_[*it];
        out.push_back(pressOf(n.move));
        out.insert(out.end(), n.ticks - 1, 0u);
    }
    return out;
}

}  // namespace simbot
