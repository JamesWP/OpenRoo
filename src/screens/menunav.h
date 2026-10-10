/* Saving the game into a save slot and loading it back, from the menu. */
#pragma once

class Game;

/* Stores the level, play time, completion, score and lives into slot slotArg
 * (low byte) and marks it in use.  Returns 1. */
  unsigned int  
Sim_StoreGameStateIntoSaveSlot(Game *self, unsigned int slotArg);

/* Restores what Store saved.  Returns 1. */
unsigned int Sim_RestoreGameStateFromSaveSlot(Game *self, unsigned int slotArg);
