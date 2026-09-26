/* Camera controls: the seven camera actions the input system binds to keys
 * (zoom, overview, rotate, tilt).
 *
 * Each is an ActionCallback (progctrl.h) registered with the Game as its
 * context; the key and strength arguments are ignored.  Every step is
 * scaled by the tick step, so holding a key moves the camera at a rate, not
 * per frame. */

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
