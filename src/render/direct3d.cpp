/* Direct3D closure reimplementations — presentation path.
 *
 * Replaces (safety-stubbed by patch.py):
 *   0x425fc0 FlipPrimaryFrame        __cdecl(LoadedImage *)
 *   0x413180 Direct3D::ReleaseResources  __thiscall(this), ret 0
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

/* ─── Direct3D::ReleaseResources (0x413180) ────────────────────────────────
 *
 * __thiscall(this), plain `ret` — no stack args (checked against the original,
 * not taken from the decompiler).  Four E8 call sites, no E9/PUSH/DATA refs.
 *
 * Releases the six COM interfaces it owns, frees the enumerated display-mode
 * list, and zeroes the mode/z-buffer-format state.
 *
 * Two behaviours preserved deliberately:
 *   - pZBuffer is NULLed but never Released.  Every other interface here gets
 *     a Release first; the z-buffer surface does not.  That is a leak in the
 *     original, reproduced rather than "fixed" — releasing it would change the
 *     refcount the rest of the teardown sees.
 *   - The mode-list walk frees each node's pValue and then calls
 *     LinkedList::Clear, which frees the nodes themselves.  Two passes, as in
 *     the original.
 *
 * LinkedList::Clear and FactAlloc::Free2 are shared helpers that stay live for
 * other callers, so they are called at their original addresses rather than
 * stubbed or duplicated (same approach as factory.cpp).
 */
typedef void (__cdecl *free2_fn)(void *);
#define ORIG_FACT_FREE2 ((free2_fn)0x004504c0)

typedef void (__attribute__((thiscall)) *listclear_fn)(LinkedList *);
#define ORIG_LIST_CLEAR ((listclear_fn)0x004254f0)

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Direct3D_ReleaseResources(Direct3D *self)
{
    if (self->pViewport)   { self->pViewport->Release();   self->pViewport   = NULL; }
    if (self->pDevice)     { self->pDevice->Release();     self->pDevice     = NULL; }
    if (self->pBackBuffer) { self->pBackBuffer->Release(); self->pBackBuffer = NULL; }
    if (self->pPrimary)    { self->pPrimary->Release();    self->pPrimary    = NULL; }
    if (self->pD3D)        { self->pD3D->Release();        self->pD3D        = NULL; }
    if (self->pDD4)        { self->pDD4->Release();        self->pDD4        = NULL; }

    /* Free each node's payload, then the nodes. */
    for (LinkedListNode *n = self->modeList.pHead; n != NULL; ) {
        void *value = n->pValue;
        n = n->pNextNode;
        if (value)
            ORIG_FACT_FREE2(value);
    }
    ORIG_LIST_CLEAR(&self->modeList);

    self->pSelectedMode     = NULL;
    self->dwModeFilterFlags = 0;
    for (int i = 0; i < 8; i++)          /* dwZBufFmtSize .. +0x30 */
        self->zbufFmt[i] = 0;

    self->pZBuffer = NULL;               /* NOT released — see header */

    log_write("direct3d: ReleaseResources this=%p done\n", self);
}
