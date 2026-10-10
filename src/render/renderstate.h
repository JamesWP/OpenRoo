/* renderstate.h -- the fixed render state (renderstate.cpp). */
#pragma once

struct Image;

/* The loading screen: bitmaps\<map>.bmp is loaded into the first; the second
 * is shown when that fails. */
extern Image g_loadingImage;
extern Image g_fallbackImage;

/* bitmaps\demo.bmp, loaded once at startup. */
extern Image g_demoImage;

/* A copy of the LevelMap's title, made at level entry. */
extern char g_levelTitle[128];

/* The clock (ms) at the last GameTick.  Level entry resets it, so the level's
 * first frame ticks from there. */
extern double g_lastTickMs;

/* Called once, by WinMain at startup. */
  void   Render_ConfigureRenderState(const char *gameDir);
