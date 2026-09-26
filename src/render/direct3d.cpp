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
#include <stdlib.h>
Direct3D* g_pDirect3D;   /* was 0x004e04ac */

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
            free(value);
    }
    LinkedList_Clear(&self->modeList);

    self->pSelectedMode     = NULL;
    self->dwModeFilterFlags = 0;
    for (int i = 0; i < 8; i++)          /* dwZBufFmtSize .. +0x30 */
        self->zbufFmt[i] = 0;

    self->pZBuffer = NULL;               /* NOT released — see header */

    log_write("direct3d: ReleaseResources this=%p done\n", self);
}

/* ─── Construction and teardown: 0x412680, 0x412730, 0x412710 ──────────────
 *
 * WinMain allocates the 0x238-byte object with the game's operator new
 * (0x42d32d), constructs it (one E8, 0x42D33B), and deletes it through
 * vtable slot 0 with flag 1 -- so the scalar dtor frees on the game heap
 * (alloc.h, mixed ownership until WinMain is ours).  The game's one-slot
 * table 0x45d398 is left pointing at the stubbed original, a tripwire.
 *
 * Quirk preserved: the ctor Clears the mode list it has just Init'd. */
static void *const g_D3DVtable[1] = { (void *)&Direct3D_ScalarDestructor };

extern "C" __declspec(dllexport) Direct3D *__attribute__((thiscall))
Direct3D_Construct(Direct3D *self)
{
    List_Init(&self->modeList);
    self->vtable = (void **)g_D3DVtable;
    self->hWnd = NULL;
    self->pDD4 = NULL;
    self->pLastError[0] = '\0';
    List_Clear(&self->modeList);
    self->pSelectedMode = NULL;
    *(DWORD *)((BYTE *)self + 0xcc) = 0;
    self->pD3D = NULL;
    self->pViewport = NULL;
    self->pDevice = NULL;
    self->dwModeFilterFlags = 0;
    for (int i = 0; i < 8; i++)
        self->zbufFmt[i] = 0;
    self->pBackBuffer = NULL;
    self->pPrimary = NULL;
    self->pZBuffer = NULL;
    return self;
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Direct3D_Destruct(Direct3D *self)
{
    self->vtable = (void **)g_D3DVtable;
    List_Destruct(&self->modeList);
}

extern "C" __declspec(dllexport) Direct3D *__attribute__((thiscall))
Direct3D_ScalarDestructor(Direct3D *self, unsigned char flags)
{
    Direct3D_Destruct(self);
    if (flags & 1)
        free(self);
    return self;
}
