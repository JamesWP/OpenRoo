/* Input setup: registers the thirteen player and camera actions, binds the
 * default keys, and saves the bindings again at shutdown.  The bindings live
 * in the ProgableControl (progctrl.h) that app owns and passes in. */

#pragma once
struct Game;
class ProgableControl;

/* progCtrl is the app's ProgableControl, which the actions are registered on. */
  void
Input_Setup(Game *game, ProgableControl *progCtrl);

/* Logs and writes the bindings file. */
  void   Input_TrySaveSettings(ProgableControl *progCtrl);
