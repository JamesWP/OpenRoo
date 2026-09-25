/* quadbatch.h -- DrawQuadBatch 0x00408710 (quadbatch.cpp). */
#pragma once
struct QuadVerts;
struct Direct3D;
/* __cdecl(placements, theme block, d3d); one caller, RenderGameFrame. */
extern "C" __declspec(dllexport) void __cdecl
Direct3D_DrawQuadBatch(QuadVerts *verts, void *game, Direct3D *d3d);
