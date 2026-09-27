#pragma once
#include <windows.h>

/* The menu screens.  Each frame the dispatch picks one screen by menu node and
 * it draws the whole screen: backdrop, text and widgets.  Every screen but the
 * credits takes (game, theme, device, text, ms); theme is the object holding
 * the theme file's colours and backdrop texture, read at fixed offsets.  The
 * level-select pages are ours (levelselect.h). */

#include "scenetexture.h"
#include "renderdevice.h"

/* The menu's nine textures and the panel quad the score overlay shares. */
extern SceneTexture g_menuTex4;
extern SceneTexture g_menuTex1;
extern SceneTexture g_menuTexKnob;
extern SceneTexture g_menuTexSelector;
extern ScreenVertex g_panelQuad[4];
extern SceneTexture g_menuTexScale;
extern SceneTexture g_menuTexOn;
extern SceneTexture g_menuTex2;
extern SceneTexture g_menuTex3;
extern SceneTexture g_menuTexOff;

class Game;
class RenderDevice;
class TextRenderer;

/* The full-screen backdrop every menu screen opens with. */
extern "C" __declspec(dllexport) void __cdecl
Menu_DrawBackdrop(RenderDevice *d3d, void *theme);

/* The two animated markers either side of the highlighted row.  rowOffset
 * shifts them, in 640-wide screen units (0 on the main menu). */
extern "C" __declspec(dllexport) void __cdecl
Menu_DrawCursorMarkers(Game *g, RenderDevice *d3d, DWORD ms, float rowOffset);

/* Node 0: the main menu. */
extern "C" __declspec(dllexport) void __cdecl
Menu_RenderMainMenu(Game *g, void *theme, RenderDevice *d3d, TextRenderer *text,
                    DWORD ms);

/* Node 4: Controls, Video, Audio. */
extern "C" __declspec(dllexport) void __cdecl
Menu_RenderOptionsMenu(Game *g, void *theme, RenderDevice *d3d, TextRenderer *text,
                       DWORD ms);

/* Node 2, Load Game: the save-slot names. */
extern "C" __declspec(dllexport) void __cdecl
Menu_RenderRestoreSlotList(Game *g, void *theme, RenderDevice *d3d,
                           TextRenderer *text, DWORD ms);

/* Node 0x2a, Save Game (from the level-complete screen). */
extern "C" __declspec(dllexport) void __cdecl
Menu_RenderSaveSlotList(Game *g, void *theme, RenderDevice *d3d,
                        TextRenderer *text, DWORD ms);

/* Builds the static quads, the widget model and the nine textures the screens
 * draw with; once per device. */
extern "C" __declspec(dllexport) void __cdecl
Menu_BuildMenuGeometry(RenderDevice *d3d, const char *prefix);

/* Node 0xa: the key-binding page. */
extern "C" __declspec(dllexport) void __cdecl
Menu_RenderControlsRemap(Game *g, void *theme, RenderDevice *d3d,
                         TextRenderer *text, DWORD ms);

/* That page's own cursor markers. */
extern "C" __declspec(dllexport) void __cdecl
Menu_DrawControlsCursorMarkers(Game *g, RenderDevice *d3d, DWORD ms);

/* Node 0xb: the video options. */
extern "C" __declspec(dllexport) void __cdecl
Menu_RenderVideoOptions(Game *g, void *theme, RenderDevice *d3d,
                        TextRenderer *text, DWORD ms);

/* Node 0xc: the audio options. */
extern "C" __declspec(dllexport) void __cdecl
Menu_RenderSoundOptions(Game *g, void *theme, RenderDevice *d3d,
                        TextRenderer *text, DWORD ms);

/* Node 5: the credits; nowMs is the millisecond clock. */
extern "C" __declspec(dllexport) void __cdecl
Menu_RenderCreditsScroll(Game *game, RenderDevice *d3d, TextRenderer *text,
                         DWORD nowMs);

/* Picks and draws the screen for the current menu node. */
extern "C" __declspec(dllexport) void __cdecl
Menu_DispatchGameState(Game *g, void *theme, RenderDevice *d3d, TextRenderer *text,
                       DWORD ms);

/* A level-select theme page. */
void Menu_RenderLevelSelect(Game *g, void *theme, RenderDevice *d3d,
                            TextRenderer *text, DWORD ms);
