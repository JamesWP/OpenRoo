/* Attaching a foe's sounds from the level's sound table. */
#pragma once

class Game;

/* Loads each sound whose name is set into the foe in slot objArg (low byte);
 * kind 2 foes get their own three.  Returns 1 in the low byte. */
unsigned int
Sim_AcquireObjectSoundBuffersForIndex(Game *self, unsigned int objArg);
