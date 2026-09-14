/* The save-slot store/restore (menunav.cpp).  Sim_NavigateMenuTree is in menutree.h.
 * The export(s) patch.py binds, typed on the Game they take in ECX
 * (COHESION_PLAN.md, the void *self metric). */
#pragma once

class Game;

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_StoreGameStateIntoSaveSlot(Game *self, unsigned int slotArg);
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_RestoreGameStateFromSaveSlot(Game *self, unsigned int slotArg);
