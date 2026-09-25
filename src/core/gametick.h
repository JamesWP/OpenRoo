/* gametick.h -- GameTick 0x00414df0 (gametick.cpp). */
#pragma once
class Game;
/* __thiscall(self, double dt, double now); one caller, RenderGameFrame. */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_GameTick(Game *self, double dt, double now);
