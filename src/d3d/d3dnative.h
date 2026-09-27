/* RenderDevice::Native: the Direct3D objects behind the RenderDevice.  For
 * backend files only -- game code goes through renderdevice.h. */

#pragma once
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include "renderdevice.h"

struct RenderDevice::Native {
    IDirectDraw4        *dd;
    IDirect3D3          *d3d;
    IDirect3DDevice3    *device;
    IDirect3DViewport3  *viewport;
    IDirectDrawSurface4 *primary;
    IDirectDrawSurface4 *backBuffer;
    IDirectDrawSurface4 *zBuffer;
    DDPIXELFORMAT        zbufFmt;
    IDirect3DMaterial3  *material;
    D3DMATERIALHANDLE    hMaterial;
    IDirect3DLight      *light;
};

/* The backend's names for the game's vocabulary (renderdevice.cpp). */
D3DPRIMITIVETYPE d3d_prim(Prim p);
DWORD            d3d_fvf(VertexFormat f);
