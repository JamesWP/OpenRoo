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
 */
#include "faktmesh.h"
#include "com_proxy.h"
#include "log.h"

#define MESH_FVF        0x212  /* XYZ | NORMAL | TEX2 — 0x28-byte stride */
#define MESH_LOG_FIRST  8

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
