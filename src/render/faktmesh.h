#pragma once
#include <windows.h>
#include <stddef.h>
class RenderDevice;

/* CFaktMesh — the mesh object, drawn by DrawMeshBuffer / DrawFramedModel
 * (faktmesh.cpp) and loaded from a .mdl by ImportSceneModels (model.cpp).
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
 * Twelve {lpvData, dwStride} pairs, ending exactly at pScratchVerts.  The
 * constructor sets the strides of position, normal, texCoords[0] and
 * texCoords[1] to 0x28 -- the four components of the mesh FVF 0x212
 * (XYZ | NORMAL | TEX2) -- and nothing else.  Nothing draws through it.
 */
#pragma pack(push, 1)
struct CFaktMesh {
    void  *unknown00;      // +0x00 vtable, set by Init
    void  *pVertexData;    // +0x04 dwVertexCount * wFrameCount vertices, stride 0x28
    DWORD  dwVertexCount;  // +0x08 vertices per animation frame
    void  *pFrameRecords;  // +0x0c wFrameCount records of 0x18 bytes (6 dwords)
    WORD   wFrameCount;    // +0x10 frame index clamps to 0 when >= this
    char  *pszName;        // +0x12 strdup of the path, freed by ReleaseModelBuffers
    /* +0x16 D3DDRAWPRIMITIVESTRIDEDDATA: position, normal, diffuse,
     * specular, then textureCoords[8].  Only the four strides the ctor
     * writes are ever touched. */
    struct { void *lpvData; DWORD dwStride; } strided[12];
    void  *pScratchVerts;  // +0x76 dwVertexCount vertices, stride 0x28, zeroed
};

/* The two character meshes, loaded once at startup (renderstate.cpp):
 * models\John.mdl and models\Enemy.mdl. */
extern CFaktMesh g_meshPlayer;
extern CFaktMesh g_meshEnemy;
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

/* The two draw exports (faktmesh.cpp). */
extern "C" __declspec(dllexport) HRESULT __attribute__((thiscall))
FaktMesh_DrawMeshBuffer(CFaktMesh *self, RenderDevice *dev, DWORD frame);
extern "C" __declspec(dllexport) HRESULT __attribute__((thiscall))
FaktMesh_DrawFramedModel(CFaktMesh *self, RenderDevice *dev, DWORD frame);

/* ─── The lifecycle four ───────────────────────────────────────────────────
 *
 *   Init                 the constructor
 *   ScalarDtor           scalar deleting destructor, the vtable's only slot
 *   DtorBody             destructor body
 *   ReleaseModelBuffers  free the four heap fields
 *
 * model.cpp allocates the four buffers with the allocator this frees them
 * with. */
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
