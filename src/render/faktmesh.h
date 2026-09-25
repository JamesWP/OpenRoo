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
 *
 * ─── The 0x60 bytes at +0x16 are a D3DDRAWPRIMITIVESTRIDEDDATA ───────────
 *
 * They were `reserved16` until the constructor 0x00437ad0 was read.  It sets
 * four dwords -- +0x1a, +0x22, +0x3a, +0x42 -- to 0x28 and nothing else in
 * the region, and those are exactly the `dwStride` fields of the position,
 * normal, texCoords[0] and texCoords[1] entries of a
 * D3DDRAWPRIMITIVESTRIDEDDATA laid out from +0x16: twelve {lpvData, dwStride}
 * pairs of 8 bytes each, 0x16 + 0x60 = 0x76, which lands exactly on
 * pScratchVerts.  Four strided components, and they are the four the mesh
 * FVF 0x212 (XYZ | NORMAL | TEX2) actually has -- the arithmetic and the FVF
 * agree, which is the cross-check CLAUDE.md asks for rather than a guess.
 *
 * Nothing in our tree draws through it yet; the region is named so that the
 * constructor's four stores are stores to something rather than to a gap.
 */
#pragma pack(push, 1)
struct CFaktMesh {
    void  *unknown00;      // +0x00 vtable (&AutoClass3VTable, set by Init)
    void  *pVertexData;    // +0x04 dwVertexCount * wFrameCount vertices, stride 0x28
    DWORD  dwVertexCount;  // +0x08 vertices per animation frame
    void  *pFrameRecords;  // +0x0c wFrameCount records of 0x18 bytes (6 dwords)
    WORD   wFrameCount;    // +0x10 frame index clamps to 0 when >= this
    char  *pszName;        // +0x12 strdup of the path, freed by FreeThing2
    /* +0x16 D3DDRAWPRIMITIVESTRIDEDDATA: position, normal, diffuse,
     * specular, then textureCoords[8].  Only the four strides the ctor
     * writes are ever touched by the game. */
    struct { void *lpvData; DWORD dwStride; } strided[12];
    void  *pScratchVerts;  // +0x76 dwVertexCount vertices, stride 0x28, zeroed
};

/* The two character meshes, loaded once at startup (renderstate.cpp):
 * models\John.mdl and models\Enemy.mdl. */
extern CFaktMesh g_meshPlayer;   /* was 0x0046c7b0 */
extern CFaktMesh g_meshEnemy;   /* was 0x004e0310 */
#pragma pack(pop)

static_assert(sizeof(CFaktMesh) == 0x7a, "CFaktMesh size: ModelManager operator new(0x7a)");
static_assert(offsetof(CFaktMesh, pVertexData)   == 0x04, "CFaktMesh layout mismatch");
static_assert(offsetof(CFaktMesh, dwVertexCount) == 0x08, "CFaktMesh layout mismatch");
static_assert(offsetof(CFaktMesh, pFrameRecords) == 0x0c, "CFaktMesh layout mismatch");
static_assert(offsetof(CFaktMesh, wFrameCount)   == 0x10, "CFaktMesh layout mismatch");
static_assert(offsetof(CFaktMesh, pszName)       == 0x12, "CFaktMesh layout mismatch");
static_assert(offsetof(CFaktMesh, strided)       == 0x16, "CFaktMesh layout mismatch");
static_assert(offsetof(CFaktMesh, pScratchVerts) == 0x76, "CFaktMesh layout mismatch");

/* The four strided entries the constructor gives a stride: position, normal,
 * textureCoords[0] and textureCoords[1] -- indices 0, 1, 4 and 5. */
#define MESH_STRIDED_POSITION 0
#define MESH_STRIDED_NORMAL   1
#define MESH_STRIDED_TEX0     4
#define MESH_STRIDED_TEX1     5
static_assert(offsetof(CFaktMesh, strided[MESH_STRIDED_POSITION].dwStride) == 0x1a, "strided");
static_assert(offsetof(CFaktMesh, strided[MESH_STRIDED_NORMAL].dwStride)   == 0x22, "strided");
static_assert(offsetof(CFaktMesh, strided[MESH_STRIDED_TEX0].dwStride)     == 0x3a, "strided");
static_assert(offsetof(CFaktMesh, strided[MESH_STRIDED_TEX1].dwStride)     == 0x42, "strided");

/* The two draw exports (faktmesh.cpp), for callers to include rather than
 * redeclare (COHESION_PLAN.md template 10). */
struct IDirect3DDevice3;
extern "C" __declspec(dllexport) HRESULT __attribute__((thiscall))
FaktMesh_DrawMeshBuffer(CFaktMesh *self, IDirect3DDevice3 *dev, DWORD frame);
extern "C" __declspec(dllexport) HRESULT __attribute__((thiscall))
FaktMesh_DrawFramedModel(CFaktMesh *self, IDirect3DDevice3 *dev, DWORD frame);

/* ─── The lifecycle four (ENDGAME_PLAN.md E2) ────────────────────────────
 *
 *   0x00437ad0  Init            the constructor
 *   0x00437b10  scalar deleting destructor -- vtable 0x0045d694's only slot
 *   0x00437b30  destructor body
 *   0x00437fb0  ReleaseModelBuffers -- free the four heap fields
 *
 * The table is ours: the literal 0x0045d694 occurs in exactly two places in
 * the image, 0x437ae7 and 0x437b32, i.e. the constructor and the destructor
 * body themselves, and it has one slot (0x0045d698 starts CvtSyms').
 *
 * The four buffers stay on the GAME heap -- model.cpp allocates them with
 * the game's operator new precisely because this function frees them, and
 * that pairing is the whole reason alloc.h still exists here. */
extern "C" __declspec(dllexport) CFaktMesh *__attribute__((thiscall))
FaktMesh_Init(CFaktMesh *self);

extern "C" __declspec(dllexport) void *__attribute__((thiscall))
FaktMesh_ScalarDtor(CFaktMesh *self, unsigned int flags);

extern "C" __declspec(dllexport) void __attribute__((thiscall))
FaktMesh_DtorBody(CFaktMesh *self);

extern "C" __declspec(dllexport) void __attribute__((thiscall))
FaktMesh_ReleaseModelBuffers(CFaktMesh *self);

/* Our one-slot table, installed by Init and the destructor body. */
extern "C" __declspec(dllexport) void *FaktMesh_Vtable(void);
