/* The bridge surfaces' drawing (bridgesurf.cpp). */

#pragma once
class Game;
class RenderDevice;
class ThemeAssetBlock;

/* Draws every bridge's surface for the frame at time t, with the theme's
 * BRIDGE records.  Called once a frame by RenderGameFrame. */
void BridgeSurf_Draw(Game *game, ThemeAssetBlock *theme, RenderDevice *d3d,
                     double t);
