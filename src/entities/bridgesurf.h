/* The bridge surfaces' drawing (bridgesurf.cpp). */

#pragma once
class Game;
class RenderDevice;

/* Draws every bridge's surface for the frame at time t; lvl is the theme
 * block.  Called once a frame by RenderGameFrame. */
extern "C" __declspec(dllexport) void __cdecl
Direct3D_DrawBridgeSurfaces(Game *game, void *lvl, RenderDevice *d3d, double t);
