/* bridgesurf.h -- DrawBridgeSurfaces (bridgesurf.cpp). */
#pragma once
class Game;
struct Direct3D;
/* __cdecl(game, theme block, d3d, double now); one caller, RenderGameFrame. */
extern "C" __declspec(dllexport) void __cdecl
Direct3D_DrawBridgeSurfaces(Game *game, void *lvl, Direct3D *d3d, double t);
