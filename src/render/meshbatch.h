/* meshbatch.h -- the level's mesh-batch objects (meshbatch.cpp). */
#pragma once
struct Direct3D;
/* __cdecl(placements, theme block, d3d); one caller, RenderGameFrame. */
extern "C" __declspec(dllexport) void __cdecl
Direct3D_DrawMeshBatch(void *ctx, void *game, Direct3D *d3d);
