#include "board.h"
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include "bombmodel.h"
#include "route.h"
#include "nav.h"
#include "tile.h"

namespace bot {

namespace {

int chebyshev(int du, int dv)
{
    du = abs(du);
    dv = abs(dv);
    return du > dv ? du : dv;
}

}  // namespace

Board::Board(const BotView &live, Worlds &worlds) : live_(live), worlds_(&worlds)
{
    gemsNeeded_ = live.gemsRequired - live.player.gems;
    if (gemsNeeded_ < 0)
        gemsNeeded_ = 0;
    for (const FoeView &f : live.foes)
        carriers_ += carrier(f);
    for (int v = 0; v < live.extentV; v++)
        for (int u = 0; u < live.extentU; u++)
            if (live.cell(u, v).contents == CONTENTS_GRANT_09)
                pickups_.push_back({{u, v}, -1});
    for (size_t i = 0; i < worlds.blockCount(); i++) {
        const Step b = worlds.block(i);
        const int hidden = live.cell(b.u, b.v).hidden;
        if (hidden == CONTENTS_GRANT_09)
            pickups_.push_back({b, (int)i});
    }
    for (int v = 0; v < live.extentV; v++)
        for (int u = 0; u < live.extentU; u++) {
            const int i = switched({u, v});
            if (i >= 0)
                switches_.push_back({{u, v}, i});
        }
}

bool Board::carrier(const FoeView &foe)
{
    return foe.drops == CONTENTS_CRYSTAL && !foe.dying;
}

Changes Board::allBlocks() const
{
    return ((Changes)1 << worlds_->blockCount()) - 1;
}

Changes Board::everything() const
{
    Changes c = allBlocks() | OPEN;
    for (size_t i = 0; i < worlds_->bridgeCount(); i++)
        if (!live_.bridges[i].extended)
            c |= worlds_->bridgeBit(i);
    return c;
}

int Board::switched(Step cell) const
{
    const BridgeView *b = switchedBy(live_, cell.u, cell.v);
    if (!b)
        return -1;
    const int i = (int)(b - live_.bridges.data());
    return i < (int)worlds_->bridgeCount() ? i : -1;
}

const std::vector<Step> &Board::gems(Changes s) const
{
    auto it = gems_.find(s);
    if (it != gems_.end())
        return it->second;
    std::vector<Step> out;
    for (int v = 0; v < live_.extentV; v++)
        for (int u = 0; u < live_.extentU; u++)
            if (live_.cell(u, v).contents == CONTENTS_CRYSTAL)
                out.push_back({u, v});
    for (size_t i = 0; i < worlds_->blockCount(); i++) {
        const Step b = worlds_->block(i);
        if ((s & ((Changes)1 << i)) && live_.cell(b.u, b.v).hidden == CONTENTS_CRYSTAL)
            out.push_back(b);
    }
    return gems_[s] = out;
}

double Board::time(Changes s, Place from, Step to) const
{
    return worlds_->get(s).from(from.u, from.v, facingOf(from)).cost(to.u, to.v);
}

double Board::turnMs(Place from, int facing) const
{
    const int turns = (facing - facingOf(from) + 4) % 4;
    return (turns == 3 ? 1 : turns) * live_.player.stepMs;
}

Changes Board::inTheWay(Changes s, Place from, bool pickups) const
{
    from.facing = facingOf(from);
    const auto key = std::make_tuple(s, from, pickups);
    auto it = way_.find(key);
    if (it != way_.end())
        return it->second;
    const Terrain &w = worlds_->get(everything());
    Changes mask = 0;
    // The unspent blocks and the bridges not extended (of s) on the cells a
    // path crosses.
    auto cross = [&](const std::vector<Step> &cells) {
        for (const Step &c : cells) {
            for (size_t i = 0; i < worlds_->blockCount(); i++) {
                const Step b = worlds_->block(i);
                if (b.u == c.u && b.v == c.v && !(s >> i & 1))
                    mask |= (Changes)1 << i;
            }
            for (size_t i = 0; i < worlds_->bridgeCount(); i++)
                if (!extended(s, i))
                    for (const Step &d : live_.bridges[i].deck)
                        if (d.u == c.u && d.v == c.v)
                            mask |= worlds_->bridgeBit(i);
        }
    };
    Place at = from;
    auto go = [&](Step to) {
        cross(w.from(at.u, at.v, at.facing).cellsTo(to.u, to.v));
        at = {to.u, to.v, worlds_->pointFacing()};
    };
    if (gemsNeeded_ > 0) {
        const GemRoute &r = gemRoute(everything(), from);
        for (const Step &g : r.gems)
            go(g);
    }
    go(exit());
    if (pickups)
        for (const Pickup &p : pickups_)
            cross(w.from(from.u, from.v, from.facing).cellsTo(p.cell.u, p.cell.v));
    return way_[key] = mask;
}

int Board::reach(Changes s, Step from) const
{
    const auto key = std::make_tuple(s, from.u, from.v);
    auto it = reach_.find(key);
    if (it != reach_.end())
        return it->second;
    const Paths &p = worlds_->get(s).from(from.u, from.v, worlds_->pointFacing());
    int n = 0;
    for (int v = 0; v < live_.extentV; v++)
        for (int u = 0; u < live_.extentU; u++)
            n += p.cost(u, v) >= 0;
    return reach_[key] = n;
}

GemRoute Board::routeFrom(Changes s, Place from, const Step *taken, int need) const
{
    GemRoute r;
    const Terrain &w = worlds_->get(s);
    const Paths &start = w.from(from.u, from.v, from.facing);
    // Only the gems that can be reached at all are in the route.
    std::vector<Step> all;
    for (const Step &g : gems(s))
        if (start.cost(g.u, g.v) >= 0 && !(taken && g.u == taken->u && g.v == taken->v))
            all.push_back(g);
    const int n = (int)all.size();
    // The route optimiser's matrix: 0 the start, 1..n the gems, n + 1 the exit.
    std::vector<std::vector<double>> dist(n + 2, std::vector<double>(n + 2, -1));
    const Step ex = exit();
    for (int i = 0; i < n + 1; i++) {
        const Paths &row = i == 0 ? start
                                  : w.from(all[i - 1].u, all[i - 1].v, worlds_->pointFacing());
        for (int j = 1; j < n + 1; j++)
            dist[i][j] = i == j ? 0 : row.cost(all[j - 1].u, all[j - 1].v);
        dist[i][n + 1] = row.cost(ex.u, ex.v);
    }
    double cost;
    const std::vector<int> order = planRoute(dist, need, &cost);
    if (!order.empty()) {
        r.found = true;
        for (int i : order)
            r.gems.push_back(all[i - 1]);
        r.toExit = dist[order.back()][n + 1];
        r.toLast = cost - r.toExit;
    }
    return r;
}

const GemRoute &Board::gemRoute(Changes s, Place from, int need) const
{
    from.facing = facingOf(from);
    if (need < 0)
        need = gemsNeeded_;
    const auto key = std::make_tuple(s, from, need);
    auto it = routes_.find(key);
    if (it != routes_.end())
        return it->second;
    return routes_[key] = routeFrom(s, from, nullptr, need);
}

bool Board::canFinishFrom(Step cell) const
{
    const Place at{cell.u, cell.v, worlds_->pointFacing()};
    if (fromMap() <= 0)
        return time(0, at, exit()) >= 0;
    return gemRoute(0, at, fromMap()).found;
}

bool Board::canFinishAfter(Step gem) const
{
    const auto key = std::make_pair(gem.u, gem.v);
    auto it = after_.find(key);
    if (it != after_.end())
        return it->second;
    const int need = fromMap() - 1;
    GemRoute r;
    const Place at{gem.u, gem.v, worlds_->pointFacing()};
    if (need <= 0)
        r.found = worlds_->get(0).from(at.u, at.v, at.facing).cost(exit().u, exit().v) >= 0;
    else
        r = routeFrom(0, at, &gem, need);
    return after_[key] = r.found;
}

const std::vector<Site> &Board::sites(Changes s) const
{
    auto it = sites_.find(s);
    if (it != sites_.end())
        return it->second;
    std::vector<Site> out;
    const BotView &view = worlds_->get(s).view();
    std::vector<size_t> open;   // the blocks of the world not spent yet
    for (size_t i = 0; i < worlds_->blockCount(); i++)
        if (!(s & ((Changes)1 << i)))
            open.push_back(i);
    // A bomb dropped on p rolls at most a step before it lands, and blasts the
    // 3x3 around: p can be two cells from a block.
    std::set<std::pair<int, int>> seen;
    for (const size_t i : open) {
        const Step b = worlds_->block(i);
        for (int du = -2; du <= 2; du++)
            for (int dv = -2; dv <= 2; dv++) {
                const Step p{b.u + du, b.v + dv};
                if (!view.inside(p.u, p.v) || !seen.insert({p.u, p.v}).second)
                    continue;
                const Cell &c = view.cell(p.u, p.v);
                if (c.height == 0 || c.kind == TILE_IMPASSABLE || c.kind == TILE_EMPTY ||
                    c.kind == TILE_BOMBABLE)
                    continue;
                for (int f = DIR_MIN; f <= DIR_MAX; f++) {
                    const Blast blast = predictBomb(view, p.u, p.v, c.height, f, 0);
                    if (blast.lost)
                        continue;
                    Changes hits = 0;
                    for (const size_t j : open) {
                        const Step o = worlds_->block(j);
                        if (chebyshev(o.u - blast.cell.u, o.v - blast.cell.v) <= 1 &&
                            view.cell(o.u, o.v).height == blast.h)
                            hits |= (Changes)1 << j;
                    }
                    if (hits)
                        out.push_back({{p.u, p.v, f}, hits});
                }
            }
    }
    return sites_[s] = out;
}

std::string Board::noRoute() const
{
    const Step ex = exit();
    const Place at = start();
    const bool exitReachable = time(0, at, ex) >= 0;
    if (gemsNeeded_ <= 0)
        return exitReachable ? "no route" : "no route: exit unreachable";
    int reachable = 0;
    for (const Step &g : gems(0))
        if (time(0, at, g) >= 0)
            reachable++;
    char buf[160];
    snprintf(buf, sizeof(buf), "no route: %d gems needed, %d reachable, exit %s", gemsNeeded_,
             reachable, exitReachable ? "reachable" : "unreachable");
    return buf;
}

}  // namespace bot
