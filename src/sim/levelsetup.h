/* The level builder: turns a parsed level map into live objects. */
#pragma once

class Game;

/* Zeroes the per-level object counters. */
  void  
Sim_ResetLevelObjectCounters(Game *self);

/* Clears the per-level state, tears down the previous level's objects, finds
 * the player start, then walks the map once spawning lifts, platforms,
 * falling tiles, bridges and foes, wiring switches and teleporters, and counting
 * what the score and the HUD need. */
  unsigned int  
Sim_SetupLevelObjects(Game *self);
