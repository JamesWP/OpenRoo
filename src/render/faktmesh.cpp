/* CFaktMesh: the two draws and the lifecycle (faktmesh.h).
 *
 * Both draws clamp the 16-bit frame index against wFrameCount, index into the
 * flat per-frame vertex array, and issue one DrawPrimitive(TRIANGLELIST);
 * DrawMeshBuffer passes flags 0x08, DrawFramedModel 0x18.
 *
 * KAROO_FAKTMESH_FX=half draws only the first half of each mesh's triangles.
 */

#include "faktmesh.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "gamelog.h"
#include "gamestr.h"
#include <stdlib.h>
#include "log.h"
#include "renderdevice.h"
CFaktMesh g_meshEnemy;
CFaktMesh g_meshPlayer;

#define MESH_FVF        VertexFormat::Normal2  // XYZ | NORMAL | TEX2
#define MESH_LOG_FIRST  8
#define MDL_VERTEX_STRIDE 0x28

static bool fx_half(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (GetEnvironmentVariableA("KAROO_FAKTMESH_FX", buf, sizeof(buf)))
            cached = (lstrcmpiA(buf, "half") == 0);
        log_write("faktmesh: FX mode = %s\n", cached ? "half" : "off");
    }
    return cached != 0;
}

/* KAROO_MESH_DIAG=1: dump the pipeline state the first time a mesh is drawn.
 * The mesh FVF carries no vertex colour, so a tinted mesh is being coloured by
 * the lighting/material/texture state, none of which this file sets. */
static void mesh_diag(RenderDevice *dev, DWORD flags)
{
    static LONG once = 0;
    char buf[8];
    if (!GetEnvironmentVariableA("KAROO_MESH_DIAG", buf, sizeof(buf)) || buf[0] == '0')
        return;
    if (InterlockedExchange(&once, 1) != 0)
        return;

    log_write("diag: drawflags=%02lX\n", flags);
    dev->LogState("diag");
}

HRESULT CFaktMesh::drawMesh(RenderDevice *dev, DWORD frame,
                         DWORD flags, const char *name)
{
    mesh_diag(dev, flags);

    frame &= 0xffff;
    if (frame >= wFrameCount_)
        frame = 0;
    void *verts = (char *)pVertexData_ + frame * dwVertexCount_ * 0x28;
    DWORD count = dwVertexCount_;
    if (fx_half())
        count = (count / 2 / 3) * 3;  // keep it a whole number of triangles

    HRESULT hr = dev->Draw(Prim::TriangleList, MESH_FVF, verts, count, flags)
               ? S_OK : E_FAIL;

    static LONG logged = 0;
    if (InterlockedIncrement(&logged) <= MESH_LOG_FIRST)
        log_write("faktmesh: %s this=%p dev=%p frame=%lu count=%lu flags=%02lX -> hr=%08lX\n",
                  name, this, dev, frame, count, flags, hr);
    return hr;
}

/* ─── Exports ───────────────────────────────────────────────────────────────
 */
HRESULT CFaktMesh::drawMeshBuffer(RenderDevice *dev, DWORD frame)
{
    return drawMesh(dev, frame, DrawFlag::NoUpdateExtents, "DrawMeshBuffer");
}

HRESULT CFaktMesh::drawFramedModel(RenderDevice *dev, DWORD frame)
{
    return drawMesh(dev, frame, DrawFlag::NoUpdateExtents | DrawFlag::NoLight,
                     "DrawFramedModel");
}
  // extern "C"

/* ─── The lifecycle four ────────────────────────────────────────────────────
 */

static void *const g_FaktMeshVtable[1] = { (void *)&CFaktMesh::scalarDtor };

void *
CFaktMesh::vtbl(void)
{
    return (void *)g_FaktMeshVtable;
}

/* Four guarded frees, each followed by a NULL, then the two scalars.
 * PRESERVED: wFrameCount goes to 1, not 0, so an empty mesh claims one frame.
 */
void CFaktMesh::releaseModelBuffers()
{
    if (pVertexData_)   free(pVertexData_);
    pVertexData_ = NULL;
    if (pFrameRecords_) free(pFrameRecords_);
    pFrameRecords_ = NULL;
    if (pScratchVerts_) free(pScratchVerts_);
    pScratchVerts_ = NULL;
    if (pszName_)       free(pszName_);
    pszName_ = NULL;
    dwVertexCount_ = 0;
    wFrameCount_   = 1;
}

/* Only the four strides are set; the other eight strided entries and every
 * lpvData are left uninitialised.  PRESERVED: wFrameCount starts at 1. */
CFaktMesh *CFaktMesh::init()
{
    strided_[MESH_STRIDED_POSITION].dwStride = MDL_VERTEX_STRIDE;
    strided_[MESH_STRIDED_NORMAL].dwStride   = MDL_VERTEX_STRIDE;
    strided_[MESH_STRIDED_TEX0].dwStride     = MDL_VERTEX_STRIDE;
    strided_[MESH_STRIDED_TEX1].dwStride     = MDL_VERTEX_STRIDE;

    unknown00_      = CFaktMesh::vtbl();
    pVertexData_    = NULL;
    dwVertexCount_  = 0;
    pFrameRecords_  = NULL;
    wFrameCount_    = 1;
    pScratchVerts_  = NULL;
    pszName_        = NULL;

    static LONG logged = 0;
    if (InterlockedIncrement(&logged) <= MESH_LOG_FIRST)
        log_write("faktmesh: Init this=%p\n", this);
    return this;
}

/* Re-install the table, then release. */
void CFaktMesh::dtorBody()
{
    unknown00_ = CFaktMesh::vtbl();
    releaseModelBuffers();
}

/* The one vtable slot. */
void * 
CFaktMesh::scalarDtor(CFaktMesh *self, unsigned int flags)
{
    self->dtorBody();
    if (flags & 1)
        free(self);
    return self;
}
  // extern "C"

/* The .mdl reader, ImportSceneModels.
 *
 * FORMAT: the .mdl file.
 *   +0x00  WORD   frameCount     -> wFrameCount_
 *   +0x02  DWORD  vertexCount    -> dwVertexCount_
 *   then, for each frame f:
 *          6 x DWORD             -> pFrameRecords_ + f*0x18
 *          for each vertex v:
 *              10 x DWORD        -> pVertexData_ + (f*vertexCount + v)*0x28
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
        if (GetEnvironmentVariableA("KAROO_MDL_FX", buf, sizeof(buf)))
            cached = (lstrcmpiA(buf, "scale") == 0);
        log_write("model: FX mode = %s\n", cached ? "scale" : "off");
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

int CFaktMesh::importSceneModels(const char *path)
{
    FILE *fp;
    unsigned frames, verts, total;
    static int logged = 0;

    // PRESERVED: the release runs before the open, so a failed open leaves the
    // mesh cleared, not unchanged.
    releaseModelBuffers();

    fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;

    // PRESERVED: no read is checked, and neither is the file's size: a
    // truncated .mdl leaves the rest of the vertices zero and still returns 1.
    // A NULL allocation is stored and then read into.
    fread(&wFrameCount_,   2, 1, fp);
    fread(&dwVertexCount_, 4, 1, fp);

    frames = wFrameCount_;
    verts  = dwVertexCount_;
    total  = frames * verts;

    // PRESERVED: the frame records are not zero-filled, unlike the two vertex
    // buffers, so a record missing from the file reads as heap garbage.
    pFrameRecords_ = malloc(frames * MDL_FRAME_REC_SIZE);

    pVertexData_ = malloc(total * MDL_VERTEX_SIZE);
    if (pVertexData_ != NULL && total != 0)
        memset(pVertexData_, 0, total * MDL_VERTEX_SIZE);

    pScratchVerts_ = malloc(verts * MDL_VERTEX_SIZE);
    if (pScratchVerts_ != NULL && verts != 0)
        memset(pScratchVerts_, 0, verts * MDL_VERTEX_SIZE);

    for (unsigned f = 0; f < (frames & 0xffff); f++) {
        unsigned char *rec = (unsigned char *)pFrameRecords_
                           + f * MDL_FRAME_REC_SIZE;
        for (int i = 0; i < 6; i++)
            fread(rec + i * 4, 4, 1, fp);

        for (unsigned v = 0; v < dwVertexCount_; v++) {
            unsigned char *vert = (unsigned char *)pVertexData_
                                + (f * dwVertexCount_ + v) * MDL_VERTEX_SIZE;
            for (int i = 0; i < 10; i++)
                fread(vert + i * 4, 4, 1, fp);

            if (fx_scale()) {
                ((float *)vert)[0] *= 0.5f;
                ((float *)vert)[1] *= 0.5f;
                ((float *)vert)[2] *= 0.5f;
            }
        }
    }

    fclose(fp);

    // pszName is the path, freed by CFaktMesh::releaseModelBuffers.
    {
        unsigned n = (unsigned)strlen(path) + 1;
        char *name = (char *)malloc(n);
        pszName_ = name;
        if (name != NULL)
            memcpy(name, path, n);
    }

    // KAROO_MDL_DUMP=<path>: one line per load, the two heap buffers hashed
    // (FNV-1a 32), to compare an independent parse of the same .mdl against
    // what landed in memory.
    {
        char dump[MAX_PATH];
        if (GetEnvironmentVariableA("KAROO_MDL_DUMP", dump, sizeof(dump))) {
            HANDLE h = CreateFileA(dump, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                                   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                char line[512];
                int n = wsprintfA(line, "%s frames=%u verts=%u rec=%08lx vtx=%08lx\r\n",
                                  path, frames, verts,
                                  fnv1a(pFrameRecords_, frames * MDL_FRAME_REC_SIZE),
                                  fnv1a(pVertexData_,   total  * MDL_VERTEX_SIZE));
                DWORD w = 0;
                WriteFile(h, line, (DWORD)n, &w, NULL);
                CloseHandle(h);
            }
        }
    }

    if (logged < MDL_LOG_FIRST) {
        logged++;
        log_write("model: '%s' frames=%u verts=%u\n", path, frames, verts);
    }
    return 1;
}
