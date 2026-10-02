/* The shared singletons and globals, one name each; gameglobals.cpp defines
 * them.  The types are incomplete here: a file that only passes a singleton
 * needs no layout, and one that uses it includes the owning header. */

#ifndef KAROO_GAMEGLOBALS_H
#define KAROO_GAMEGLOBALS_H

#include <windows.h>
#include <stdio.h>

class LoadedImage;
class CDM;
class ProgableControl;

/* The CD audio device; cdm.cpp owns its methods. */
extern CDM g_cdAudio;

/* The programmable-control singleton (progctrl.h). */
extern ProgableControl g_progCtrl;

/* The level entry's globals (levelentry.cpp).  The camera block beside them
 * has a layout, so it lives in camera.h. */

/* The loading screen: bitmaps\<map>.bmp is loaded into the first; the second
 * is shown when that fails. */
extern LoadedImage g_loadingImage;
extern LoadedImage g_fallbackImage;

/* bitmaps\demo.bmp, loaded once at startup (renderstate.cpp). */
extern LoadedImage g_demoImage;

/* A copy of the LevelMap's title, made at level entry. */
extern char g_levelTitle[128];

/* The clock (ms) at the last GameTick.  Level entry resets it, so the level's
 * first frame ticks from there. */
extern double g_lastTickMs;

/* The install directory, filled by WinMain; game paths are formatted against
 * it. */
extern char g_gameDir[260];
static const size_t GG_GAME_DIR_LEN = 0x104;

extern HINSTANCE g_moduleInstance;

#endif
