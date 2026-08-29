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

static HRESULT draw_mesh(CFaktMesh *mesh, IDirect3DDevice3 *dev, DWORD frame,
                         DWORD flags, const char *name)
{
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
