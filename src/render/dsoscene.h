/* dsoscene.h -- the scene-object passes (dsoscene.cpp). */
#pragma once
#include <windows.h>
class RenderDevice;
/* cdecl(dev, camera eye, two unread dwords, now). */
  void  
Scene_DrawSceneObjects(RenderDevice *dev, float *cam, DWORD a3, DWORD a4, double t);
/* cdecl(dev, camera eye, dt ms, now). */
  void  
Scene_DrawParticleSystems(RenderDevice *dev, float *cam, double dt_ms, double t);
