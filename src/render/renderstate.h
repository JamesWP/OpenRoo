/* renderstate.h -- the fixed render state (renderstate.cpp). */
#pragma once

class Renderer;
class World;
class CameraPose;

/* Called once, by WinMain at startup.  False if a font failed to load; the
 * caller then quits. */
  bool   Render_ConfigureRenderState(Renderer *renderer, const World *level,
                                     CameraPose *cam, const char *gameDir);
