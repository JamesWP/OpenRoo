/* Input setup: brings DirectInput up for the game window, registers the
 * thirteen player and camera actions, binds the default keys, and saves the
 * bindings again at shutdown.  The devices and bindings live in the single
 * ProgableControl (progctrl.h). */

#pragma once
#include <windows.h>
struct Game;

/* Returns 1, or 0 after showing an error box when the keyboard, mouse or
 * device acquisition fails.  A missing joystick is not an error.  The third
 * argument is unused. */
extern "C" __declspec(dllexport) int __cdecl
Input_DirectInputSetup(HINSTANCE hInstance, HWND hwnd, DWORD unused, Game *game);

/* Logs, writes the bindings file and releases every input device. */
extern "C" __declspec(dllexport) void __cdecl Input_TrySaveSettings(void);
