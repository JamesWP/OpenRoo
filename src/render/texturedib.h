/* texturedib.h -- the owner header for texturedib.cpp's exports. */
#pragma once

#include <windows.h>
#include "direct3d.h"

/* LoadedImage::CreateSurfaceDIB 0x43de40 -- load a .bmp into a new surface;
 * the low byte of the result is the success flag. */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
TextureDIB_CreateSurface(LoadedImage *self, IDirectDraw4 *dd, LPCSTR name,
                         char bSysMem);
