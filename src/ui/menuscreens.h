#pragma once
#include <windows.h>

/* The MenuScreens draw functions -- ENDGAME_PLAN.md Phase E6.
 *
 * DispatchGameState 0x0042e000 picks one per frame by menu node
 * (Game+0x195734); each draws its whole screen through TextRenderer and the
 * D3D proxy.  All but credits are called as
 *
 *   __cdecl(Game *g, void *theme, Direct3D *d3d, TextRenderer *text, DWORD ms)
 *
 * (DispatchGameState's own five arguments, forwarded -- Ghidra shows three
 * and loses the rest, as in scoreoverlay.cpp).  `theme` is the object whose
 * +0x6f8xx fields are the theme file's colours and the backdrop texture;
 * unmapped, so read at literal offsets.  Credits alone drops `theme`.
 *
 * Replaced so far:
 *
 *   0x0042df80  DrawMenuBackdrop        shared    Menu_DrawBackdrop
 *   0x00437390  DrawMenuCursorMarkers   shared    Menu_DrawCursorMarkers
 *   0x0042e190  RenderMainMenu          node 0    Menu_RenderMainMenu
 *   0x0042e710  RenderRestoreSlotList   node 2    Menu_RenderRestoreSlotList
 *   0x0042e500  RenderOptionsMenu       node 4    Menu_RenderOptionsMenu
 *   0x0042e880  RenderSaveSlotList      node 0x2a Menu_RenderSaveSlotList
 *   0x00433dc0  RenderLevelComplete     node 0x28 Menu_RenderLevelComplete
 *               (in scoreoverlay.cpp: its body is DrawGameOverScore's)
 *   0x00436550  RenderCreditsScroll     node 5    Menu_RenderCreditsScroll
 */

class Game;
struct Direct3D;
class TextRenderer;

/* 0x0042df80 -- the full-screen backdrop quad every menu screen opens with. */
extern "C" __declspec(dllexport) void __cdecl
Menu_DrawBackdrop(Direct3D *d3d, void *theme);

/* 0x00437390 -- the two animated markers either side of the highlighted row.
 * rowOffset shifts the column in 640-space (0 on the main menu). */
extern "C" __declspec(dllexport) void __cdecl
Menu_DrawCursorMarkers(Game *g, Direct3D *d3d, DWORD ms, float rowOffset);

/* 0x0042e190 -- menu node 0. */
extern "C" __declspec(dllexport) void __cdecl
Menu_RenderMainMenu(Game *g, void *theme, Direct3D *d3d, TextRenderer *text,
                    DWORD ms);

/* 0x0042e500 -- menu node 4: Controls / Video / Audio. */
extern "C" __declspec(dllexport) void __cdecl
Menu_RenderOptionsMenu(Game *g, void *theme, Direct3D *d3d, TextRenderer *text,
                       DWORD ms);

/* 0x0042e710 -- menu node 2, Load Game: the save-slot names. */
extern "C" __declspec(dllexport) void __cdecl
Menu_RenderRestoreSlotList(Game *g, void *theme, Direct3D *d3d,
                           TextRenderer *text, DWORD ms);

/* 0x0042e880 -- menu node 0x2a, Save Game (level-complete "Save"). */
extern "C" __declspec(dllexport) void __cdecl
Menu_RenderSaveSlotList(Game *g, void *theme, Direct3D *d3d,
                        TextRenderer *text, DWORD ms);

/* 0x00436550 -- cdecl; DispatchGameState passes the millisecond clock. */
extern "C" __declspec(dllexport) void __cdecl
Menu_RenderCreditsScroll(Game *game, Direct3D *d3d, TextRenderer *text,
                         DWORD nowMs);
