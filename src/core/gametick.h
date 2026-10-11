/* The per-frame simulation step (gametick.cpp). */

#pragma once
class Game;
class ProgableControl;

/* Advances the level by dt seconds at clock time now.  Called once a frame by
 * RenderGameFrame, which passes the app's progCtrl through. */
  unsigned int
Core_GameTick(Game *self, double dt, double now, ProgableControl *progCtrl);
