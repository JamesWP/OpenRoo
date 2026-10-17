/* The OpenGL backend's one public function: makes a RenderDevice that draws
 * with OpenGL ES 3.2, or desktop OpenGL 3.3 core without it.  Nothing else
 * of src/gl/ is for use outside it; everything after this goes through renderdevice.h. */

#pragma once
#include <memory>
#include "renderdevice.h"

/* A device that has not been Created yet (see RenderDevice::Create); the
 * enumeration and headless() work on it as they do on a Created one. */
std::unique_ptr<RenderDevice> createGLRenderDevice();
