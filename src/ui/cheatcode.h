/* The typed cheat codes (cheatcode.cpp). */
#pragma once

class Game;

/* Reads the letters typed since the last call and applies any cheat code they
 * complete. */
extern "C" __declspec(dllexport) void  
Sim_HandleTypedCheatCode(Game *self);
