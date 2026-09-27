/* The end-of-level score tally animation. */
#pragma once

class Game;

/* Advances the tally one step: each stage counts its line up, with a sound,
 * and the total follows. */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_AnimateScoreTallyStages(Game *self);
