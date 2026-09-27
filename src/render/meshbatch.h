/* meshbatch.h -- the level's mesh-batch objects (meshbatch.cpp). */
#pragma once
class RenderDevice;
/* __cdecl(placements, theme block, d3d); one caller, RenderGameFrame. */
extern "C" __declspec(dllexport) void __cdecl
MeshBatch_Draw(void *ctx, void *game, RenderDevice *d3d);
