/* quadbatch.h -- the level's quad-batch objects (quadbatch.cpp). */
#pragma once
struct QuadVerts;
class RenderDevice;
/* __cdecl(placements, theme block, d3d); one caller, RenderGameFrame. */
extern "C" __declspec(dllexport) void __cdecl
QuadBatch_Draw(QuadVerts *verts, void *game, RenderDevice *d3d);
