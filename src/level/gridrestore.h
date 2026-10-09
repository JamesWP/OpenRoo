/* Restoring the level's tiles from the snapshot taken at setup, when the
 * player restarts the level. */
#pragma once

class LevelMap;

/* Copies each tile back from the snapshot, with the state byte's special cases
 * (see gridrestore.cpp). */
  void  
Sim_RestoreTileGridFromSnapshot(LevelMap *map);
