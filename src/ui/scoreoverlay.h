/* The score overlays (scoreoverlay.cpp).  Declared here for the callers
 * outside that file: our DispatchGameState (menuscreens.cpp). */
#pragma once
#include <windows.h>

class Game;
struct Direct3D;
class TextRenderer;

/* 0x00433dc0 -- menu node 0x28; `game` is DispatchGameState's theme argument,
 * as in the other screens.  Its body is DrawGameOverScore's. */
extern "C" __declspec(dllexport) void __cdecl
Menu_RenderLevelComplete(Game *g, void *game, Direct3D *d3d,
                         TextRenderer *text, DWORD ms);
