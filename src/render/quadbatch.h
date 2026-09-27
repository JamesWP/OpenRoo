/* quadbatch.h -- the level's quad-batch objects (quadbatch.cpp). */
#pragma once
struct QuadVerts;
struct RenderDevice;
/* __cdecl(placements, theme block, d3d); one caller, RenderGameFrame. */
extern "C" __declspec(dllexport) void __cdecl
Direct3D_DrawQuadBatch(QuadVerts *verts, void *game, RenderDevice *d3d);
