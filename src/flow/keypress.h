/* The menu keyboard handler, run once per tick while a menu is up. */

#pragma once

class Game;
class ProgableControl;

/* Polls the menu keys, moves through the menu tree, edits the option pages,
 * and acts on the node the player selected: new game, continue, load and
 * save slots, quit, key rebinding, sound and video toggles.  progCtrl is the
 * app's, which the joystick dead zone and key rebinding write to. */
  void
Sim_HandleKeypress(Game *self, ProgableControl *progCtrl);
