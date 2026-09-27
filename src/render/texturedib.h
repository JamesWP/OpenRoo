/* texturedib.h -- the owner header for texturedib.cpp's exports. */
#pragma once

#include <windows.h>
class LoadedImage;
class RenderDevice;

/* Load a .bmp into a new surface;
 * the low byte of the result is the success flag. */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
TextureDIB_CreateSurface(LoadedImage *self, RenderDevice *dev, LPCSTR name,
                         char bSysMem);
