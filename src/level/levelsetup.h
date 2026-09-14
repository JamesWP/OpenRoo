/* The level object builder (levelsetup.cpp).
 * The export(s) patch.py binds, typed on the Game they take in ECX
 * (COHESION_PLAN.md, the void *self metric). */
#pragma once

class Game;

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_ResetLevelObjectCounters(Game *self);
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_SetupLevelObjects(Game *self);
