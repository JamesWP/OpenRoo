#pragma once
#include <windows.h>
#include <stddef.h>
class RenderDevice;

/* CFaktMesh — the mesh object, drawn by DrawMeshBuffer / DrawFramedModel
 * (faktmesh.cpp) and loaded from a .mdl by ImportSceneModels (model.cpp).
 *
 * Vertex data is a flat array of MeshVertex, all animation frames
 * concatenated: frame f, vertex v is at index f * dwVertexCount + v
 *
 * strided is a D3DDRAWPRIMITIVESTRIDEDDATA: twelve {lpvData, dwStride}
 * pairs.  The constructor sets the strides of position, normal, texCoords[0]
 * and texCoords[1] to sizeof(MeshVertex) -- the four components of the mesh
 * FVF 0x212 (XYZ | NORMAL | TEX2) -- and nothing else.  Nothing draws through
 * it.
 */

/* One FVF 0x212 vertex: position, normal, two texture-coordinate sets. */
struct MeshVertex {
    float pos[3];
    float normal[3];
    float uv0[2];
    float uv1[2];
};
static_assert(sizeof(MeshVertex) == 0x28, "MeshVertex is the FVF 0x212 stride");

struct CFaktMesh {
    void  *unknown00;      // vtable, set by Init
    void  *pVertexData;    // dwVertexCount * wFrameCount MeshVertex
    DWORD  dwVertexCount;  // vertices per animation frame
    void  *pFrameRecords;  // wFrameCount records of 0x18 bytes (6 dwords)
    WORD   wFrameCount;    // frame index clamps to 0 when >= this
    char  *pszName;        // strdup of the path, freed by ReleaseModelBuffers
    /* D3DDRAWPRIMITIVESTRIDEDDATA: position, normal, diffuse,
     * specular, then textureCoords[8].  Only the four strides the ctor
     * writes are ever touched. */
    struct { void *lpvData; DWORD dwStride; } strided[12];
    void  *pScratchVerts;  // dwVertexCount MeshVertex, zeroed
};

/* The two character meshes, loaded once at startup (renderstate.cpp):
 * models\John.mdl and models\Enemy.mdl. */
extern CFaktMesh g_meshPlayer;
extern CFaktMesh g_meshEnemy;

/* The four strided entries the constructor gives a stride: position, normal,
 * textureCoords[0] and textureCoords[1] -- indices 0, 1, 4 and 5. */
#define MESH_STRIDED_POSITION 0
#define MESH_STRIDED_NORMAL   1
#define MESH_STRIDED_TEX0     4
#define MESH_STRIDED_TEX1     5

/* The two draw exports (faktmesh.cpp). */
HRESULT
FaktMesh_DrawMeshBuffer(CFaktMesh *self, RenderDevice *dev, DWORD frame);
HRESULT
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
CFaktMesh *FaktMesh_Init(CFaktMesh *self);

void *FaktMesh_ScalarDtor(CFaktMesh *self, unsigned int flags);

void FaktMesh_DtorBody(CFaktMesh *self);

void FaktMesh_ReleaseModelBuffers(CFaktMesh *self);

/* Our one-slot table, installed by Init and the destructor body. */
void *FaktMesh_Vtable(void);
