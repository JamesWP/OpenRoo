/* The bridge surfaces' drawing (bridgesurf.cpp). */

#pragma once
class BridgeObject;
class RenderDevice;
class ThemeAssetBlock;

/* What the drawing needs of the level: its bridges and whether specular
 * highlights are on (the video option). */
struct BridgeSurfSet {
    BridgeObject *const *slots;
    unsigned             count;
    bool                 highlights;
};

/* Draws every bridge's surface for the frame at time t, with the theme's
 * BRIDGE records.  Called once a frame by RenderGameFrame. */
void BridgeSurf_Draw(const BridgeSurfSet &bridges, ThemeAssetBlock *theme,
                     RenderDevice *d3d, double t);
