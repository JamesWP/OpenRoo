/* AnimatedMesh: the two draws and the lifecycle (animatedmesh.h).
 *
 * Both draws clamp the 16-bit frame index against wFrameCount, index into the
 * flat per-frame vertex array, and issue one DrawPrimitive(TRIANGLELIST);
 * DrawMeshBuffer passes flags 0x08, DrawFramedModel 0x18.
 *
 * KAROO_FAKTMESH_FX=half draws only the first half of each mesh's triangles.
 */

#include <algorithm>
#include <strings.h>
#include <atomic>
#include <stdio.h>
#include <stdint.h>
#include "animatedmesh.h"
#include "sysdev.h"
#include <fstream>
#include "binio.h"
#include <string.h>
#include "logger.h"
#include "gamestr.h"
#include "renderdevice.h"
AnimatedMesh g_meshEnemy;
AnimatedMesh g_meshPlayer;

#define MESH_FVF        VertexFormat::Normal2  // XYZ | NORMAL | TEX2
#define MESH_LOG_FIRST  8
#define MDL_VERTEX_STRIDE sizeof(MeshVertex)

static bool fx_half(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (sysdev::getEnv("KAROO_FAKTMESH_FX", buf, sizeof(buf)))
            cached = (strcasecmp(buf, "half") == 0);
        g_logger.write("animatedmesh: FX mode = %s\n", cached ? "half" : "off");
    }
    return cached != 0;
}

/* KAROO_MESH_DIAG=1: dump the pipeline state the first time a mesh is drawn.
 * The mesh FVF carries no vertex colour, so a tinted mesh is being coloured by
 * the lighting/material/texture state, none of which this file sets. */
static void mesh_diag(RenderDevice *dev, uint32_t flags)
{
    static std::atomic<long> once = 0;
    char buf[8];
    if (!sysdev::getEnv("KAROO_MESH_DIAG", buf, sizeof(buf)) || buf[0] == '0')
        return;
    if (once.exchange(1) != 0)
        return;

    g_logger.write("diag: drawflags=%02lX\n", flags);
    dev->LogState("diag");
}

long AnimatedMesh::drawMesh(RenderDevice *dev, uint32_t frame,
                         uint32_t flags, const char *name)
{
    mesh_diag(dev, flags);

    frame &= 0xffff;
    if (frame >= wFrameCount_)
        frame = 0;
    uint32_t count = this->vertexCount();
    if (fx_half())
        count = (count / 2 / 3) * 3;  // keep it a whole number of triangles

    if (!vb_ && !vertexData_.empty()) {
        vb_ = dev->CreateVertexBuffer(MESH_FVF, (uint32_t)vertexData_.size(),
                                      BufferUsage::Dynamic, vertexData_.data());
        dirtyFirst_ = dirtyEnd_ = 0;
    }
    if (vb_ && dirtyEnd_ > dirtyFirst_) {
        dev->UpdateVertexBuffer(vb_, dirtyFirst_, vertexData_.data() + dirtyFirst_,
                                dirtyEnd_ - dirtyFirst_);
        dirtyFirst_ = dirtyEnd_ = 0;
    }
    long hr = vb_ && dev->DrawBuffer(Prim::TriangleList, vb_,
                                     frame * this->vertexCount(), count, flags)
               ? 0 : (long)0x80004005u;  // S_OK : E_FAIL

    static std::atomic<long> logged = 0;
    if (++logged <= MESH_LOG_FIRST)
        g_logger.write("animatedmesh: %s this=%p dev=%p frame=%lu count=%lu flags=%02lX -> hr=%08lX\n",
                  name, this, dev, frame, count, flags, hr);
    return hr;
}

/* ─── Exports ───────────────────────────────────────────────────────────────
 */
long AnimatedMesh::drawMeshBuffer(RenderDevice *dev, uint32_t frame)
{
    return drawMesh(dev, frame, 0, "DrawMeshBuffer");
}

long AnimatedMesh::drawFramedModel(RenderDevice *dev, uint32_t frame)
{
    return drawMesh(dev, frame, DrawFlag::NoLight,
                     "DrawFramedModel");
}
  //  

/* ─── Lifecycle ────────────────────────────────────────────────────
 */

/* PRESERVED: wFrameCount goes to 1, not 0, so an empty mesh claims one frame.
 */
void AnimatedMesh::releaseModelBuffers()
{
    RenderDevice::DestroyVertexBuffer(vb_);
    vb_ = nullptr;
    dirtyFirst_ = dirtyEnd_ = 0;
    std::vector<MeshVertex>().swap(vertexData_);
    std::vector<FrameRecord>().swap(frameRecords_);
    std::string().swap(pszName_);
    dwVertexCount_ = 0;
    wFrameCount_   = 1;
}

void AnimatedMesh::touchVertices(uint32_t first, uint32_t count)
{
    if (!vb_ || count == 0 || first >= vertexData_.size())
        return;
    const uint32_t end = (uint32_t)std::min<size_t>(vertexData_.size(), (size_t)first + count);
    if (dirtyEnd_ == dirtyFirst_) {
        dirtyFirst_ = first;
        dirtyEnd_   = end;
    } else {
        dirtyFirst_ = std::min(dirtyFirst_, first);
        dirtyEnd_   = std::max(dirtyEnd_, end);
    }
}

/* Only the four strides are set; the other eight strided entries and every
 * lpvData are left uninitialised.  PRESERVED: wFrameCount starts at 1. */
AnimatedMesh::AnimatedMesh()
{
    strided_[MESH_STRIDED_POSITION].dwStride = MDL_VERTEX_STRIDE;
    strided_[MESH_STRIDED_NORMAL].dwStride   = MDL_VERTEX_STRIDE;
    strided_[MESH_STRIDED_TEX0].dwStride     = MDL_VERTEX_STRIDE;
    strided_[MESH_STRIDED_TEX1].dwStride     = MDL_VERTEX_STRIDE;

    dwVertexCount_  = 0;
    wFrameCount_    = 1;

    static std::atomic<long> logged = 0;
    if (++logged <= MESH_LOG_FIRST)
        g_logger.write("animatedmesh: Init this=%p\n", this);
}

AnimatedMesh::~AnimatedMesh()
{
    releaseModelBuffers();
}
  //  

/* The .mdl reader, ImportSceneModels.
 *
 * FORMAT: the .mdl file.
 *   +0x00  uint16_t   frameCount     -> wFrameCount_
 *   +0x02  uint32_t  vertexCount    -> dwVertexCount_
 *   then, for each frame f:
 *          6 x uint32_t             -> frameRecords_[f]
 *          for each vertex v:
 *              10 x uint32_t        -> vertexData_[f*vertexCount + v]
 * Opened "rb".  Every read is a separate 4-byte fread; the header's two are 2
 * and 4 bytes.
 *
 * KAROO_MDL_FX=scale halves every vertex position as it is read, so every
 * model comes out at half size. */

#define MDL_FRAME_REC_SIZE 0x18
#define MDL_VERTEX_SIZE    0x28
#define MDL_LOG_FIRST      8

static bool fx_scale(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (sysdev::getEnv("KAROO_MDL_FX", buf, sizeof(buf)))
            cached = (strcasecmp(buf, "scale") == 0);
        g_logger.write("model: FX mode = %s\n", cached ? "scale" : "off");
    }
    return cached != 0;
}

static unsigned long fnv1a(const void *p, unsigned len)
{
    const unsigned char *b = (const unsigned char *)p;
    unsigned long h = 2166136261UL;
    if (b == NULL) return 0;
    for (unsigned i = 0; i < len; i++) { h ^= b[i]; h *= 16777619UL; }
    return h;
}

int AnimatedMesh::importSceneModels(const char *path)
{
    unsigned frames, verts, total;
    static int logged = 0;

    // PRESERVED: the release runs before the open, so a failed open leaves the
    // mesh cleared, not unchanged.
    releaseModelBuffers();

    std::ifstream in(path, std::ios::binary);
    if (!in)
        return 0;

    // PRESERVED: no read is checked, and neither is the file's size: a
    // truncated .mdl leaves the rest of the vertices zero and still returns 1.
    // A NULL allocation is stored and then read into.
    readBytes(in, &wFrameCount_,   2);
    readBytes(in, &dwVertexCount_, 4);

    frames = wFrameCount_;
    verts  = dwVertexCount_;
    total  = frames * verts;

    frameRecords_.assign(frames, FrameRecord{});
    vertexData_.assign(total, MeshVertex{});

    for (unsigned f = 0; f < (frames & 0xffff); f++) {
        unsigned char *rec = (unsigned char *)&frameRecords_[f];
        for (int i = 0; i < 6; i++)
            readBytes(in, rec + i * 4, 4);

        for (unsigned v = 0; v < dwVertexCount_; v++) {
            unsigned char *vert = (unsigned char *)&vertexData_[f * dwVertexCount_ + v];
            for (int i = 0; i < 10; i++)
                readBytes(in, vert + i * 4, 4);

            if (fx_scale()) {
                ((float *)vert)[0] *= 0.5f;
                ((float *)vert)[1] *= 0.5f;
                ((float *)vert)[2] *= 0.5f;
            }
        }
    }

    pszName_ = path;

    // KAROO_MDL_DUMP=<path>: one line per load, the two heap buffers hashed
    // (FNV-1a 32), to compare an independent parse of the same .mdl against
    // what landed in memory.
    {
        const std::string dump = sysdev::getEnv("KAROO_MDL_DUMP");
        if (!dump.empty()) {
            std::ofstream h(dump, std::ios::binary | std::ios::app);
            if (h) {
                char line[512];
                int n = snprintf(line, sizeof(line), "%s frames=%u verts=%u rec=%08lx vtx=%08lx\r\n",
                                  path, frames, verts,
                                  fnv1a(frameRecords_.data(), frames * MDL_FRAME_REC_SIZE),
                                  fnv1a(vertexData_.data(), total  * MDL_VERTEX_SIZE));
                h.write(line, n);
            }
        }
    }

    if (logged < MDL_LOG_FIRST) {
        logged++;
        g_logger.write("model: '%s' frames=%u verts=%u\n", path, frames, verts);
    }
    return 1;
}
