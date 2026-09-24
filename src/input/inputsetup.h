/* inputsetup -- WinMain's input glue around the ProgableControl singleton,
 * from the AutoClass5 TU (0x403000..0x403ce0).
 *
 *   0x00403940  DirectInputSetup         inputsetup.cpp
 *   0x00403cb0  ControlTrySaveSettings   inputsetup.cpp
 */
#pragma once
#include <windows.h>
struct Game;

/* 0x403940, __cdecl: bring DirectInput up on the window, register the
 * thirteen player/camera actions (player.cpp, camerainput.cpp), bind the
 * default keys if no saved bindings load, acquire.  Returns 1, or 0 after a
 * German error box.  The third argument is unused.  One E8, WinMain
 * 0x42d43c. */
extern "C" __declspec(dllexport) int __cdecl
Input_DirectInputSetup(HINSTANCE hInstance, HWND hwnd, DWORD unused, Game *game);

/* 0x403cb0, __cdecl, no arguments: log, save the bindings, release every
 * input device.  One E8, WinMain's shutdown 0x42d647. */
extern "C" __declspec(dllexport) void __cdecl Input_TrySaveSettings(void);
