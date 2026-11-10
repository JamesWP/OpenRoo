#include "survey.h"
#include "facts.h"
#include "tile.h"
#include "world.h"

namespace simbot {

Survey::Survey(const World &start) : graph_(start)
{
    const LevelMap *map = start.map();
    for (int v = 0; v < map->extentV(); v++)
        for (int u = 0; u < map->extentU(); u++)
            if (map->tile(u, v)->contents() == CONTENTS_CRYSTAL && gems_.size() < GemSet::MAX)
                gems_.push_back({u, v});
    exit_  = {start.player()->markerCellU(), start.player()->markerCellV()};
    quota_ = start.gemsRequired();
}

}  // namespace simbot
