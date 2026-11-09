/* Tile queries against the level map: which tile answers a question about the
 * object lists or the tile flags. */
#pragma once

#include "levelmap.h"
#include "tile.h"

class Game;

/* Calls f(u, v) for each tile of switch sw, v outer and u inner. */
template <class F>
void Sim_ForEachSwitchCell(const LevelMap *map, unsigned int sw, F f)
{
    for (unsigned v = 0; v < map->extentV(); v++)
        for (unsigned u = 0; u < map->extentU(); u++) {
            const Tile *t = map->tile((int)u, (int)v);
            if (t->objectMarker() == TILE_SWITCH && t->field1f3() == (sw & 0xff))
                f((unsigned char)u, (unsigned char)v);
        }
}

/* Sets switch sw's tiles busy, or not, from the phase of the bridge it arms. */
void Sim_MarkSwitchTiles(LevelMap *map, int bridgePhase, unsigned int sw);

/* The busy switch tile nearest (*pu, *pv) within maxDist, written back. */
unsigned int Sim_FindNearestBusySwitch(const LevelMap *map, unsigned char *pu, unsigned char *pv,
                                       unsigned char maxDist);

/* The nearest flagged tile within radius of (*pu, *pv), written back. */
  unsigned int   Sim_FindNearestFlaggedTileInRadius(LevelMap *map, unsigned char *pu, unsigned char *pv, unsigned char radius);

/* The occupied tile farthest from (*pu, *pv), written back. */
  unsigned int   Sim_FindFarthestOccupiedTile(LevelMap *map, unsigned char *pu, unsigned char *pv);

