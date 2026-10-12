/* The end-of-level score tally animation. */
#pragma once

class Game;

/* Advances the tally one step: each stage counts its line up, with a sound,
 * and the total follows. */
  unsigned int  
Flow_AnimateScoreTallyStages(Game *self);
