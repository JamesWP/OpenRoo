/* SceneTexture / LoadedImage release path reimplementation.
 *
 *   0x43ebb0 LoadedImage::ReleaseTextureSurfaces  __thiscall(this), ret 0
 *            — 3 E8 call sites, all-CALL refs
 *   0x440050 SceneTexture::ReleaseD3DTexture      __thiscall(this), ret 0
 *            — 8 E8 call sites, all-CALL refs
 *
 * Signature note: HOOKS.md lists ReleaseTextureSurfaces as `__fastcall`.  The
 * original is `mov ecx,esi` at entry and a plain `ret` — this in ECX, zero
 * stack args, i.e. __thiscall.  For a one-argument function the two are
 * indistinguishable at the call site, but the entry is what matters when
 * declaring the replacement, and `__fastcall` would be misleading to anyone
 * adding a second parameter later.
 *
 * ReleaseTextureSurfaces is shared: it is called on both the 24-byte base
 * LoadedImage instances (from the error paths in CreateSurfaceDIB /
 * BlitDIBToSurface) and, via ReleaseD3DTexture, on the 28-byte SceneTexture
 * ones.  It therefore takes LoadedImage* and must not touch +0x18 — a base
 * instance has no such field (the globals at 0x46c798 and 0x4dc7a8 are
 * followed by unrelated data exactly 24 bytes later).
 *
 * Preserved deliberately: pTextureSurface and pTexturePalette are NULLed
 * *unconditionally*, outside their null checks, while pImageName is NULLed
 * only inside its.  The originals are inconsistent about this and it is
 * reproduced rather than tidied.
 *
 * The call from ReleaseD3DTexture to ReleaseTextureSurfaces (0x440069) sits
 * inside a function this patch UD2-stubs, so rewriting it is harmless; the
 * replacement calls its own copy directly.
 */
#include "texture.h"
#include "log.h"

/* FactAlloc::Free2 — __cdecl(void *), shared helper left live in the binary. */
typedef void (__cdecl *free2_fn)(void *);
#define ORIG_FACT_FREE2 ((free2_fn)0x004504c0)

extern "C" {

__declspec(dllexport) void __attribute__((thiscall))
Texture_ReleaseSurfaces(LoadedImage *self)
{
    IDirectDrawSurface4 *surf = self->pTextureSurface;
    if (surf != NULL)
        surf->Release();
    self->pTextureSurface = NULL;          /* unconditional */

    IDirectDrawSurface4 *pal = self->pTexturePalette;
    if (pal != NULL)
        pal->Release();
    self->pTexturePalette = NULL;          /* unconditional */

    if (self->ImageName != NULL) {
        ORIG_FACT_FREE2(self->ImageName);
        self->ImageName = NULL;            /* only inside the check */
    }

    self->loadedState = 0;
    self->loadStatus  = 0;
}

__declspec(dllexport) void __attribute__((thiscall))
Texture_ReleaseD3DTexture(SceneTexture *self)
{
    IDirect3DTexture2 *tex = self->pTexture2;
    if (tex != NULL)
        tex->Release();
    self->pTexture2 = NULL;                /* unconditional */

    Texture_ReleaseSurfaces(&self->base);
}

} // extern "C"
