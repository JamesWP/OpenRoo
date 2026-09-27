/* The level loaders: the map file (.jjm) and the level's script (.jjs). */
#pragma once

class Game;

/* Loads the level by name (the menu backdrop's demo level). */
unsigned int Sim_ParseLevelFiles(Game *self, const char *name);

/* Copies level levelNo's name from the game file's table into the Game. */
unsigned int Sim_SetCurrentLevelName(Game *self, unsigned int levelNo);

/* Loads level levelNo by number: every gameplay level goes through here. */
unsigned int Sim_OpenLevelFile(Game *self, unsigned int levelNo);
