/* renderstate.h -- the fixed render state (renderstate.cpp). */
#pragma once

class Renderer;
class World;
class CameraPose;

/* Called once, by WinMain at startup. */
  void   Render_ConfigureRenderState(Renderer *renderer, const World *level,
                                     CameraPose *cam, const char *gameDir);
