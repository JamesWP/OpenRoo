#include "board.h"
#include <stdio.h>
#include "route.h"
#include "tile.h"

namespace bot {

Board::Board(const BotView &view, const Distances &distances, const Paths &fromPlayer)
    : distances_(&distances)
{
    const std::vector<Point> &all = distances.points();
    for (size_t i = 0; i < all.size(); i++) {
        const Point &p = all[i];
        const double t = fromPlayer.cost(p.cell.u, p.cell.v);
        if (p.kind != Point::Exit) {
            const int want = p.kind == Point::Gem ? CONTENTS_CRYSTAL : CONTENTS_GRANT_09;
            if (view.cell(p.cell.u, p.cell.v).contents != want || t < 0)
                continue;
        }
        const int at = (int)points_.size();
        if (p.kind == Point::Gem)
            gems_.push_back(at);
        else if (p.kind == Point::Bombs)
            pickups_.push_back(at);
        else
            exit_ = at;
        index_.push_back((int)i);
        points_.push_back(p);
        fromPlayer_.push_back(t);
    }
    gemsNeeded_ = view.gemsRequired - view.player.gems;
    if (gemsNeeded_ < 0)
        gemsNeeded_ = 0;
    bombs_ = view.player.bombs;
    foes_  = (int)view.foes.size();
    routes_.resize(points_.size() + 1);
    routed_.assign(points_.size() + 1, false);
}

double Board::time(int a, int b) const
{
    if (a == b)
        return 0;
    if (b == PLAYER)
        return -1;  // never asked: nothing leads back to where the player stood
    if (a == PLAYER)
        return fromPlayer_[b];
    return distances_->between(index_[a], index_[b]);
}

const GemRoute &Board::gemRoute(int from) const
{
    GemRoute &r = routes_[from + 1];
    if (routed_[from + 1])
        return r;
    routed_[from + 1] = true;
    // The route optimiser's matrix: 0 the start, 1..n the gems, n + 1 the exit.
    const int n = (int)gems_.size();
    std::vector<std::vector<double>> dist(n + 2, std::vector<double>(n + 2, -1));
    auto node = [&](int i) { return i == 0 ? from : i <= n ? gems_[i - 1] : exit_; };
    for (int i = 0; i < n + 1; i++)
        for (int j = 1; j < n + 2; j++)
            dist[i][j] = time(node(i), node(j));
    double cost;
    const std::vector<int> order = planRoute(dist, gemsNeeded_, &cost);
    if (order.empty())
        return r;
    r.found = true;
    for (int i : order)
        r.gems.push_back(gems_[i - 1]);
    r.toExit = dist[order.back()][n + 1];
    r.toLast = cost - r.toExit;
    return r;
}

std::string Board::noRoute() const
{
    const bool exitReachable = fromPlayer_[exit_] >= 0;
    if (gemsNeeded_ <= 0)
        return "no route: exit unreachable";
    char buf[160];
    snprintf(buf, sizeof(buf), "no route: %d gems needed, %d reachable, exit %s", gemsNeeded_,
             (int)gems_.size(), exitReachable ? "reachable" : "unreachable");
    return buf;
}

}  // namespace bot
