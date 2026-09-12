/* The four tile queries (tilequery.cpp), __thiscall on Game -- except
 * FindFarthestOccupiedTile, whose `self` is the entity searching.  The owner
 * header: callers include this rather than redeclaring the exports. */
#pragma once

extern "C" {
__declspec(dllexport) void __attribute__((thiscall)) Sim_MarkListedTilesBlockedByObject(void *self, unsigned int listIndex);
__declspec(dllexport) unsigned int __attribute__((thiscall)) Sim_FindNearestListedObjectTile(void *self, unsigned char *pu, unsigned char *pv, unsigned char maxDist);
__declspec(dllexport) unsigned int __attribute__((thiscall)) Sim_FindNearestFlaggedTileInRadius(void *self, unsigned char *pu, unsigned char *pv, unsigned char radius);
__declspec(dllexport) unsigned int __attribute__((thiscall)) Sim_FindFarthestOccupiedTile(void *self, unsigned char *pu, unsigned char *pv);
}
