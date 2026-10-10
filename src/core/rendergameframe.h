/* rendergameframe.h -- one frame of the game (rendergameframe.cpp). */
#pragma once

/* One caller, WinMain's message loop, which passes the app's Renderer. */
class Renderer;

  void   Render_RenderGameFrame(Renderer *renderer);
