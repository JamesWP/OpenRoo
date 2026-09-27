/* scenequad -- the strided quad draw behind Scene_RenderSceneObjects'
 * kind-2 objects (scenequad.cpp). */
#pragma once
#include "renderdevice.h"

/* A four-vertex Diffuse2 triangle strip from v. */
bool SceneQuad_Draw(RenderDevice *dev, StridedVertices *v, uint32_t count);
