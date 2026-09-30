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

#define MESH_STRIDED_POSITION 0
#define MESH_STRIDED_NORMAL   1
#define MESH_STRIDED_TEX0     4
#define MESH_STRIDED_TEX1     5

/* One D3DDRAWPRIMITIVESTRIDEDDATA entry. */
struct MeshStridedEntry { void *lpvData; DWORD dwStride; };

class CFaktMesh {
public:
    /* Load `path` into `self`; the low byte of the result is the
     * success flag. */
    int importSceneModels(const char *path);

    /* The two draw exports (faktmesh.cpp). */
    HRESULT drawMeshBuffer(RenderDevice *dev, DWORD frame);

    HRESULT drawFramedModel(RenderDevice *dev, DWORD frame);

    /* ─── Lifecycle ────────────────────────────────────────────────────────
     *
     * The constructor sets the four strides and empties the buffers; the
     * destructor frees them with releaseModelBuffers.  model.cpp allocates
     * the four buffers with the allocator this frees them with. */
    CFaktMesh();
    virtual ~CFaktMesh();
    CFaktMesh(const CFaktMesh &) = delete;
    CFaktMesh &operator=(const CFaktMesh &) = delete;

    void releaseModelBuffers();

    void  *vertexData() const { return pVertexData_; }
    DWORD vertexCount() const { return dwVertexCount_; }
    void  *frameRecords() const { return pFrameRecords_; }
    WORD frameCount() const { return wFrameCount_; }
    char  *name() const { return pszName_; }

private:
    HRESULT drawMesh(RenderDevice *dev, DWORD frame, DWORD flags,
                     const char *name);

 

    void  *pVertexData_;    // +0x04 dwVertexCount * wFrameCount vertices, stride 0x28
    DWORD  dwVertexCount_;  // +0x08 vertices per animation frame
    void  *pFrameRecords_;  // +0x0c wFrameCount records of 0x18 bytes (6 dwords)
    WORD   wFrameCount_;    // +0x10 frame index clamps to 0 when >= this
    char  *pszName_;        // +0x12 strdup of the path, freed by ReleaseModelBuffers
    /* +0x16 D3DDRAWPRIMITIVESTRIDEDDATA: position, normal, diffuse,
     * specular, then textureCoords[8].  Only the four strides the ctor
     * writes are ever touched. */
    MeshStridedEntry strided_[12];
    void  *pScratchVerts_;  // +0x76 dwVertexCount vertices, stride 0x28, zeroed
};

/* The two character meshes, loaded once at startup (renderstate.cpp):
 * models\John.mdl and models\Enemy.mdl. */
extern CFaktMesh g_meshPlayer;
extern CFaktMesh g_meshEnemy;

