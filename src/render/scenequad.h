/* scenequad -- the strided quad draw behind Scene_RenderSceneObjects'
 * kind-2 objects (scenequad.cpp). */
#pragma once
#include <stdint.h>
#include "renderdevice.h"

/* A Diffuse1 triangle strip of count vertices from v. */
bool SceneQuad_Draw(RenderDevice *dev, StridedVertices *v, uint32_t count);
