/* The level score (levelscore.cpp).
 * The export(s) patch.py binds, typed on the Game they take in ECX
 * (COHESION_PLAN.md, the void *self metric). */
#pragma once

class Game;

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Score_CalculateLevelScore(Game *self, char endReason);
