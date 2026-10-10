/* Attaching a level's sounds to its objects once the level is built. */
#pragma once

class Game;

/* Picks the world's sound code, reloads the level's pools and effect buffers,
 * and attaches sounds to every foe, falling tile, lift, platform, bridge and (in 3D)
 * the extra objects that carry one.  Returns 0 in the low byte. */
  unsigned int  
Sim_InitLevelBasedSounds(Game *self);
