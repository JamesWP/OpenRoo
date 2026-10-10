/* Input setup: brings the input devices up for the game window, registers the
 * thirteen player and camera actions, binds the default keys, and saves the
 * bindings again at shutdown.  The devices and bindings live in the
 * ProgableControl (progctrl.h) that app owns and passes in. */

#pragma once
struct Game;
class ProgableControl;

/* Returns 1, or 0 after showing an error box when the keyboard, mouse or
 * device acquisition fails.  A missing joystick is not an error.  progCtrl is
 * the app's ProgableControl, which the actions are registered on. */
  int
Input_Setup(void *hwnd, Game *game, ProgableControl *progCtrl);

/* Logs, writes the bindings file and releases every input device. */
  void   Input_TrySaveSettings(ProgableControl *progCtrl);
