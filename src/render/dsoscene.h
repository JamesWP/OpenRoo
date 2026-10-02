/* dsoscene.h -- the scene-object passes (dsoscene.cpp). */
#pragma once
#include <stdint.h>
class RenderDevice;
/* cdecl(dev, camera eye, two unread dwords, now). */
  void  
Scene_DrawSceneObjects(RenderDevice *dev, float *cam, uint32_t a3, uint32_t a4, double t);
/* cdecl(dev, camera eye, dt ms, now). */
  void  
Scene_DrawParticleSystems(RenderDevice *dev, float *cam, double dt_ms, double t);
