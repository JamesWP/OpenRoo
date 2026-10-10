/* The Game's start-up and shut-down (gameboot.cpp). */

#pragma once

class Game;
class ProgableControl;

/* Brings a Game up: its data, the game file, saves, config, high scores, the
 * first (menu) level and the audio.  The Game quits the window on failure.
 * progCtrl is app's; Game_Boot sets its joystick dead zones and keeps no
 * pointer to it. */
void Game_Boot(Game *g, const char *gameName, const char *gameDir, ProgableControl *progCtrl);

/* Tears it down: removes the entities, saves the high scores and config, and
 * releases the sounds. */
void Game_Shutdown(Game *g);
