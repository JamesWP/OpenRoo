#pragma once
#include <windows.h>
#include <stddef.h>

/* CFaktMesh — the mesh object, drawn by DrawMeshBuffer / DrawFramedModel
 * (faktmesh.cpp) and loaded from a .mdl by ImportSceneModels (model.cpp).
 * Ghidra calls the same object LoadedModel on the loader side; it is one
 * struct, and this is the single definition both sides use.
 *
 * Layout from HOOKS.md § CFaktMesh, confirmed against the disassembly of the
 * two draw methods (0x437b40 / 0x437b80), of LoadedModel::Init (0x437b00),
 * of ImportSceneModels (0x437bc0) and of FreeThing2 (0x437fb0) -- the last of
 * which names every heap field by freeing it.
 *
 * PACKED: pszName sits at +0x12, immediately after the 16-bit wFrameCount, so
 * the struct cannot be naturally aligned.  Init and the loader both write it
 * at that offset.
 *
 * Vertex data is a flat array of 0x28-byte FVF 0x212 vertices, all animation
 * frames concatenated: frame f, vertex v is at
 *     pVertexData + (f * dwVertexCount + v) * 0x28
 */
#pragma pack(push, 1)
struct CFaktMesh {
    void  *unknown00;      // +0x00 vtable (&AutoClass3VTable, set by Init)
    void  *pVertexData;    // +0x04 dwVertexCount * wFrameCount vertices, stride 0x28
    DWORD  dwVertexCount;  // +0x08 vertices per animation frame
    void  *pFrameRecords;  // +0x0c wFrameCount records of 0x18 bytes (6 dwords)
    WORD   wFrameCount;    // +0x10 frame index clamps to 0 when >= this
    char  *pszName;        // +0x12 strdup of the path, freed by FreeThing2
    BYTE   reserved16[0x76 - 0x16];
    void  *pScratchVerts;  // +0x76 dwVertexCount vertices, stride 0x28, zeroed
};
#pragma pack(pop)

static_assert(offsetof(CFaktMesh, pVertexData)   == 0x04, "CFaktMesh layout mismatch");
static_assert(offsetof(CFaktMesh, dwVertexCount) == 0x08, "CFaktMesh layout mismatch");
static_assert(offsetof(CFaktMesh, pFrameRecords) == 0x0c, "CFaktMesh layout mismatch");
static_assert(offsetof(CFaktMesh, wFrameCount)   == 0x10, "CFaktMesh layout mismatch");
static_assert(offsetof(CFaktMesh, pszName)       == 0x12, "CFaktMesh layout mismatch");
static_assert(offsetof(CFaktMesh, pScratchVerts) == 0x76, "CFaktMesh layout mismatch");

/* The two draw exports (faktmesh.cpp), for callers to include rather than
 * redeclare (COHESION_PLAN.md template 10). */
struct IDirect3DDevice3;
extern "C" __declspec(dllexport) HRESULT __attribute__((thiscall))
FaktMesh_DrawMeshBuffer(CFaktMesh *self, IDirect3DDevice3 *dev, DWORD frame);
extern "C" __declspec(dllexport) HRESULT __attribute__((thiscall))
FaktMesh_DrawFramedModel(CFaktMesh *self, IDirect3DDevice3 *dev, DWORD frame);
