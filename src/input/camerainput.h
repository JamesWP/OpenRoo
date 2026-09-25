/* camerainput -- the seven camera actions DirectInputSetup 0x403940
 * registers with the ProgableControl (context = the Game), from the Game TU:
 *
 *   0x00419c30  CameraZoomOut       0x00419c70  CameraZoomIn
 *   0x00419cb0  CameraOverview
 *   0x00419ce0  CameraRotateRight   0x00419d00  CameraRotateLeft
 *   0x00419d20  CameraTiltUp        0x00419d80  CameraTiltDown
 *
 * Each is an ActionCallback (progctrl.h): __cdecl(key, strength, context);
 * only the context is read.  Every step is scaled by the tick step dt. */
#pragma once

extern "C" {
__declspec(dllexport) void __cdecl Camera_ZoomOut(int key, int strength, void *game);
__declspec(dllexport) void __cdecl Camera_ZoomIn(int key, int strength, void *game);
__declspec(dllexport) void __cdecl Camera_Overview(int key, int strength, void *game);
__declspec(dllexport) void __cdecl Camera_RotateRight(int key, int strength, void *game);
__declspec(dllexport) void __cdecl Camera_RotateLeft(int key, int strength, void *game);
__declspec(dllexport) void __cdecl Camera_TiltUp(int key, int strength, void *game);
__declspec(dllexport) void __cdecl Camera_TiltDown(int key, int strength, void *game);
}
