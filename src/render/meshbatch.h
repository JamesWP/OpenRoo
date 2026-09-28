/* meshbatch.h -- the level's mesh-batch objects (meshbatch.cpp). */
#pragma once
class RenderDevice;
/*  (placements, theme block, d3d); one caller, RenderGameFrame. */
  void  
MeshBatch_Draw(void *ctx, void *game, RenderDevice *d3d);
