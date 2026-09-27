/* meshbatch.h -- the level's mesh-batch objects (meshbatch.cpp). */
#pragma once
struct LevelPlacements;
class ThemeAssetBlock;
class RenderDevice;
/* Draws the theme's PLATE records over the TILE_KIND_01 cells; one caller,
 * RenderGameFrame. */
void MeshBatch_Draw(const LevelPlacements *pl, const ThemeAssetBlock *theme,
                    RenderDevice *d3d);
