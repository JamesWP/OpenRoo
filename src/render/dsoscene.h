/* dsoscene.h -- the scene-object passes (dsoscene.cpp). */
#pragma once
#include <stdint.h>
class Renderer;
class RenderDevice;
class Scene;
class World;
/* cdecl(renderer, dev, world, camera eye, two unread dwords, now). */
  void  
Scene_DrawSceneObjects(Renderer *renderer, RenderDevice *dev, const World *world, float *cam,
                        uint32_t a3, uint32_t a4, double t);
/* cdecl(scene, show paths, show control polygons, dev, camera eye, dt ms, now).
 * The two flags are the F3/F4 debug views (Renderer's showSplinePaths and
 * showSplineControlPolygons). */
  void  
Scene_DrawParticleSystems(Scene *scene, bool showPaths, bool showControlPolygons,
                          RenderDevice *dev, float *cam, double dt_ms, double t);
