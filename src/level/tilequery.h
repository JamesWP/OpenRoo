/* The four tile queries (tilequery.cpp), __thiscall on Game -- except
 * FindFarthestOccupiedTile, whose `self` is the entity searching.  The owner
 * header: callers include this rather than redeclaring the exports. */
#pragma once

class Game;
class MovableEntity;

extern "C" {
__declspec(dllexport) void __attribute__((thiscall)) Sim_MarkListedTilesBlockedByObject(Game *self, unsigned int listIndex);
__declspec(dllexport) unsigned int __attribute__((thiscall)) Sim_FindNearestListedObjectTile(Game *self, unsigned char *pu, unsigned char *pv, unsigned char maxDist);
__declspec(dllexport) unsigned int __attribute__((thiscall)) Sim_FindNearestFlaggedTileInRadius(Game *self, unsigned char *pu, unsigned char *pv, unsigned char radius);
__declspec(dllexport) unsigned int __attribute__((thiscall)) Sim_FindFarthestOccupiedTile(MovableEntity *self, unsigned char *pu, unsigned char *pv);
void tilequery_census_object_types(Game *self);
}
