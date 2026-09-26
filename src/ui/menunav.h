/* Saving the game into a save slot and loading it back, from the menu. */
#pragma once

class Game;

/* Stores the level, play time, completion, score and lives into slot slotArg
 * (low byte) and marks it in use.  Returns 1. */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_StoreGameStateIntoSaveSlot(Game *self, unsigned int slotArg);

/* Restores what Store saved.  Returns 1 in the low byte. */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_RestoreGameStateFromSaveSlot(Game *self, unsigned int slotArg);
