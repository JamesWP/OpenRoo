/* The level teardown (gamereset.cpp). */

#pragma once

class Game;

/* Clears the Game's level state, ready for the next level. */
extern "C" __declspec(dllexport) void  
Sim_ClearGameState(Game *self);
