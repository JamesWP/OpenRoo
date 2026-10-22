#include "board.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include "bombmodel.h"
#include "fallingmodel.h"
#include "foemodel.h"
#include "route.h"
#include "nav.h"
#include "tile.h"

namespace bot {

namespace {

/* What a time bonus adds to the time limit (player.cpp). */
const double TIME_BONUS_MS = 5000;
/* A bait's way clear of the thrower's bomb is walked this long before it
 * blows, at least. */
const double CLEAR_MARGIN_MS = 300;

/* Whether the player on at, the thrower beside it on by, gets clear of the
 * thrower's bomb before it blows: some cell it walks to by then, never
 * through the thrower, lies outside the blast. */
bool clearOfBait(const BotView &view, Step at, Step by, const Blast &blast)
{
    NavModel nav(view);
    nav.shut(by);
    const Paths away(nav, at.u, at.v, 1);
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++) {
            const double c = away.cost(u, v);
            if (c > 0 && c <= FUSE_MS - CLEAR_MARGIN_MS &&
                !blast.kills(u, v, view.cell(u, v).height, blast.from))
                return true;
        }
    return false;
}
/* How far short the plans' times can run of the real ones, a fraction: the
 * walks are timed leg by leg, so a falling tile crossed again before it is
 * back, for one, is not counted (up to 0.2 short on the levels without foes
 * or bombs).  Against the clock, the times are counted this much longer. */
const double ESTIMATE_SLACK = 0.15;

}  // namespace

Board::Board(const BotView &live, Worlds &worlds) : live_(live), worlds_(&worlds)
{
    gemsNeeded_ = live.gemsRequired - live.player.gems;
    if (gemsNeeded_ < 0)
        gemsNeeded_ = 0;
    for (const FoeView &f : live.foes)
        carriers_ += carrier(f);
    auto kind = [](int contents, Pickup::Kind *k) {
        *k = contents == CONTENTS_PARAGLIDER ? Pickup::Glider : Pickup::Bombs;
        return contents == CONTENTS_GRANT_09 || contents == CONTENTS_PARAGLIDER;
    };
    Pickup::Kind k;
    for (int v = 0; v < live.extentV; v++)
        for (int u = 0; u < live.extentU; u++)
            if (kind(live.cell(u, v).contents, &k))
                pickups_.push_back({{u, v}, -1, k});
    for (size_t i = 0; i < worlds.blockCount(); i++) {
        const Step b = worlds.block(i);
        if (kind(live.cell(b.u, b.v).hidden, &k))
            pickups_.push_back({b, (int)i, k});
    }
    for (int v = 0; v < live.extentV; v++)
        for (int u = 0; u < live.extentU; u++)
            if (live.cell(u, v).contents == CONTENTS_TIME_BONUS)
                timeBonuses_.push_back({{u, v}, -1});
    for (int v = 0; v < live.extentV; v++)
        for (int u = 0; u < live.extentU; u++)
            if (live.cell(u, v).contents == CONTENTS_FREEZE)
                freezes_.push_back({u, v});
    for (size_t i = 0; i < worlds.blockCount(); i++) {
        const Step b = worlds.block(i);
        if (live.cell(b.u, b.v).hidden == CONTENTS_TIME_BONUS)
            timeBonuses_.push_back({b, (int)i});
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

double Board::timeLeft(unsigned bonuses) const
{
    if (live_.timeLimitS <= 0)
        return -1;
    int taken = 0;
    for (size_t i = 0; i < timeBonuses_.size() && i < 32; i++)
        taken += bonuses >> i & 1;
    return (live_.timeLimitS * 1000.0 - live_.timeElapsedMs + TIME_BONUS_MS * taken) /
           (1 + ESTIMATE_SLACK);
}

bool Board::timeBonus(Step c) const
{
    return live_.cell(c.u, c.v).contents == CONTENTS_TIME_BONUS;
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
            if (p.kind == Pickup::Bombs)
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

bool Board::opens(Changes next, Step cell, Changes s, Place from) const
{
    const Paths &there = worlds_->get(next).from(cell.u, cell.v, worlds_->pointFacing());
    const Paths &here  = worlds_->get(s).from(from.u, from.v, facingOf(from));
    for (int v = 0; v < live_.extentV; v++)
        for (int u = 0; u < live_.extentU; u++)
            if (there.cost(u, v) >= 0 && here.cost(u, v) < 0)
                return true;
    return false;
}

int Board::letsIn(Changes next, Step cell, Changes s, Place from) const
{
    const Paths &there = worlds_->get(next).from(cell.u, cell.v, worlds_->pointFacing());
    const Paths &here  = worlds_->get(s).from(from.u, from.v, facingOf(from));
    int n = 0;
    for (const FoeView &f : live_.foes)
        n += f.kind == 2 && !f.dying && there.cost(f.u, f.v) >= 0 && here.cost(f.u, f.v) < 0;
    return n;
}

Taken Board::gemBit(Step gem) const
{
    const std::vector<Step> &all = gems(everything());
    for (size_t i = 0; i < all.size() && i < 64; i++)
        if (all[i].u == gem.u && all[i].v == gem.v)
            return (Taken)1 << i;
    return 0;
}

GemRoute Board::routeFrom(Changes s, Place from, Taken taken, int need, bool toExit) const
{
    GemRoute r;
    const Terrain &w = worlds_->get(s);
    const Paths &start = w.from(from.u, from.v, from.facing);
    // Only the gems that can be reached at all, and are not taken, are in the
    // route.
    std::vector<Step> all;
    for (const Step &g : gems(s))
        if (start.cost(g.u, g.v) >= 0 && !(taken & gemBit(g)))
            all.push_back(g);
    const int n = (int)all.size();
    if (need < 0)
        need = std::min(n, -1 - need);  // every one, up to that many
    if (need == 0 || need > n)
        return r;
    // The route optimiser's matrix: 0 the start, 1..n the gems, n + 1 the exit.
    std::vector<std::vector<double>> dist(n + 2, std::vector<double>(n + 2, -1));
    const Step ex = exit();
    for (int i = 0; i < n + 1; i++) {
        const Paths &row = i == 0 ? start
                                  : w.from(all[i - 1].u, all[i - 1].v, worlds_->pointFacing());
        for (int j = 1; j < n + 1; j++)
            dist[i][j] = i == j ? 0 : row.cost(all[j - 1].u, all[j - 1].v);
        dist[i][n + 1] = toExit ? row.cost(ex.u, ex.v) : 0;
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

const GemRoute &Board::gemRoute(Changes s, Place from, int need, Taken taken) const
{
    from.facing = facingOf(from);
    if (need < 0)
        need = gemsNeeded_;
    const auto key = std::make_tuple(s, from, need, taken);
    auto it = routes_.find(key);
    if (it != routes_.end())
        return it->second;
    return routes_[key] = routeFrom(s, from, taken, need, true);
}

const GemRoute &Board::timedRoute(Changes s, Place from, int need, Taken taken,
                                  unsigned bonuses, double left) const
{
    from.facing = facingOf(from);
    const auto key = std::make_tuple(s, from, need, taken, bonuses, std::lround(left));
    auto it = timed_.find(key);
    if (it != timed_.end())
        return it->second;
    GemRoute &r = timed_[key];
    const Terrain &w = worlds_->get(s);
    const Paths &start = w.from(from.u, from.v, from.facing);
    std::vector<Step> gems, bonusCells;
    std::vector<int> bonusIndex;
    for (size_t i = 0; i < timeBonuses_.size() && i < 32; i++) {
        const Step c = timeBonuses_[i].cell;
        if (!(bonuses >> i & 1) && available(timeBonuses_[i], s) && start.cost(c.u, c.v) >= 0) {
            bonusCells.push_back(c);
            bonusIndex.push_back((int)i);
        }
    }
    for (const Step &g : this->gems(s))
        if (start.cost(g.u, g.v) >= 0 && !(taken & gemBit(g)))
            gems.push_back(g);
    if ((int)(gems.size() + bonusCells.size()) > TIMED_MAX) {
        // Too many for the exact search: the gems the route without the
        // clock would take.
        const GemRoute &plain = gemRoute(s, from, need, taken);
        if (!plain.found)
            return r;
        gems = plain.gems;
    }
    const int n = (int)gems.size(), m = n + (int)bonusCells.size();
    auto at = [&](int i) { return i < n ? gems[i] : bonusCells[i - n]; };
    // 0 the start, 1..n the gems, n + 1..m the bonuses, m + 1 the exit.
    std::vector<std::vector<double>> dist(m + 2, std::vector<double>(m + 2, -1));
    const Step ex = exit();
    for (int i = 0; i < m + 1; i++) {
        const Paths &row = i == 0 ? start : w.from(at(i - 1).u, at(i - 1).v, worlds_->pointFacing());
        for (int j = 1; j < m + 1; j++)
            dist[i][j] = i == j ? 0 : row.cost(at(j - 1).u, at(j - 1).v);
        dist[i][m + 1] = row.cost(ex.u, ex.v);
    }
    double cost;
    const std::vector<int> order = planTimedRoute(dist, n, need, left,
                                                     TIME_BONUS_MS / (1 + ESTIMATE_SLACK), &cost);
    if (cost < 0)
        return r;
    r.found = true;
    int last = 0;
    for (int i : order) {
        r.gems.push_back(at(i - 1));
        if (i > n)
            r.bonuses |= 1u << bonusIndex[i - 1 - n];
        last = i;
    }
    r.toExit = dist[last][m + 1];
    r.toLast = cost - r.toExit;
    return r;
}

const GemRoute &Board::gather(Changes s, Place from, Taken taken, int most) const
{
    from.facing = facingOf(from);
    const auto key = std::make_tuple(s, from, -1 - most, taken);
    auto it = routes_.find(key);
    if (it != routes_.end())
        return it->second;
    return routes_[key] = routeFrom(s, from, taken, -1 - most, false);
}

bool Board::glidesToBe() const
{
    if (live_.player.glides > 0)
        return true;
    for (const Pickup &p : pickups_)
        if (p.kind == Pickup::Glider)
            return true;
    return false;
}

Changes Board::spendable() const
{
    if (!spendableFound_) {
        spendableFound_ = true;
        bool bombs = live_.player.bombs > 0;
        for (const Pickup &p : pickups_)
            bombs = bombs || p.kind == Pickup::Bombs;
        spendable_ = bombs || !baits(0).empty() ? allBlocks() : 0;
    }
    return spendable_;
}

bool Board::canFinishFrom(Step cell) const
{
    if (glidesToBe())
        return true;  // a glide may lead on: no cell is known to be a trap
    const Place at{cell.u, cell.v, worlds_->pointFacing()};
    if (crossable(spendable(), at, 0))
        return true;  // and so may a crossing
    for (const Changes s : {(Changes)0, spendable()}) {
        if (fromMap() <= 0 ? time(s, at, exit()) >= 0 : gemRoute(s, at, fromMap()).found)
            return true;
        if (!spendable())
            break;
    }
    return false;
}

bool Board::canFinishAfter(Step gem) const
{
    if (glidesToBe())
        return true;
    const auto key = std::make_pair(gem.u, gem.v);
    auto it = after_.find(key);
    if (it != after_.end())
        return it->second;
    const int need = fromMap() - 1;
    const Place at{gem.u, gem.v, worlds_->pointFacing()};
    bool found = crossable(spendable(), at, 0);  // a crossing may lead on
    for (const Changes s : {(Changes)0, spendable()}) {
        if (found)
            break;
        if (need <= 0)
            found = worlds_->get(s).from(at.u, at.v, at.facing).cost(exit().u, exit().v) >= 0;
        else
            found = routeFrom(s, at, gemBit(gem), need, true).found;
        if (found || !spendable())
            break;
    }
    return after_[key] = found;
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
    // 3x3 around: p can be two cells from a block.  Or something carries it
    // off (bombmodel.h).
    std::set<std::pair<int, int>> seen;
    std::vector<Step> candidates;
    for (const size_t i : open) {
        const Step b = worlds_->block(i);
        for (int du = -2; du <= 2; du++)
            for (int dv = -2; dv <= 2; dv++)
                candidates.push_back({b.u + du, b.v + dv});
    }
    if (!open.empty())
        for (const Step &p : carriedFrom(view))
            candidates.push_back(p);
    for (const Step &p : candidates) {
        if (!view.inside(p.u, p.v) || !seen.insert({p.u, p.v}).second)
            continue;
        const Cell &c = view.cell(p.u, p.v);
        if (c.height == 0 || c.kind == TILE_IMPASSABLE || c.kind == TILE_EMPTY ||
            (c.kind == TILE_BOMBABLE && !c.busy))
            continue;
        for (int f = DIR_MIN; f <= DIR_MAX; f++) {
            // Each spell of drop times its blocks (once, where carried, a
            // drop at one time spends some and at another others).
            std::set<Changes> found;
            for (const DropSpell &spell : dropSpells(view, p.u, p.v, c.height, f, view.now)) {
                Changes hits = 0;
                for (const size_t j : open)
                    if (spends(view, spell.blast, worlds_->block(j)))
                        hits |= (Changes)1 << j;
                if (hits && found.insert(hits).second)
                    out.push_back({{p.u, p.v, f}, hits});
            }
        }
    }
    return sites_[s] = out;
}

const std::vector<Site> &Board::baits(Changes s) const
{
    auto it = baits_.find(s);
    if (it != baits_.end())
        return it->second;
    std::vector<Site> &out = baits_[s];
    const BotView &view = worlds_->get(s).view();
    std::vector<size_t> open;
    for (size_t i = 0; i < worlds_->blockCount(); i++)
        if (!(s & ((Changes)1 << i)))
            open.push_back(i);
    if (open.empty())
        return out;
    // Where the throwers can go, all together.
    FoeModel sim;
    sim.load(view);
    std::vector<char> walk;
    for (size_t i = 0; i < sim.foeCount(); i++) {
        if (!sim.throwsAtPlayer(i))
            continue;
        const std::vector<char> w = sim.walkable(i);
        if (walk.empty())
            walk = w;
        for (size_t c = 0; c < w.size(); c++)
            walk[c] |= w[c];
    }
    if (walk.empty())
        return out;
    // How far, in cells, the nearest thrower walks to each cell it can go.
    std::vector<int> far(walk.size(), -1);
    std::vector<Step> ring;
    for (size_t i = 0; i < sim.foeCount(); i++)
        if (sim.throwsAtPlayer(i)) {
            const Step c = sim.foeCell(i);
            if (view.inside(c.u, c.v) && far[(size_t)c.v * view.extentU + c.u] < 0) {
                far[(size_t)c.v * view.extentU + c.u] = 0;
                ring.push_back(c);
            }
        }
    for (size_t k = 0; k < ring.size(); k++)
        for (int d = DIR_MIN; d <= DIR_MAX; d++) {
            const Step n{ring[k].u + DIR_DU[d], ring[k].v + DIR_DV[d]};
            const size_t at = (size_t)n.v * view.extentU + n.u;
            if (!view.inside(n.u, n.v) || !walk[at] || far[at] >= 0)
                continue;
            far[at] = far[(size_t)ring[k].v * view.extentU + ring[k].u] + 1;
            ring.push_back(n);
        }
    std::set<std::pair<int, int>> seen;
    for (const size_t i : open) {
        const Step b = worlds_->block(i);
        for (int dv = -1; dv <= 1; dv++)
            for (int du = -1; du <= 1; du++) {
                const Step p{b.u + du, b.v + dv};
                if (!view.inside(p.u, p.v) || !seen.insert({p.u, p.v}).second)
                    continue;
                const Cell &c = view.cell(p.u, p.v);
                if (c.height == 0 || c.kind == TILE_IMPASSABLE || c.kind == TILE_EMPTY ||
                    (c.kind == TILE_BOMBABLE && !c.busy))
                    continue;
                // The thrower beside it, facing the player on it.
                Changes hits = 0;
                int nearest = -1;
                for (int f = DIR_MIN; f <= DIR_MAX; f++) {
                    const Step n{p.u - DIR_DU[f], p.v - DIR_DV[f]};
                    if (!view.inside(n.u, n.v) || !walk[(size_t)n.v * view.extentU + n.u])
                        continue;
                    const Blast blast =
                        predictBomb(view, n.u, n.v, view.cell(n.u, n.v).height, f, view.now);
                    Changes these = 0;
                    for (const size_t j : open)
                        if (spends(view, blast, worlds_->block(j)))
                            these |= (Changes)1 << j;
                    // No bait where the player cannot get clear of it.
                    if (!these || !clearOfBait(view, p, n, blast))
                        continue;
                    hits |= these;
                    const int d = far[(size_t)n.v * view.extentU + n.u];
                    if (d >= 0 && (nearest < 0 || d < nearest))
                        nearest = d;
                }
                if (hits)
                    out.push_back({{p.u, p.v, 0}, hits, nearest < 0 ? 0 : nearest});
            }
    }
    return out;
}

const std::vector<Crossing> &Board::crossings() const
{
    if (crossingsFound_)
        return crossings_;
    crossingsFound_ = true;
    const BotView &view = live_;
    const std::vector<int> runs = keptOffRuns(view);
    auto runAt = [&](int u, int v) {
        return view.inside(u, v) ? runs[(size_t)v * view.extentU + u] : -1;
    };
    const NavModel over(view, true);
    for (int v = 0; v < view.extentV; v++)
        for (int u = 0; u < view.extentU; u++) {
            if (runAt(u, v) >= 0)
                continue;
            // From a cell beside a run, facing it.
            for (int f = DIR_MIN; f <= DIR_MAX; f++) {
                const int run = runAt(u + DIR_DU[f], v + DIR_DV[f]);
                if (run < 0 || run >= 32)
                    continue;
                const Paths paths(over, u, v, f);
                for (int tv = 0; tv < view.extentV; tv++)
                    for (int tu = 0; tu < view.extentU; tu++) {
                        const double ms = paths.cost(tu, tv);
                        if (ms <= 0 || runAt(tu, tv) >= 0)
                            continue;
                        // Over the run alone.
                        const std::vector<Step> cells = paths.cellsTo(tu, tv);
                        bool alone = cells.size() > 2;
                        for (size_t i = 1; i + 1 < cells.size() && alone; i++)
                            alone = runAt(cells[i].u, cells[i].v) == run;
                        if (alone)
                            crossings_.push_back({{u, v}, {tu, tv, paths.facingAt(tu, tv)}, ms,
                                                  1u << run});
                    }
            }
        }
    return crossings_;
}

bool Board::crossable(Changes s, Place from, unsigned crossed) const
{
    for (const Crossing &c : crossings())
        if (!(crossed & c.bit) && time(s, from, c.from) >= 0)
            return true;
    return false;
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

const std::vector<Release> &Board::releases() const
{
    if (releasesFound_)
        return releases_;
    releasesFound_ = true;
    FoeModel sim;
    hunted_ = catchersHunted(sim, live_);
    for (size_t b = 0; b < worlds_->blockCount() && b < 63; b++) {
        BotView opened = live_;
        const Step c = worlds_->block(b);
        opened.cells[(size_t)c.v * opened.extentU + c.u].busy = 1;
        const int then = catchersHunted(sim, opened);
        if (then > hunted_)
            releases_.push_back({(Changes)1 << b, then - hunted_});
    }
    return releases_;
}

int Board::hunted() const
{
    releases();
    return hunted_;
}

bool Board::releasePending(Changes s, int bombs) const
{
    if (bombs <= 0)
        return false;
    for (const Release &r : releases())
        if (!(s & r.gate))
            return true;
    return false;
}

}  // namespace bot
