/* dsoscene.h -- the scene-object passes (dsoscene.cpp). */
#pragma once
#include <windows.h>
#include <d3d.h>
/* cdecl(dev, camera eye, two unread dwords, now). */
extern "C" __declspec(dllexport) void __cdecl
Scene_DrawSceneObjects(IDirect3DDevice3 *dev, float *cam, DWORD a3, DWORD a4, double t);
/* cdecl(dev, camera eye, dt ms, now). */
extern "C" __declspec(dllexport) void __cdecl
Scene_DrawParticleSystems(IDirect3DDevice3 *dev, float *cam, double dt_ms, double t);
