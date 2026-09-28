/* texturedib.h -- the owner header for texturedib.cpp's exports. */
#pragma once

#include <windows.h>
class LoadedImage;
class RenderDevice;

/* Load a .bmp into a new surface;
 * the low byte of the result is the success flag. */
  unsigned int  
TextureDIB_CreateSurface(LoadedImage *self, RenderDevice *dev, LPCSTR name,
                         char bSysMem);
