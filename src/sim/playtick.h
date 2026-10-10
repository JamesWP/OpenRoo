/* The simulation's steps in a tick while a level runs (GameTick, gametick.cpp, calls them in order). */

#pragma once
class Game;

void Play_LoadSounds(Game *self);
void Play_SpawnTimedFoes(Game *self);
void Play_DropPlayerBomb(Game *self);
void Play_PlayerSwitch(Game *self);
void Play_TickFoes(Game *self);
void Play_OpenTallyMenu(Game *self);
void Play_CheckLevelEnd(Game *self);
