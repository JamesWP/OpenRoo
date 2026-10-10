/* Tile queries against the level map: which tile answers a question about the
 * object lists or the tile flags. */
#pragma once

class Game;
class LevelMap;
class SwitchCells;

 
/* Marks the tiles of object list listIndex as blocked, from the phase of the
 * bridge on that list. */
  void   Sim_MarkListedTilesBlockedByObject(SwitchCells *sw, LevelMap *map, int bridgePhase, unsigned int listIndex);

/* The nearest tile of the listed objects to (*pu, *pv), within maxDist;
 * written back through pu and pv. */
  unsigned int   Sim_FindNearestListedObjectTile(SwitchCells *sw, unsigned char switchMax, LevelMap *map, unsigned char *pu, unsigned char *pv, unsigned char maxDist);

/* The nearest flagged tile within radius of (*pu, *pv), written back. */
  unsigned int   Sim_FindNearestFlaggedTileInRadius(LevelMap *map, unsigned char *pu, unsigned char *pv, unsigned char radius);

/* The occupied tile farthest from (*pu, *pv), written back. */
  unsigned int   Sim_FindFarthestOccupiedTile(LevelMap *map, unsigned char *pu, unsigned char *pv);

