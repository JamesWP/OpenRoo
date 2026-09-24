/* inputsetup -- WinMain's input glue around the ProgableControl singleton,
 * from the AutoClass5 TU (0x403000..0x403ce0).
 *
 *   0x00403cb0  ControlTrySaveSettings   inputsetup.cpp
 */
#pragma once

/* 0x403cb0, __cdecl, no arguments: log, save the bindings, release every
 * input device.  One E8, WinMain's shutdown 0x42d647. */
extern "C" __declspec(dllexport) void __cdecl Input_TrySaveSettings(void);
