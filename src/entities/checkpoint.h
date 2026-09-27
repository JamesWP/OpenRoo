/* The scripted-camera step (checkpoint.cpp). */

#pragma once

class Game;

/* While a script is running, ticks it and copies its camera into the Game. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_RestoreCheckpointStateBlocks(Game *self);
