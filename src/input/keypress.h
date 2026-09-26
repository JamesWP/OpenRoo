/* The menu keyboard handler, run once per tick while a menu is up. */

#pragma once

class Game;

/* Polls the menu keys, moves through the menu tree, edits the option pages,
 * and acts on the node the player selected: new game, continue, load and
 * save slots, quit, key rebinding, sound and video toggles. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_HandleKeypress(Game *self);
