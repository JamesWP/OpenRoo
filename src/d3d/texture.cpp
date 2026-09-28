/* LoadedImage and SceneTexture: the lifecycle, the release path, the
 * surface-lost reload, and the palette built from a DIB (texture.h).
 *
 * ReleaseSurfaces serves both the 24-byte LoadedImage instances and, through
 * ReleaseD3DTexture, the 28-byte SceneTexture ones, so it takes a LoadedImage*
 * and must not touch +0x18. */

#include "scenetexture.h"
#include "d3dnative.h"
#include "log.h"
#include <stdlib.h>
SceneTexture g_texKaroo128;
SceneTexture g_texShadow;

extern "C" {

/* KAROO_IMAGE_DIAG=1: every one of the six lifecycle functions announces its
 * first call -- the census that tells "ran and agreed" from "never ran".  The
 * two scalar deleting dtors are the ones worth watching: each is reachable
 * only through vtable slot 0. */
static bool image_diag(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = (GetEnvironmentVariableA("KAROO_IMAGE_DIAG", buf, sizeof(buf))
                  && buf[0] == '1') ? 1 : 0;
    }
    return cached != 0;
}

/* First-call announcement plus a running tally. */
__declspec(dllexport) void Texture_ImageFirstCall(const char *who,
                                                  unsigned long *seen)
{
    if (!image_diag())
        return;
    if ((*seen)++ == 0)
        log_write("texture: DIAG first call -- %s\n", who);
}
#define image_first Texture_ImageFirstCall

/* ─── CreatePaletteFromDIB ─────────────────────────────────────────────────
 *
 * __stdcall.  Reads the DIB's colour table off a scratch DC, rewrites each
 * RGBQUAD in place as a PALETTEENTRY, and hands the result to
 * IDirectDraw4::CreatePalette. */
__declspec(dllexport) IDirectDrawPalette *__stdcall
Texture_CreatePaletteFromDIB(IDirectDraw4 *dd, HBITMAP hbmp)
{
    IDirectDrawPalette *pal = NULL;
    RGBQUAD table[256];  // PRESERVED: uninitialised

    // PRESERVED: CreateCompatibleDC is not NULL-checked, and the DC's previous
    // bitmap is discarded, never restored.
    HDC dc = CreateCompatibleDC(NULL);
    SelectObject(dc, hbmp);
    int count = (int)GetDIBColorTable(dc, 0, 256, table);
    DeleteDC(dc);

    // PRESERVED: fewer than 256 colours leaves the rest of the table as stack
    // garbage, and CreatePalette reads all 256 for an 8-bit palette.  An empty
    // table returns NULL without calling CreatePalette.
    if (count == 0)
        return pal;

    if (count > 0) {
        // BGRX -> RGB0: `reserved` is shifted out, not copied, so the flags
        // are 0.
        DWORD *p = (DWORD *)table;
        for (int i = 0; i < count; ++i) {
            DWORD q = p[i];
            DWORD r = (q >> 16) & 0xff;
            DWORD g = (q >> 8)  & 0xff;
            DWORD b =  q        & 0xff;
            p[i] = r | (g << 8) | (b << 16);
        }
    }

    // Signed comparison against 16 picks the palette width.
    DWORD flags = (count > 16) ? DDPCAPS_8BIT : DDPCAPS_4BIT;
    dd->CreatePalette(flags, (LPPALETTEENTRY)table, &pal, NULL);
    if (image_diag())
        log_write("texture: DIAG CreatePaletteFromDIB count=%d flags=%lu pal=%p\n",
                  count, (unsigned long)flags, (void *)pal);
    return pal;
}


/* One slot: the scalar deleting destructor. */
static void *const g_LoadedImageVtable[1] = { (void *)&LoadedImage::scalarDtor };

void *
LoadedImage::vtbl(void)
{
    return (void *)g_LoadedImageVtable;
}

/* ─── LoadedImage lifecycle ──────────────────────────────────────────────────
 */
LoadedImage *LoadedImage::construct()
{
    static unsigned long seen; image_first("LoadedImage::Ctor", &seen);
    unknown00_       = LoadedImage::vtbl();
    pTextureSurface_ = NULL;
    pTexturePalette_ = NULL;
    ImageName_       = NULL;
    loadedState_     = 0;
    loadStatus_      = 0;
    return this;
}

void LoadedImage::dtorBody()
{
    static unsigned long seen; image_first("LoadedImage::DtorBody", &seen);
    unknown00_ = LoadedImage::vtbl();
    if (ImageName_ != NULL)
        free(ImageName_);  // PRESERVED: not NULLed, so a second DtorBody double-frees
}

/* Reachable only through vtable slot 0. */
LoadedImage * __attribute__((thiscall))
LoadedImage::scalarDtor(LoadedImage *self, unsigned int flags)
{
    static unsigned long seen; image_first("LoadedImage::ScalarDeletingDtor", &seen);
    self->dtorBody();
    if ((flags & 1) != 0)
        free(self);
    return self;
}

void LoadedImage::releaseSurfaces()
{
    IDirectDrawSurface4 *surf = pTextureSurface_;
    if (surf != NULL)
        surf->Release();
    pTextureSurface_ = NULL;  // unconditional

    IDirectDrawSurface4 *pal = pTexturePalette_;
    if (pal != NULL)
        pal->Release();
    pTexturePalette_ = NULL;  // unconditional

    if (ImageName_ != NULL) {
        free(ImageName_);
        ImageName_ = NULL;  // only inside the check
    }

    loadedState_ = 0;
    loadStatus_  = 0;
}

}  // extern "C"

/* ─── Load ─────────────────────────────────────────────────────────────────
 *
 * Surface-lost recovery: Restore() the surface, then re-apply whichever loader
 * filled it -- the DIB path for loadedState 1, the TGA path for 2, nothing
 * otherwise.
 *
 * Only the low byte of the result is the success flag.  The upper bytes are
 * whatever the last call left: Restore's HRESULT, DeleteObject's result, or
 * the TGA loader's value.  Any loadedState other than 1 or 2 returns true. */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
TextureDIB_BlitToSurface(LoadedImage *, HANDLE);

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
TextureTGA_Parse(LoadedImage *, LPCSTR);

unsigned int LoadedImage::load()
{
    IDirectDrawSurface4 *surf = pTextureSurface_;
    if (surf == NULL)
        return 0;

    HRESULT hr = surf->Restore();
    static LONG seen = 0;
    if (InterlockedIncrement(&seen) <= 4)
        log_write("texture: Load this=%p state=%d restore=%08lX name=%s\n",
                  this, loadedState_, hr,
                  ImageName_ ? ImageName_ : "(null)");
    if (hr < 0)
        return (unsigned int)hr & 0xffffff00u;

    unsigned int last;
    if (loadedState_ == 1) {
        HANDLE h = LoadImageA(GetModuleHandleA(NULL), ImageName_,
                              IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
        if (h == NULL) {
            h = LoadImageA(NULL, ImageName_, IMAGE_BITMAP, 0, 0,
                           LR_LOADFROMFILE | LR_CREATEDIBSECTION);
            if (h == NULL)
                return 0;
        }
        unsigned int ok = TextureDIB_BlitToSurface(this, h);
        if ((ok & 0xff) == 0) {
            unsigned int d = (unsigned int)DeleteObject((HGDIOBJ)h);
            return d & 0xffffff00u;
        }
        last = (unsigned int)DeleteObject((HGDIOBJ)h);
    } else {
        last = (unsigned int)(loadedState_ - 2);
        if (last == 0) {
            last = TextureTGA_Parse(this, ImageName_);
            if ((last & 0xff) == 0)
                return last;
        }
    }

    return (last & 0xffffff00u) | 1u;
}
