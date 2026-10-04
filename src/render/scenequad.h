/* scenequad -- the quad draw behind Scene_RenderSceneObjects' kind-2 objects
 * (scenequad.cpp). */
#pragma once
#include <stdint.h>
#include "renderdevice.h"
#include "sceneobjects.h"

/* One Diffuse1 triangle strip of the four vertices q, which carry two UV
 * sets of which only the second is drawn. */
bool SceneQuad_Draw(RenderDevice *dev, const SceneQuadVertex *q);
