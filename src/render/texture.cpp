/* SceneTexture / LoadedImage release path reimplementation.
 *
 *   0x43ebb0 LoadedImage::ReleaseTextureSurfaces  __thiscall(this), ret 0
 *            — 3 E8 call sites, all-CALL refs
 *   0x440050 SceneTexture::ReleaseD3DTexture      __thiscall(this), ret 0
 *            — 8 E8 call sites, all-CALL refs
 *   0x43eb00 LoadedImage::Load                    __thiscall(this), ret 0
 *            — 24 E8 call sites, all-CALL refs (surface-lost reload)
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

/* ─── LoadedImage::Load (0x43eb00) ─────────────────────────────────────────
 *
 * Surface-lost recovery: Restore() the DirectDraw surface, then re-apply
 * whichever loader originally filled it — LoadImageA + BlitDIBToSurface for a
 * BMP/DIB (loadedState 1), ParseTGAFile for a TGA (loadedState 2), nothing
 * otherwise.  __thiscall(this), plain ret, 24 call sites, no vtable refs.
 *
 * ParseTGAFile (0x43e190) is left intact in the binary and called at its
 * original address; it is __thiscall with one stack arg (`ret $0x4`, checked).
 * BlitDIBToSurface (0x43dfc0) used to be called the same way, but is now
 * replaced and UD2-stubbed, so this calls TextureDIB_BlitToSurface directly.
 *
 * Return convention, preserved exactly: a bool in AL with the upper three
 * bytes carrying whatever the last call left there.
 *   - no surface                        -> 0
 *   - Restore failed                    -> hr & 0xffffff00      (AL = 0)
 *   - DIB reload failed                 -> DeleteObject & ~0xff (AL = 0)
 *   - TGA reload failed                 -> ParseTGAFile's value verbatim
 *   - otherwise                         -> (last & 0xffffff00) | 1
 * Note the last case also covers loadedState values other than 1 and 2: the
 * original computes loadedState-2, finds it non-zero, skips the TGA path and
 * still returns true.  That is reproduced rather than turned into a failure.
 */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
TextureDIB_BlitToSurface(LoadedImage *, HANDLE);

typedef unsigned int (__attribute__((thiscall)) *parsetga_fn)(LoadedImage *, LPCSTR);
#define ORIG_PARSE_TGA ((parsetga_fn)0x0043e190)

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Texture_Load(LoadedImage *self)
{
    IDirectDrawSurface4 *surf = self->pTextureSurface;
    if (surf == NULL)
        return 0;

    HRESULT hr = surf->Restore();
    static LONG seen = 0;
    if (InterlockedIncrement(&seen) <= 4)
        log_write("texture: Load this=%p state=%d restore=%08lX name=%s\n",
                  self, self->loadedState, hr,
                  self->ImageName ? self->ImageName : "(null)");
    if (hr < 0)
        return (unsigned int)hr & 0xffffff00u;

    unsigned int last;
    if (self->loadedState == 1) {
        HANDLE h = LoadImageA(GetModuleHandleA(NULL), self->ImageName,
                              IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
        if (h == NULL) {
            h = LoadImageA(NULL, self->ImageName, IMAGE_BITMAP, 0, 0,
                           LR_LOADFROMFILE | LR_CREATEDIBSECTION);
            if (h == NULL)
                return 0;
        }
        unsigned int ok = TextureDIB_BlitToSurface(self, h);
        if ((ok & 0xff) == 0) {
            unsigned int d = (unsigned int)DeleteObject((HGDIOBJ)h);
            return d & 0xffffff00u;
        }
        last = (unsigned int)DeleteObject((HGDIOBJ)h);
    } else {
        last = (unsigned int)(self->loadedState - 2);
        if (last == 0) {
            last = ORIG_PARSE_TGA(self, self->ImageName);
            if ((last & 0xff) == 0)
                return last;
        }
    }

    return (last & 0xffffff00u) | 1u;
}
