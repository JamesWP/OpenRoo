/* The level loaders (levelparse.cpp).
 * The export(s) patch.py binds, typed on the Game they take in ECX
 * (COHESION_PLAN.md, the void *self metric). */
#pragma once

class Game;

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_ParseLevelFiles(Game *self, const char *name);
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_SetCurrentLevelName(Game *self, unsigned int levelNo);
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_OpenLevelFile(Game *self, unsigned int levelNo);
