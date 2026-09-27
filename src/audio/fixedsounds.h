/* Loading the fixed sounds, once, on the first game tick. */
#pragma once

class Game;

/* Saves Karoo.cfg, starts the main theme, loads the three banks of pickup
 * sounds and the fixed effects, and checks the level-report key.  Does nothing
 * after the first call, or if there is no sound device. */
void Sim_AcquireFixedSoundBuffersAndMaybeReport(Game *self);
