/* The per-frame simulation step (gametick.cpp). */

#pragma once
class Game;
class ProgableControl;

/* Advances the level by dt milliseconds at clock time now.  Called once a
 * frame by the frame's update (frame.h), which passes the app's progCtrl
 * through. */
void Core_GameTick(Game *self, double dt, double now, ProgableControl *progCtrl);
