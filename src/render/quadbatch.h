/* quadbatch.h -- the level's quad-batch objects (quadbatch.cpp). */
#pragma once
struct LevelPlacements;
class ThemeAssetBlock;
class RenderDevice;
void QuadBatch_Draw(const LevelPlacements *pl, const ThemeAssetBlock *theme,
                    RenderDevice *d3d);
