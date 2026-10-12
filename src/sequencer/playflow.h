/* The player's input around the play: the flythrough, the pause menu, the controls while playing, and the ENTER that restarts, leaves the game-over tally or ends name entry (GameTick calls them). */

#pragma once
class Game;
class ProgableControl;

void Flow_TickFlythrough(Game *self);
void Flow_OpenPauseMenu(Game *self);
void Flow_TickPlayingControls(Game *self, ProgableControl *progCtrl);
void Flow_TickIdleControls(Game *self, ProgableControl *progCtrl);
void Flow_RestartAfterDeath(Game *self);
void Flow_LeaveGameOver(Game *self);
void Flow_LeaveNameEntry(Game *self);
