/* The cheats: what each code does, for the typed codes (cheatcode.cpp) and the debug UI. */
#pragma once

class Game;

/* The tail both level loaders share: go to the loaded state with the intro
 * flythrough armed. */
void Cheat_EnterLoadedState(Game *game);

void Cheat_KillFoes(Game *g);        // kaputo
void Cheat_AddLife(Game *g, int n);  // mausuruh
void Cheat_AddGlide(Game *g);        // sportsman
void Cheat_AddBombs(Game *g);        // boommaker: ten
void Cheat_Invulnerable(Game *g);    // notme
void Cheat_FreezeFoes(Game *g);      // restarts the freeze pickup's window

/* jjmapnr: loads level `lvl` (0-based) by number and enters it.  False if
 * there is no such level or its file is missing. */
bool Cheat_LoadLevel(Game *g, unsigned char lvl);

/* Moves the player to cell (u, v), standing on its floor, as a teleporter does. */
void Cheat_TeleportPlayer(Game *g, unsigned char u, unsigned char v);
