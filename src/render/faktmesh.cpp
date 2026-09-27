/* CFaktMesh: the two draws and the lifecycle (faktmesh.h).
 *
 * Both draws clamp the 16-bit frame index against wFrameCount, index into the
 * flat per-frame vertex array, and issue one DrawPrimitive(TRIANGLELIST);
 * DrawMeshBuffer passes flags 0x08, DrawFramedModel 0x18.
 *
 * KAROO_FAKTMESH_FX=half draws only the first half of each mesh's triangles.
 */

#include "faktmesh.h"
#include "com_proxy.h"
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

static HRESULT draw_mesh(CFaktMesh *mesh, RenderDevice *dev, DWORD frame,
                         DWORD flags, const char *name)
{
    mesh_diag(dev, flags);

    frame &= 0xffff;
    if (frame >= mesh->wFrameCount)
        frame = 0;
    void *verts = (char *)mesh->pVertexData + frame * mesh->dwVertexCount * 0x28;
    DWORD count = mesh->dwVertexCount;
    if (fx_half())
        count = (count / 2 / 3) * 3;  // keep it a whole number of triangles

    HRESULT hr = dev->Draw(Prim::TriangleList, MESH_FVF, verts, count, flags)
               ? S_OK : E_FAIL;

    static LONG logged = 0;
    if (InterlockedIncrement(&logged) <= MESH_LOG_FIRST)
        log_write("faktmesh: %s this=%p dev=%p frame=%lu count=%lu flags=%02lX -> hr=%08lX\n",
                  name, mesh, dev, frame, count, flags, hr);
    return hr;
}

/* ─── Exports ───────────────────────────────────────────────────────────────
 */
extern "C" {

__declspec(dllexport) HRESULT __attribute__((thiscall))
FaktMesh_DrawMeshBuffer(CFaktMesh *self, RenderDevice *dev, DWORD frame)
{
    return draw_mesh(self, dev, frame, DrawFlag::NoUpdateExtents, "DrawMeshBuffer");
}

__declspec(dllexport) HRESULT __attribute__((thiscall))
FaktMesh_DrawFramedModel(CFaktMesh *self, RenderDevice *dev, DWORD frame)
{
    return draw_mesh(self, dev, frame, DrawFlag::NoUpdateExtents | DrawFlag::NoLight,
                     "DrawFramedModel");
}

}  // extern "C"

/* ─── The lifecycle four ────────────────────────────────────────────────────
 */

extern "C" {

static void *const g_FaktMeshVtable[1] = { (void *)&FaktMesh_ScalarDtor };

__declspec(dllexport) void *FaktMesh_Vtable(void)
{
    return (void *)g_FaktMeshVtable;
}

/* Four guarded frees, each followed by a NULL, then the two scalars.
 * PRESERVED: wFrameCount goes to 1, not 0, so an empty mesh claims one frame.
 */
__declspec(dllexport) void __attribute__((thiscall))
FaktMesh_ReleaseModelBuffers(CFaktMesh *self)
{
    if (self->pVertexData)   free(self->pVertexData);
    self->pVertexData = NULL;
    if (self->pFrameRecords) free(self->pFrameRecords);
    self->pFrameRecords = NULL;
    if (self->pScratchVerts) free(self->pScratchVerts);
    self->pScratchVerts = NULL;
    if (self->pszName)       free(self->pszName);
    self->pszName = NULL;
    self->dwVertexCount = 0;
    self->wFrameCount   = 1;
}

/* Only the four strides are set; the other eight strided entries and every
 * lpvData are left uninitialised.  PRESERVED: wFrameCount starts at 1. */
__declspec(dllexport) CFaktMesh *__attribute__((thiscall))
FaktMesh_Init(CFaktMesh *self)
{
    self->strided[MESH_STRIDED_POSITION].dwStride = MDL_VERTEX_STRIDE;
    self->strided[MESH_STRIDED_NORMAL].dwStride   = MDL_VERTEX_STRIDE;
    self->strided[MESH_STRIDED_TEX0].dwStride     = MDL_VERTEX_STRIDE;
    self->strided[MESH_STRIDED_TEX1].dwStride     = MDL_VERTEX_STRIDE;

    self->unknown00      = FaktMesh_Vtable();
    self->pVertexData    = NULL;
    self->dwVertexCount  = 0;
    self->pFrameRecords  = NULL;
    self->wFrameCount    = 1;
    self->pScratchVerts  = NULL;
    self->pszName        = NULL;

    static LONG logged = 0;
    if (InterlockedIncrement(&logged) <= MESH_LOG_FIRST)
        log_write("faktmesh: Init this=%p\n", self);
    return self;
}

/* Re-install the table, then release. */
__declspec(dllexport) void __attribute__((thiscall))
FaktMesh_DtorBody(CFaktMesh *self)
{
    self->unknown00 = FaktMesh_Vtable();
    FaktMesh_ReleaseModelBuffers(self);
}

/* The one vtable slot. */
__declspec(dllexport) void *__attribute__((thiscall))
FaktMesh_ScalarDtor(CFaktMesh *self, unsigned int flags)
{
    FaktMesh_DtorBody(self);
    if (flags & 1)
        free(self);
    return self;
}

}  // extern "C"
