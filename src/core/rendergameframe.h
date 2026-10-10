/* rendergameframe.h -- one frame of the game (rendergameframe.cpp). */
#pragma once

/* One caller, WinMain's message loop, which passes the app's Renderer and
 * MenuArt. */
class Renderer;
struct MenuArt;

  void   Render_RenderGameFrame(Renderer *renderer, const MenuArt *art);
