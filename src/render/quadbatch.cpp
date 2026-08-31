/* DrawQuadBatch (0x408710) reimplementation.
 *
 * __cdecl(QuadVerts *verts, Game *game, Direct3D *d3d) — 341 bytes, ten D3D
 * dispatches, one E8 call site (0x427B47 in RenderGameFrame).
 *
 * Walks the quad-batch LevelObject array (count at Game+0x11aa8, objects at
 * +0x11aac, stride 0x5dd).  For each object it walks its SceneSubObject array
 * (count at +0x3c1, entries at +0x3c5, stride 0x3c) and, per sub-object:
 *
 *   SetRenderState(TEXTUREADDRESSU=0x2c, sub->dwTexAddress ? : 3)
 *   SetRenderState(TEXTUREADDRESSV=0x2d, sub->dwTexAddress ? : 3)
 *   if (sub->pTexture)  SetTexture(0, *(sub->pTexture + 0x18))
 *   if (src && dst) { SetRenderState(ALPHABLENDENABLE, 1)
 *                     SetRenderState(SRCBLEND, src)
 *                     SetRenderState(DESTBLEND, dst) }
 *   else             { SetRenderState(ALPHABLENDENABLE, 0) }
 *   if (obj->dwType == 2) { SetTransform(WORLD, &g_dwWorldIdentity)
 *                          DrawPrimitive(TRIANGLELIST, 0x1e2,
 *                                        verts->pData, verts->dwQuads * 6, 0) }
 *
 * Faithful details worth not "cleaning up":
 *   - The original re-reads d3d->pDevice before every single dispatch rather
 *     than caching it; harmless, but reproduced so the traffic through the
 *     proxy layer is call-for-call identical.
 *   - The ALPHABLENDENABLE=0 path and the DESTBLEND path are the *same* tail
 *     call in the original, with (state, value) selected by the branch.  Kept
 *     as one call here for the same reason.
 *   - The draw is gated on the per-object dwType at +0x00 (the decompile
 *     spells it *(puVar4 - 0x3c1), and puVar4 is &obj->dwSubObjectCount at
 *     obj+0x3c1 — so it is obj+0x00, not obj+0x04), but the render
 *     states above it are set for every sub-object whether or not the draw
 *     happens.  That ordering is load-bearing: it leaves state behind for
 *     whatever draws next.  Do not hoist the gate.
 *   - Both loop bounds are re-read from memory each iteration, as in the
 *     original.
 *
 * The device is the com_proxy device proxy; calls go through it deliberately.
 *
 * KAROO_QUAD_FX visual-proof modes:
 *   noalpha — force ALPHABLENDENABLE off for every sub-object.  Note this is
 *             invisible on content whose sub-objects already have
 *             dwBlendSrc/dwBlendDst of 0, which is the common case: the first
 *             level checked draws 116 quads with src=0 dst=0, already taking
 *             the alpha-off branch.
 *   nodraw  — skip the DrawPrimitive entirely, leaving every render state set
 *             exactly as before.  The quads vanish; nothing else changes.
 *             This is the mode that actually proves the draw is ours.
 */
#include "direct3d.h"
#include "levelobject.h"
#include "log.h"

#define QUAD_FVF       0x1e2
#define QUAD_LOG_FIRST 8

/* Vertex source passed as param_1 — only these two fields are read. */
struct QuadVerts {
    BYTE  pad00[0x124];
    DWORD dwQuads;   // +0x124  quads; 6 vertices each (two triangles)
    void *pData;     // +0x128  raw vertex array, FVF 0x1e2
};
static_assert(offsetof(QuadVerts, dwQuads) == 0x124, "QuadVerts layout");
static_assert(offsetof(QuadVerts, pData)   == 0x128, "QuadVerts layout");

#define g_dwWorldIdentity (*(D3DMATRIX *)0x004e0440)

/* KAROO_QUAD_DUMP=<path> — write every vertex of the first frame's quad batch
 * to a file, once.  FVF 0x1e2 is a 32-byte vertex: xyz(12) + reserved(4) +
 * diffuse(4) + specular(4) + uv(8).  This is how the tile-side geometry is
 * mapped back onto the level grid without guessing from file statistics. */
static void quad_dump(const void *data, DWORD quads)
{
    static LONG calls = 0;
    char path[MAX_PATH];
    if (!GetEnvironmentVariableA("KAROO_QUAD_DUMP", path, sizeof(path)))
        return;
    /* Dump the 200th gated draw, not the first: if the buffer is filled
     * lazily, the first frame would show an empty one. */
    if (InterlockedIncrement(&calls) != 200)
        return;
    log_write("quadbatch: dumping at call 200, pData=%p quads=%lu\n", data, quads);

    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        log_write("quadbatch: dump could not open %s (err=%lu)\n",
                  path, GetLastError());
        return;
    }
    const BYTE *v = (const BYTE *)data;
    char line[300];
    DWORD wr;
    int n = wsprintfA(line, "raw dump pData=%p quads=%lu\r\n", data, quads);
    WriteFile(f, line, n, &wr, NULL);
    /* Raw hex of the whole declared buffer (quads*6*32 bytes), 32 bytes per
     * line = one vertex per line, so the analysis can find the geometry
     * wherever it actually sits rather than assuming the FVF offsets. */
    DWORD total = quads * 6 * 32;
    for (DWORD off = 0; off < total; off += 32) {
        n = 0;
        n += wsprintfA(line + n, "%06lX ", off);
        for (int b = 0; b < 32; b++)
            n += wsprintfA(line + n, "%02X", v[off + b]);
        n += wsprintfA(line + n, "\r\n");
        WriteFile(f, line, n, &wr, NULL);
    }
    CloseHandle(f);
    log_write("quadbatch: dumped %lu quads to %s\n", quads, path);
}

enum QuadFxMode { QUAD_FX_OFF = 0, QUAD_FX_NOALPHA, QUAD_FX_NODRAW };

static QuadFxMode quad_fx(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = QUAD_FX_OFF;
        if (GetEnvironmentVariableA("KAROO_QUAD_FX", buf, sizeof(buf))) {
            if (lstrcmpiA(buf, "noalpha") == 0) cached = QUAD_FX_NOALPHA;
            else if (lstrcmpiA(buf, "nodraw") == 0) cached = QUAD_FX_NODRAW;
        }
        log_write("quadbatch: FX mode = %s\n",
                  cached == QUAD_FX_NOALPHA ? "noalpha" :
                  cached == QUAD_FX_NODRAW  ? "nodraw"  : "off");
    }
    return (QuadFxMode)cached;
}

extern "C" __declspec(dllexport) void __cdecl
Direct3D_DrawQuadBatch(QuadVerts *verts, void *game, Direct3D *d3d)
{
    DWORD nobj = *(DWORD *)((BYTE *)game + GAME_OFF_QUAD_COUNT);
    if (nobj == 0)
        return;

    for (DWORD i = 0; i < *(DWORD *)((BYTE *)game + GAME_OFF_QUAD_COUNT); i++) {
        BYTE  *obj  = lobj_at(game, i);
        DWORD  nsub = *(DWORD *)(obj + LOBJ_OFF_SUBOBJCOUNT);
        {   /* KAROO_QUAD_DIAG=1 — which dwType values actually reach here?
             * The draw is gated on dwType==2 and no level tested so far has
             * such an object, so this is how to tell "gate never matches" from
             * "gate is reading the wrong field". */
            static LONG diag = 0;
            char dbuf[8];
            if (GetEnvironmentVariableA("KAROO_QUAD_DIAG", dbuf, sizeof(dbuf))
                && dbuf[0] != '0' && InterlockedIncrement(&diag) <= 24)
                log_write("quadbatch: diag obj=%lu kind=%lu nsub=%lu\n",
                          i, *(DWORD *)(obj + LOBJ_OFF_DRAWKIND), nsub);
        }
        if (nsub == 0)
            continue;

        for (DWORD s = 0; s < *(DWORD *)(obj + LOBJ_OFF_SUBOBJCOUNT); s++) {
            SceneSubObject *sub =
                (SceneSubObject *)(obj + LOBJ_OFF_SUBOBJECTS) + s;

            DWORD addr = sub->dwTexAddress ? sub->dwTexAddress : 3;
            d3d->pDevice->SetRenderState(D3DRENDERSTATE_TEXTUREADDRESSU, addr);
            d3d->pDevice->SetRenderState(D3DRENDERSTATE_TEXTUREADDRESSV, addr);

            if (sub->pTexture)
                d3d->pDevice->SetTexture(
                    0, *(IDirect3DTexture2 **)((BYTE *)sub->pTexture + 0x18));

            /* One tail call in the original, state/value chosen by the branch. */
            D3DRENDERSTATETYPE last_state;
            DWORD              last_value;
            if (sub->dwBlendSrc && sub->dwBlendDst
                && quad_fx() != QUAD_FX_NOALPHA) {
                d3d->pDevice->SetRenderState(D3DRENDERSTATE_ALPHABLENDENABLE, 1);
                d3d->pDevice->SetRenderState(D3DRENDERSTATE_SRCBLEND,
                                             sub->dwBlendSrc);
                last_state = D3DRENDERSTATE_DESTBLEND;
                last_value = sub->dwBlendDst;
            } else {
                last_state = D3DRENDERSTATE_ALPHABLENDENABLE;
                last_value = 0;
            }
            d3d->pDevice->SetRenderState(last_state, last_value);

            if (*(DWORD *)(obj + LOBJ_OFF_DRAWKIND) == 2) {
                d3d->pDevice->SetTransform(D3DTRANSFORMSTATE_WORLD,
                                           &g_dwWorldIdentity);
                quad_dump(verts->pData, verts->dwQuads);
                HRESULT hr = S_OK;
                if (quad_fx() != QUAD_FX_NODRAW)
                    hr = d3d->pDevice->DrawPrimitive(
                        D3DPT_TRIANGLELIST, QUAD_FVF, verts->pData,
                        verts->dwQuads * 6, 0);

                static LONG logged = 0;
                if (InterlockedIncrement(&logged) <= QUAD_LOG_FIRST)
                    log_write("quadbatch: obj=%lu sub=%lu tex=%p addr=%lu "
                              "src=%lu dst=%lu quads=%lu -> hr=%08lX\n",
                              i, s, sub->pTexture, addr, sub->dwBlendSrc,
                              sub->dwBlendDst, verts->dwQuads, hr);
            }
        }
    }
}
