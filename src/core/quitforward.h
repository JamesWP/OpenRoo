/* Forwards a fatal-error quit requested in the simulation to the window layer
 * (quitforward.cpp). */
#pragma once

class Game;

/* If the Game has a quit requested (Game::requestQuit), asks the window to
 * quit with its code and clears the request.  Called where core hands control
 * back from the simulation. */
void Core_ForwardQuit(Game *g);
