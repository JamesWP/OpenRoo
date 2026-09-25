/* CFaktMesh draw-path reimplementation.
 *
 * Replaces the two ~55-byte originals (safety-stubbed by patch.py):
 *   0x437b40 CFaktMesh::DrawMeshBuffer  — DrawPrimitive(..., dwFlags=0x08)
 *   0x437b80 CFaktMesh::DrawFramedModel — DrawPrimitive(..., dwFlags=0x18)
 *
 * Both clamp the 16-bit frame index against wFrameCount, index into the flat
 * per-frame vertex array (stride 0x28 = FVF 0x212), and issue one
 * IDirect3DDevice3::DrawPrimitive(D3DPT_TRIANGLELIST, ...).  The device
 * pointer passed by the game is the com_proxy device proxy; calling through
 * it (rather than the raw Wine object) is intentional — the DrawPrimitive
 * forwarder unwraps for us and keeps all traffic visible to the proxy layer.
 *
 * KAROO_FAKTMESH_FX=half draws only the first half of each mesh's triangles,
 * as visual proof the pixels come from this reimplementation.
 *
 * ─── The lifecycle four (ENDGAME_PLAN.md E2, 2026-09-20) ─────────────────
 *
 *   0x00437ad0  Init                 constructor
 *   0x00437b10  scalar deleting dtor vtable slot 0, no code reference
 *   0x00437b30  destructor body      re-install the table, then release
 *   0x00437fb0  ReleaseModelBuffers  free the four heap fields
 *
 * These close the two LoadedModel TU rows and end model.cpp's private copy
 * of the free path: `model_release` was an inline of 0x437fb0 kept in step
 * by hand, and a copy kept in step by hand is a bug waiting for the next
 * field.  model.cpp now calls FaktMesh_ReleaseModelBuffers through this
 * header, which is the owner rule doing its job.
 *
 * WHAT DOES NOT CHANGE IS THE HEAP.  The four buffers are the game's --
 * model.cpp allocates them with the game's operator new precisely because
 * 0x437fb0 frees them with FactAlloc::Free2, and now that the free is ours
 * the pairing is merely visible rather than implicit.  Moving both sides to
 * our heap is a separate decision: 0x004386f7, still game code, calls the
 * release on a mesh we did not necessarily fill.
 *
 * Quirks preserved, both from the listing:
 *   - Init sets wFrameCount to ONE, not zero, and so does the release.  A
 *     mesh that has never been loaded therefore claims one frame.
 *   - Init writes the four strided-data strides FIRST, before the vtable,
 *     and never touches the other eight entries or their lpvData pointers --
 *     they are left uninitialised, which for an embedded mesh means whatever
 *     the container had there.
 */
#include "faktmesh.h"
#include "com_proxy.h"
#include <stdlib.h>
#include "log.h"
CFaktMesh g_meshEnemy;   /* was 0x004e0310 */
CFaktMesh g_meshPlayer;   /* was 0x0046c7b0 */

#define MESH_FVF        0x212  /* XYZ | NORMAL | TEX2 — 0x28-byte stride */
#define MESH_LOG_FIRST  8
#define MDL_VERTEX_STRIDE 0x28   /* the FVF 0x212 stride the ctor writes */

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

/* KAROO_MESH_DIAG=1 — dump the pipeline state the first time a mesh is drawn.
 *
 * The mesh FVF (0x212 = XYZ|NORMAL|TEX2) carries no vertex colour, so a mesh
 * that comes out tinted is being coloured by the lighting/material/texture
 * state, none of which this file sets.  Rather than theorise about which, read
 * them all off one unattended run. */
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
        count = (count / 2 / 3) * 3;  /* keep it a whole number of triangles */

    HRESULT hr = dev->DrawPrimitive(D3DPT_TRIANGLELIST, MESH_FVF, verts, count, flags);

    static LONG logged = 0;
    if (InterlockedIncrement(&logged) <= MESH_LOG_FIRST)
        log_write("faktmesh: %s this=%p dev=%p frame=%lu count=%lu flags=%02lX -> hr=%08lX\n",
                  name, mesh, dev, frame, count, flags, hr);
    return hr;
}

/* ─── Exports — thiscall wrappers, redirected to by patch.py CALL_PATCHES ─── */
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

} // extern "C"

/* ─── The lifecycle four ─────────────────────────────────────────────────
 *
 * The vtable is ours.  0x0045d694 has ONE slot and the literal occurs in
 * exactly two places in the image -- 0x437ae7 and 0x437b32, the two
 * functions below -- so nothing else installs it and the game's table is
 * left holding a UD2 as a tripwire (faktmesh.h). */

extern "C" {

static void *const g_FaktMeshVtable[1] = { (void *)&FaktMesh_ScalarDtor };

__declspec(dllexport) void *FaktMesh_Vtable(void)
{
    return (void *)g_FaktMeshVtable;
}

/* 0x00437fb0.  Four guarded frees, each followed by a NULL, then the two
 * scalars.  wFrameCount goes to 1 (see the header). */
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

/* 0x00437ad0.  Written in the original's order because the order is the
 * evidence: the four strides come first, and they are what identified the
 * 0x60 bytes at +0x16 as a D3DDRAWPRIMITIVESTRIDEDDATA. */
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

/* 0x00437b30.  Re-install the table, then release. */
__declspec(dllexport) void __attribute__((thiscall))
FaktMesh_DtorBody(CFaktMesh *self)
{
    self->unknown00 = FaktMesh_Vtable();
    FaktMesh_ReleaseModelBuffers(self);
}

/* 0x00437b10 -- vtable slot 0.
 *
 * Unverified by test, and said plainly: xref.py finds no reference of any
 * kind to 0x00437b10, our table's slot 0 is the only way in, and the free is
 * the game heap's because a heap-allocated CFaktMesh would have come from
 * the game's operator new.  Same position as LevelObjBase_ScalarDtor and
 * Wrapper_ScalarDtor. */
__declspec(dllexport) void *__attribute__((thiscall))
FaktMesh_ScalarDtor(CFaktMesh *self, unsigned int flags)
{
    FaktMesh_DtorBody(self);
    if (flags & 1)
        free(self);
    return self;
}

} // extern "C"
