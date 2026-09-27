/* Direct3D: presentation, teardown and lifecycle (direct3d.h).  Device
 * creation is in createdevice.cpp.
 *
 * FlipPrimaryFrame Blts the image to pZBuffer -- which holds the back buffer
 * despite its name (direct3d.h) -- and flips.  Neither return value is
 * checked.
 *
 * KAROO_FLIP_FX=noblt skips the Blt and flips whatever is already on the
 * primary: the loading/theme bitmap never appears. */

#include "direct3d.h"
#include "log.h"
#include <stdlib.h>
Direct3D* g_pDirect3D;

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

/* Releases the six COM interfaces it owns, frees the enumerated display-mode
 * list, and zeroes the mode/z-buffer-format state. */

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Direct3D_ReleaseResources(Direct3D *self)
{
    if (self->pViewport)   { self->pViewport->Release();   self->pViewport   = NULL; }
    if (self->pDevice)     { self->pDevice->Release();     self->pDevice     = NULL; }
    if (self->pBackBuffer) { self->pBackBuffer->Release(); self->pBackBuffer = NULL; }
    if (self->pPrimary)    { self->pPrimary->Release();    self->pPrimary    = NULL; }
    if (self->pD3D)        { self->pD3D->Release();        self->pD3D        = NULL; }
    if (self->pDD4)        { self->pDD4->Release();        self->pDD4        = NULL; }

    // Free each node's payload, then the nodes: two passes.
    for (LinkedListNode *n = self->modeList.pHead; n != NULL; ) {
        void *value = n->pValue;
        n = n->pNextNode;
        if (value)
            free(value);
    }
    LinkedList_Clear(&self->modeList);

    self->pSelectedMode     = NULL;
    self->dwModeFilterFlags = 0;
    for (int i = 0; i < 8; i++)
        self->zbufFmt[i] = 0;

    // PRESERVED: pZBuffer (the back buffer from GetAttachedSurface) is NULLed
    // but never Released, leaking the reference GetAttachedSurface added.
    self->pZBuffer = NULL;

    log_write("direct3d: ReleaseResources this=%p done\n", self);
}

/* ─── Construction and teardown ─────────────────────────────────────────────
 *
 * WinMain allocates the 0x238-byte object, constructs it, and deletes it
 * through the scalar destructor with flag 1. */
static void *const g_D3DVtable[1] = { (void *)&Direct3D_ScalarDestructor };

extern "C" __declspec(dllexport) Direct3D *__attribute__((thiscall))
Direct3D_Construct(Direct3D *self)
{
    List_Init(&self->modeList);
    self->vtable = (void **)g_D3DVtable;
    self->hWnd = NULL;
    self->pDD4 = NULL;
    self->pLastError[0] = '\0';
    // Clearing the list just Init'd is redundant but harmless.
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
