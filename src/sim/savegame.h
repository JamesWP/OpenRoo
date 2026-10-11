/* Saving the game into a save slot and loading it back: the play-through's
 * state (level, play time, completion, score, lives) goes to and from the
 * front end's save slots. */
#pragma once

class FrontEnd;
class Session;
class World;

/* Stores the level, play time, completion, score and lives into slot slotArg
 * (low byte) and marks it in use.  Returns 1. */
unsigned int SaveGame_StoreIntoSaveSlot(FrontEnd *frontEnd, const Session *session, const World *world,
                                        unsigned int slotArg);

/* Restores what Store saved.  Returns 1. */
unsigned int SaveGame_RestoreFromSaveSlot(FrontEnd *frontEnd, Session *session, World *world,
                                          unsigned int slotArg);
