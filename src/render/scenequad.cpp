/* Game::RenderSceneObjects — replacement for its type-2 quad draw step.
 *
 * The original issues one strided draw for every "type 2" scene object
 * (animated UV/colour billboard quad):
 *
 *   0x0040af26  call dword ptr [edx+0x80]   ; IDirect3DDevice3::DrawPrimitiveStrided
 *               (D3DPT_TRIANGLESTRIP, FVF 0x242, &strided, 4, 0)
 *
 * patch.py rewrites that 6-byte indirect call into `E8 rel32` + `NOP` targeting
 * hooks_SceneQuadDrawStrided below, so this function *is* the draw step now.
 *
 * ── The bug this fixes ──────────────────────────────────────────────────────
 * FVF 0x242 is XYZ | DIFFUSE | D3DFVF_TEX2, i.e. it declares **two** texture
 * coordinate sets ((0x242 & D3DFVF_TEXCOUNT_MASK) >> 8 == 2).  The original
 * only ever fills three of the strided struct's arrays — position (+0x00),
 * diffuse (+0x10) and textureCoords[0] (+0x20).  textureCoords[1] at +0x28 is
 * left holding whatever was on the stack; observed values are leftover floats
 * (0xc05b82c9, 0x40750cf1, ...) that change every call.
 *
 * The driver is entitled to read every set the FVF declares, and Wine's
 * pack_strided_data does.  Dereferencing that garbage usually lands on an
 * unmapped page — STATUS_ACCESS_VIOLATION, which Wine's __EXCEPT_PAGE_FAULT
 * around the memcpy swallows, matching Windows' silent discard.  When it
 * happens to land on a *guard* page instead, the exception is 0x80000001,
 * that filter declines it, and the process dies.  Hence the intermittency:
 * same wild read every frame, fatal only when the page underneath is a guard
 * page (CRASH.md).
 *
 * The fix is to stop making the wild read possible: point every texture
 * coordinate set the FVF declares, beyond the one the game actually fills, at
 * set 0's array.  Rendering is unchanged — only texture stage 0 is enabled at
 * this draw, so coordinate set 1 is never sampled; it just has to be readable.
 * The FVF itself is left at 0x242 so the vertex layout D3D builds is exactly
 * the one the game asked for.
 *
 * Everything else is deliberately bit-identical to the original: same device
 * (the com_proxy proxy the game holds, so the draw stays visible to the proxy
 * layer), same primitive type, FVF, vertex count and flags.
 *
 * KAROO_SCENEQUAD_FX=drop skips the draw entirely — the animated billboard
 * quads vanish — as visual proof the pixels come from this reimplementation.
 */
#include "com_proxy.h"
#include "log.h"

#define QUAD_LOG_FIRST  8

static bool fx_drop(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (GetEnvironmentVariableA("KAROO_SCENEQUAD_FX", buf, sizeof(buf)))
            cached = (lstrcmpiA(buf, "drop") == 0);
        log_write("scenequad: FX mode = %s\n", cached ? "drop" : "off");
    }
    return cached != 0;
}

extern "C" __declspec(dllexport) HRESULT WINAPI
hooks_SceneQuadDrawStrided(IDirect3DDevice3 *dev, D3DPRIMITIVETYPE prim, DWORD fvf,
                           D3DDRAWPRIMITIVESTRIDEDDATA *data, DWORD vert_count, DWORD flags)
{
    /* Repair the sets the caller declared but never filled.  Set 0 is always
     * the one the game populates; sets 1..n-1 are stack residue. */
    DWORD ntex = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    if (ntex > D3DDP_MAXTEXCOORD)
        ntex = D3DDP_MAXTEXCOORD;
    for (DWORD i = 1; i < ntex; i++)
        data->textureCoords[i] = data->textureCoords[0];

    static LONG logged = 0;
    if (InterlockedIncrement(&logged) <= QUAD_LOG_FIRST)
        log_write("scenequad: dev=%p prim=%lu fvf=%03lX ntex=%lu pos=%p tex0=%p count=%lu\n",
                  dev, (DWORD)prim, fvf, ntex, data->position.lpvData,
                  data->textureCoords[0].lpvData, vert_count);

    if (fx_drop())
        return D3D_OK;

    return dev->DrawPrimitiveStrided(prim, fvf, data, vert_count, flags);
}
