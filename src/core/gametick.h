/* The per-frame simulation step (gametick.cpp). */

#pragma once
class Game;

/* Advances the level by dt seconds at clock time now.  Called once a frame by
 * RenderGameFrame. */
unsigned int Sim_GameTick(Game *self, double dt, double now);
