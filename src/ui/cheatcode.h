/* The typed cheat codes (cheatcode.cpp). */
#pragma once

class Game;

/* Reads the letters typed since the last call and applies any cheat code they
 * complete. */
  void  
Sim_HandleTypedCheatCode(Game *self);

/* The cheats themselves, for the debug UI as well as the typed codes. */
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

/* The debug UI's Cheats section (dbg.h).  Does nothing unless it is shown. */
void Cheat_DebugPanel(Game *g);
