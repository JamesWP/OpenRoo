/* dsoscene.h -- the scene-object passes (dsoscene.cpp). */
#pragma once
#include <stdint.h>
class Renderer;
class RenderDevice;
class Scene;
/* The level's scene objects, seen from the camera eye, at clock now (ms). */
void Scene_DrawSceneObjects(Renderer *renderer, RenderDevice *dev, float *cam, double t);
/* cdecl(scene, show paths, show control polygons, dev, camera eye, dt ms, now).
 * The two flags are the F3/F4 debug views (Renderer's showSplinePaths and
 * showSplineControlPolygons). */
  void  
Scene_DrawParticleSystems(Scene *scene, bool showPaths, bool showControlPolygons,
                          RenderDevice *dev, float *cam, double dt_ms, double t);
