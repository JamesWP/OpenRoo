/* scenequad -- the strided quad draw behind Scene_RenderSceneObjects'
 * kind-2 objects (scenequad.cpp). */
#pragma once
#include <windows.h>
#include <d3d.h>

extern "C" __declspec(dllexport) HRESULT WINAPI
hooks_SceneQuadDrawStrided(IDirect3DDevice3 *dev, D3DPRIMITIVETYPE prim, DWORD fvf,
                           D3DDRAWPRIMITIVESTRIDEDDATA *data, DWORD vert_count, DWORD flags);
