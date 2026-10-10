/* rendergameframe.h -- one frame of the game (rendergameframe.cpp). */
#pragma once

/* One caller, WinMain's message loop, which passes the app's Renderer, MenuArt
 * and ProgableControl. */
class Renderer;
struct MenuArt;
class ProgableControl;

  void   Render_RenderGameFrame(Renderer *renderer, const MenuArt *art, ProgableControl *progCtrl);
