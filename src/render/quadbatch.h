/* quadbatch.h -- the level's quad-batch objects (quadbatch.cpp). */
#pragma once
struct LevelPlacements;
class ThemeAssetBlock;
class RenderDevice;
/* Draws the wall strips with the theme's SIDE records; one caller,
 * RenderGameFrame. */
void QuadBatch_Draw(const LevelPlacements *pl, const ThemeAssetBlock *theme,
                    RenderDevice *d3d);
