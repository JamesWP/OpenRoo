#include "survey.h"
#include <algorithm>
#include <set>
#include "facts.h"
#include "tile.h"
#include "world.h"

namespace simbot {

namespace {

/* Whether the player may ever have a bomb for a foe: foes, and bombs in
 * hand or a pickup of them. */
bool bombsForFoes(const World &w)
{
    if (w.foes().empty())
        return false;
    if (w.player()->bombsCarried() > 0)
        return true;
    const LevelMap *map = w.map();
    for (int v = 0; v < map->extentV(); v++)
        for (int u = 0; u < map->extentU(); u++)
            if (map->tile(u, v)->contents() == CONTENTS_GRANT_09)
                return true;
    return false;
}

/* Whether the player may ever glide: glider charges in hand, or a glider
 * to pick up. */
bool glidesToBe(const World &w)
{
    if (w.player()->glides() > 0)
        return true;
    const LevelMap *map = w.map();
    for (int v = 0; v < map->extentV(); v++)
        for (int u = 0; u < map->extentU(); u++)
            if (map->tile(u, v)->contents() == CONTENTS_PARAGLIDER)
                return true;
    return false;
}

}  // namespace

Survey::Survey(const World &start)
    : graph_(start, bombsForFoes(start), glidesToBe(start))
{
    const LevelMap *map = start.map();
    for (int v = 0; v < map->extentV(); v++)
        for (int u = 0; u < map->extentU(); u++)
            if (map->tile(u, v)->contents() == CONTENTS_CRYSTAL && gems_.size() < IndexSet::MAX)
                gems_.push_back({u, v});
            else if (map->tile(u, v)->contents() == CONTENTS_GRANT_09 &&
                     pickups_.size() < IndexSet::MAX)
                pickups_.push_back({u, v});
            else if (map->tile(u, v)->contents() == CONTENTS_PARAGLIDER &&
                     gliders_.size() < IndexSet::MAX)
                gliders_.push_back({u, v});
    exit_  = {start.player()->markerCellU(), start.player()->markerCellV()};
    quota_ = start.gemsRequired();
    for (const Foe &f : start.foes())
        if (f.isCatcher() && foes_.size() < 64)
            foes_.push_back(f.id());
    seen_.resize(foes_.size());
    see(start);
    for (const FallingTile &f : start.fallings())
        fallings_.push_back({f.cellU(), f.cellV()});
}

void Survey::see(const World &w)
{
    for (size_t i = 0; i < foes_.size(); i++)
        if (const Foe *f = findFoe(w, foes_[i]))
            seen_[i] = {f->cellU(), f->cellV()};
}

namespace {

/* A stand is on the foe's way by its tile if the foe comes to it no more
 * than this after the tile (a step, or a step and a drop). */
const double ON_WAY_MS = 400;

}  // namespace

const std::vector<Survey::Stand> &Survey::stands(Cell foe) const
{
    auto it = stands_.find(foe);
    if (it != stands_.end())
        return it->second;
    std::vector<Stand> &out = stands_[foe];
    const MoveGraph &g = graph_;
    if (fallings_.empty() || g.on(foe).empty())
        return out;
    auto falling = [&](Cell c) {
        return std::find(fallings_.begin(), fallings_.end(), c) != fallings_.end();
    };
    // The foe walks as the player would: its time to each cell, and the
    // cells it comes to without crossing a falling tile.
    const int from = g.on(foe).front();
    const MoveGraph::Table times = g.times(from);
    auto timeTo = [&](Cell c) {
        double best = -1;
        for (int p : g.on(c))
            if ((*times)[p] >= 0 && (best < 0 || (*times)[p] < best))
                best = (*times)[p];
        return best;
    };
    std::set<Cell> open;
    std::vector<int> stack{from};
    std::set<int> seen{from};
    while (!stack.empty()) {
        const int p = stack.back();
        stack.pop_back();
        open.insert(g.place(p).cell());
        for (const MoveGraph::Edge &e : g.edges(p))
            if (!falling(g.place(e.to).cell()) && seen.insert(e.to).second)
                stack.push_back(e.to);
    }
    std::vector<std::pair<double, Stand>> found;
    for (const Cell &t : fallings_) {
        const double toTile = timeTo(t);
        if (toTile < 0)
            continue;
        for (const Cell &n : {Cell{t.u + 1, t.v}, Cell{t.u - 1, t.v}, Cell{t.u, t.v + 1},
                              Cell{t.u, t.v - 1}}) {
            const double toStand = timeTo(n);
            // Just past the tile from the foe: its way there is by the tile.
            if (falling(n) || open.count(n) || toStand <= toTile || toStand > toTile + ON_WAY_MS)
                continue;
            found.push_back({toTile, {t, n, toTile, toStand}});
        }
    }
    std::stable_sort(found.begin(), found.end(),
                     [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &f : found)
        out.push_back(f.second);
    return out;
}

}  // namespace simbot
