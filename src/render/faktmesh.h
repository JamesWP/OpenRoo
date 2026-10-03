#pragma once
#include <stdint.h>
#include <stddef.h>
#include <array>
#include <string>
#include <vector>
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
struct MeshStridedEntry { void *lpvData; uint32_t dwStride; };

class CFaktMesh {
public:
    /* Load `path` into `self`; the low byte of the result is the
     * success flag. */
    int importSceneModels(const char *path);

    /* The two draw exports (faktmesh.cpp). */
    long drawMeshBuffer(RenderDevice *dev, uint32_t frame);

    long drawFramedModel(RenderDevice *dev, uint32_t frame);

    /* ─── Lifecycle ────────────────────────────────────────────────────────
     *
     * The constructor sets the four strides and empties the buffers; the
     * destructor releases them with releaseModelBuffers. */
    CFaktMesh();
    virtual ~CFaktMesh();
    CFaktMesh(const CFaktMesh &) = delete;
    CFaktMesh &operator=(const CFaktMesh &) = delete;

    void releaseModelBuffers();

    void  *vertexData() const { return const_cast<MeshVertex *>(vertexData_.data()); }
    uint32_t vertexCount() const { return dwVertexCount_; }
    void  *frameRecords() const { return const_cast<FrameRecord *>(frameRecords_.data()); }
    uint16_t frameCount() const { return wFrameCount_; }
    char  *name() { return &pszName_[0]; }
    const char *name() const { return pszName_.c_str(); }

private:
    long drawMesh(RenderDevice *dev, uint32_t frame, uint32_t flags,
                     const char *name);

 

    using FrameRecord = std::array<float, 6>;  // 0x18 bytes: the frame's bounding box

    std::vector<MeshVertex> vertexData_;  // dwVertexCount * wFrameCount vertices
    uint32_t  dwVertexCount_;  // +0x08 vertices per animation frame
    std::vector<FrameRecord> frameRecords_;  // wFrameCount records
    uint16_t   wFrameCount_;    // +0x10 frame index clamps to 0 when >= this
    std::string pszName_;   // the path
    /* +0x16 D3DDRAWPRIMITIVESTRIDEDDATA: position, normal, diffuse,
     * specular, then textureCoords[8].  Only the four strides the ctor
     * writes are ever touched. */
    MeshStridedEntry strided_[12];
};

/* The two character meshes, loaded once at startup (renderstate.cpp):
 * models\John.mdl and models\Enemy.mdl. */
extern CFaktMesh g_meshPlayer;
extern CFaktMesh g_meshEnemy;

