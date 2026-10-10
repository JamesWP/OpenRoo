#pragma once
#include <stdint.h>

/* The menu screens.  Each frame the dispatch picks one screen by menu node and
 * it draws the whole screen: backdrop, text and widgets.  Every screen but the
 * credits takes the front end, session or config it reads, then (theme, device,
 * text, ms); theme is the object holding
 * the theme file's colours and backdrop texture, read at fixed offsets.  The
 * level-select pages are ours (levelselect.h). */

#include "texture.h"
#include "renderdevice.h"
#include "menuart.h"

class FrontEnd;
class Session;
class Config;
class RenderDevice;
class TextRenderer;
class ThemeAssetBlock;

/* The full-screen backdrop every menu screen opens with. */
void Menu_DrawBackdrop(RenderDevice *d3d, ThemeAssetBlock *theme);

/* The two animated markers either side of the highlighted row.  rowOffset
 * shifts them, in 640-wide screen units (0 on the main menu). */
  void  
Menu_DrawCursorMarkers(const MenuArt *art, const FrontEnd *frontEnd, RenderDevice *d3d, uint32_t ms,
                       float rowOffset);

/* Node 0: the main menu. */
void
Menu_RenderMainMenu(const MenuArt *art, const FrontEnd *frontEnd, ThemeAssetBlock *theme,
                    RenderDevice *d3d, TextRenderer *text, uint32_t ms);

/* Node 4: Controls, Video, Audio. */
void
Menu_RenderOptionsMenu(const MenuArt *art, const FrontEnd *frontEnd, ThemeAssetBlock *theme,
                       RenderDevice *d3d, TextRenderer *text, uint32_t ms);

/* Node 2, Load Game: the save-slot names. */
void Menu_RenderRestoreSlotList(const MenuArt *art, FrontEnd *frontEnd, ThemeAssetBlock *theme,
                                RenderDevice *d3d, TextRenderer *text, uint32_t ms);

/* Node 0x2a, Save Game (from the level-complete screen). */
void Menu_RenderSaveSlotList(const MenuArt *art, FrontEnd *frontEnd, ThemeAssetBlock *theme,
                             RenderDevice *d3d, TextRenderer *text, uint32_t ms);

/* Builds the static quads, the widget model and the nine textures (art) the
 * screens draw with; once per device. */
  void  
Menu_BuildMenuGeometry(MenuArt *art, RenderDevice *d3d, const char *prefix);

/* Node 0xa: the key-binding page. */
void Menu_RenderControlsRemap(const MenuArt *art, const FrontEnd *frontEnd, const Config *config,
                              ThemeAssetBlock *theme, RenderDevice *d3d, TextRenderer *text,
                              uint32_t ms);

/* That page's own cursor markers. */
  void  
Menu_DrawControlsCursorMarkers(const MenuArt *art, const FrontEnd *frontEnd, RenderDevice *d3d,
                               uint32_t ms);

/* Node 0xb: the video options. */
void Menu_RenderVideoOptions(const MenuArt *art, const FrontEnd *frontEnd, const Config *config,
                             ThemeAssetBlock *theme,
                             RenderDevice *d3d, TextRenderer *text, uint32_t ms);

/* Node 0xc: the audio options. */
void Menu_RenderSoundOptions(const MenuArt *art, const FrontEnd *frontEnd, const Config *config,
                             ThemeAssetBlock *theme,
                             RenderDevice *d3d, TextRenderer *text, uint32_t ms);

/* Node 5: the credits; nowMs is the millisecond clock. */
  void  
Menu_RenderCreditsScroll(FrontEnd *frontEnd, RenderDevice *d3d, TextRenderer *text, uint32_t nowMs);

/* Picks and draws the screen for the current menu node. */
void
Menu_DispatchGameState(const MenuArt *art, FrontEnd *frontEnd, Session *session, const Config *config,
                       ThemeAssetBlock *theme, RenderDevice *d3d, TextRenderer *text, uint32_t ms);

/* A level-select theme page. */
void Menu_RenderLevelSelect(const MenuArt *art, const FrontEnd *frontEnd, Session *session,
                            ThemeAssetBlock *theme, RenderDevice *d3d, TextRenderer *text,
                            uint32_t ms);
