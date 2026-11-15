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
}  // namespace simbot
