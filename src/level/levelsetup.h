/* The level builder: turns a parsed level map into live objects. */
#pragma once

class Game;

/* Zeroes the per-level object counters. */
extern "C" __declspec(dllexport) void  
Sim_ResetLevelObjectCounters(Game *self);

/* Clears the per-level state, tears down the previous level's objects, finds
 * the player start, then walks the map once spawning lifts, slides,
 * breakables, bridges and foes, wiring switches and teleporters, and counting
 * what the score and the HUD need. */
extern "C" __declspec(dllexport) unsigned int  
Sim_SetupLevelObjects(Game *self);
