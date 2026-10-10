/* The level teardown (gamereset.cpp). */

#pragma once

class Game;

/* Clears the Game's level state, ready for the next level. */
  void  
Sim_ClearGameState(Game *self);
