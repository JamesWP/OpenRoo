/* quadbatch.h -- the level's quad-batch objects (quadbatch.cpp). */
#pragma once
struct QuadVerts;
class RenderDevice;
/*(placements, theme block, d3d); one caller, RenderGameFrame. */
void QuadBatch_Draw(QuadVerts *verts, void *game, RenderDevice *d3d);
