/* scenequad -- the repaired strided quad draw (scenequad.cpp).  Once the
 * INDIRECT_CALL patch inside RenderSceneObjects; now called directly by its
 * reimplementation (sceneobjects.cpp). */
#pragma once
#include <windows.h>
#include <d3d.h>

extern "C" __declspec(dllexport) HRESULT WINAPI
hooks_SceneQuadDrawStrided(IDirect3DDevice3 *dev, D3DPRIMITIVETYPE prim, DWORD fvf,
                           D3DDRAWPRIMITIVESTRIDEDDATA *data, DWORD vert_count, DWORD flags);
