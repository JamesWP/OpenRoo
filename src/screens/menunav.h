/* Saving the game into a save slot and loading it back, from the menu. */
#pragma once

class FrontEnd;
class Session;
class World;

/* Stores the level, play time, completion, score and lives into slot slotArg
 * (low byte) and marks it in use.  Returns 1. */
  unsigned int  
Menu_StoreGameStateIntoSaveSlot(FrontEnd *frontEnd, const Session *session, const World *world,
                               unsigned int slotArg);

/* Restores what Store saved.  Returns 1. */
unsigned int Menu_RestoreGameStateFromSaveSlot(FrontEnd *frontEnd, Session *session, World *world,
                                              unsigned int slotArg);
