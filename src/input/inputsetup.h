/* Input setup: brings the input devices up for the game window, registers the
 * thirteen player and camera actions, binds the default keys, and saves the
 * bindings again at shutdown.  The devices and bindings live in the single
 * ProgableControl (progctrl.h). */

#pragma once
#include <windows.h>
struct Game;

/* Returns 1, or 0 after showing an error box when the keyboard, mouse or
 * device acquisition fails.  A missing joystick is not an error.  The third
 * argument is unused. */
  int  
Input_Setup(HINSTANCE hInstance, HWND hwnd, Game *game);

/* Logs, writes the bindings file and releases every input device. */
  void   Input_TrySaveSettings(void);
