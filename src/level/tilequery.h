/* Tile queries against the level map: which tile answers a question about the
 * object lists or the tile flags. */
#pragma once

class Game;
class LevelMap;
class SwitchCells;
class MovableEntity;

 
/* Marks the tiles of object list listIndex as blocked. */
  void   Sim_MarkListedTilesBlockedByObject(Game *self, unsigned int listIndex);

/* The nearest tile of the listed objects to (*pu, *pv), within maxDist;
 * written back through pu and pv. */
  unsigned int   Sim_FindNearestListedObjectTile(SwitchCells *sw, unsigned char switchMax, LevelMap *map, unsigned char *pu, unsigned char *pv, unsigned char maxDist);

/* The nearest flagged tile within radius of (*pu, *pv), written back. */
  unsigned int   Sim_FindNearestFlaggedTileInRadius(LevelMap *map, unsigned char *pu, unsigned char *pv, unsigned char radius);

/* self is the entity searching: the farthest occupied tile from it. */
  unsigned int   Sim_FindFarthestOccupiedTile(MovableEntity *self, unsigned char *pu, unsigned char *pv);

/* KAROO_TILEQ_DIAG=1: a census of object types per level. */
void tilequery_census_object_types(Game *self);
