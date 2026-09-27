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
CFaktMesh g_meshEnemy;
CFaktMesh g_meshPlayer;

#define MESH_FVF        0x212  // XYZ | NORMAL | TEX2
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
static void mesh_diag(IDirect3DDevice3 *dev, DWORD flags)
{
    static LONG once = 0;
    char buf[8];
    if (!GetEnvironmentVariableA("KAROO_MESH_DIAG", buf, sizeof(buf)) || buf[0] == '0')
        return;
    if (InterlockedExchange(&once, 1) != 0)
        return;

    static const struct { D3DRENDERSTATETYPE rs; const char *name; } rstates[] = {
        { D3DRENDERSTATE_SHADEMODE,       "SHADEMODE"       },
        { D3DRENDERSTATE_SRCBLEND,        "SRCBLEND"        },
        { D3DRENDERSTATE_DESTBLEND,       "DESTBLEND"       },
        { D3DRENDERSTATE_TEXTUREMAPBLEND, "TEXTUREMAPBLEND" },
        { D3DRENDERSTATE_CULLMODE,        "CULLMODE"        },
        { D3DRENDERSTATE_ALPHABLENDENABLE,"ALPHABLENDENABLE"},
        { D3DRENDERSTATE_FOGENABLE,       "FOGENABLE"       },
        { D3DRENDERSTATE_FOGCOLOR,        "FOGCOLOR"        },
        { D3DRENDERSTATE_SPECULARENABLE,  "SPECULARENABLE"  },
        { D3DRENDERSTATE_COLORKEYENABLE,  "COLORKEYENABLE"  },
        { D3DRENDERSTATE_TEXTUREFACTOR,   "TEXTUREFACTOR"   },
        { D3DRENDERSTATE_AMBIENT,         "AMBIENT"         },
    };
    for (unsigned i = 0; i < sizeof rstates / sizeof rstates[0]; i++) {
        DWORD v = 0xdeadbeef;
        HRESULT hr = dev->GetRenderState(rstates[i].rs, &v);
        log_write("diag: rs %-17s = %08lX (hr=%08lX)\n", rstates[i].name, v, hr);
    }

    static const struct { D3DTEXTURESTAGESTATETYPE ts; const char *name; } tstates[] = {
        { D3DTSS_COLOROP,   "COLOROP"   },
        { D3DTSS_COLORARG1, "COLORARG1" },
        { D3DTSS_COLORARG2, "COLORARG2" },
        { D3DTSS_ALPHAOP,   "ALPHAOP"   },
        { D3DTSS_TEXCOORDINDEX, "TEXCOORDINDEX" },
    };
    for (unsigned i = 0; i < sizeof tstates / sizeof tstates[0]; i++) {
        DWORD v = 0xdeadbeef;
        HRESULT hr = dev->GetTextureStageState(0, tstates[i].ts, &v);
        log_write("diag: ts0 %-14s = %08lX (hr=%08lX)\n", tstates[i].name, v, hr);
    }

    DWORD lmat = 0xdeadbeef, lamb = 0xdeadbeef;
    HRESULT hr1 = dev->GetLightState(D3DLIGHTSTATE_MATERIAL, &lmat);
    HRESULT hr2 = dev->GetLightState(D3DLIGHTSTATE_AMBIENT,  &lamb);
    IDirect3DTexture2 *tex = NULL;
    HRESULT hr3 = dev->GetTexture(0, &tex);
    log_write("diag: lightstate MATERIAL=%08lX (hr=%08lX) AMBIENT=%08lX (hr=%08lX) "
              "tex0=%p (hr=%08lX) drawflags=%02lX\n",
              lmat, hr1, lamb, hr2, (void *)tex, hr3, flags);
}

static HRESULT draw_mesh(CFaktMesh *mesh, IDirect3DDevice3 *dev, DWORD frame,
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

    HRESULT hr = dev->DrawPrimitive(D3DPT_TRIANGLELIST, MESH_FVF, verts, count, flags);

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
FaktMesh_DrawMeshBuffer(CFaktMesh *self, IDirect3DDevice3 *dev, DWORD frame)
{
    return draw_mesh(self, dev, frame, 0x08, "DrawMeshBuffer");
}

__declspec(dllexport) HRESULT __attribute__((thiscall))
FaktMesh_DrawFramedModel(CFaktMesh *self, IDirect3DDevice3 *dev, DWORD frame)
{
    return draw_mesh(self, dev, frame, 0x18, "DrawFramedModel");
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
