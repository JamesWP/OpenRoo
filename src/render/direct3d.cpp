/* Direct3D closure reimplementations — presentation path.
 *
 * Replaces (safety-stubbed by patch.py):
 *   0x425fc0 FlipPrimaryFrame  __cdecl(LoadedImage *)
 *
 * The original is 52 bytes and does exactly two COM calls:
 *
 *   g_pDirect3D->pZBuffer->Blt(NULL, img->pTextureSurface, NULL,
 *                              DDBLT_WAIT, NULL);
 *   g_pDirect3D->pPrimary->Flip(NULL, 1);
 *
 * Confirmed from both the Ghidra decompile and the raw disassembly at
 * 0x425fde (FF 51 14 = Blt, slot 5) / 0x425ff0 (FF 51 2c = Flip, slot 11).
 * Neither return value is checked by the original, and it returns void.
 *
 * Note the destination is pZBuffer (+0x3c), not the back buffer — that is
 * what the game does; the field name comes from how the surface is created,
 * not from how this path uses it.  UpdatePlayerCamera Blts to the same
 * surface.  Reproduced as-is.
 *
 * Both surfaces are com_proxy surface proxies (every surface is wrapped at
 * w4_CreateSurface / ws4_GetAttachedSurface), and img->pTextureSurface is a
 * proxy too.  Calling through them is deliberate: the proxy Blt forwarder
 * unwraps the peer argument itself, so presentation stays visible to the
 * proxy layer exactly as it was before the replacement.
 *
 * KAROO_FLIP_FX=noblt skips the Blt and flips whatever is already on the
 * primary, as visual proof the presented pixels come from this code: the
 * loading/theme bitmap never appears.
 */
#include "direct3d.h"
#include "log.h"

#define FLIP_LOG_FIRST 8

static bool fx_noblt(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = 0;
        if (GetEnvironmentVariableA("KAROO_FLIP_FX", buf, sizeof(buf)))
            cached = (lstrcmpiA(buf, "noblt") == 0);
        log_write("direct3d: flip FX mode = %s\n", cached ? "noblt" : "off");
    }
    return cached != 0;
}

extern "C" __declspec(dllexport) void __cdecl
Direct3D_FlipPrimaryFrame(LoadedImage *img)
{
    Direct3D *d3d = g_pDirect3D;
    HRESULT hr_blt = S_OK;
    bool skipped = fx_noblt();

    if (!skipped)
        hr_blt = d3d->pZBuffer->Blt(NULL, img->pTextureSurface, NULL,
                                    DDBLT_WAIT, NULL);

    HRESULT hr_flip = d3d->pPrimary->Flip(NULL, 1);

    static LONG logged = 0;
    if (InterlockedIncrement(&logged) <= FLIP_LOG_FIRST) {
        char blt[16];
        if (skipped)
            lstrcpyA(blt, "skipped");
        else
            wsprintfA(blt, "%08lX", hr_blt);
        log_write("direct3d: FlipPrimaryFrame img=%p src=%p zbuf=%p primary=%p "
                  "blt=%s flip=%08lX\n",
                  img, img->pTextureSurface, d3d->pZBuffer, d3d->pPrimary,
                  blt, hr_flip);
    }
}
