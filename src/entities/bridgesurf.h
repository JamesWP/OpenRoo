/* The bridge surfaces' drawing (bridgesurf.cpp). */

#pragma once
class Game;
struct Direct3D;

/* Draws every bridge's surface for the frame at time t; lvl is the theme
 * block.  Called once a frame by RenderGameFrame. */
extern "C" __declspec(dllexport) void __cdecl
Direct3D_DrawBridgeSurfaces(Game *game, void *lvl, Direct3D *d3d, double t);
