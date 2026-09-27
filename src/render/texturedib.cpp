/* The LoadedImage DIB loader: load a .bmp and blit it into a DirectDraw
 * surface.
 *
 * Both functions return a bool in the low byte and leave the upper three bytes
 * as whatever the last call returned; callers test the low byte only.  Each
 * return below names the call its upper bytes come from. */

#include <string.h>
#include "texture.h"
#include "d3dnative.h"
#include "log.h"
#include <stdlib.h>
#include <stdio.h>
#include "gamestr.h"
#include "gameglobals.h"

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Texture_ReleaseSurfaces(LoadedImage *self);

/* The name-copy length, computed here rather than by the CRT. */
static unsigned int dib_strlen(const char *s)
{
    const char *p = s;
    while (*p != '\0')
        ++p;
    return (unsigned int)(p - s);
}

extern "C" {

/* ─── BlitToSurface ─────────────────────────────────────────────────────────
 *
 * Blits a GDI bitmap into pTextureSurface by way of a temporary system-memory
 * DirectDraw surface: create it with the destination's pixel format, GetDC it,
 * BitBlt the DIB in, then BltFast the result across. */
__declspec(dllexport) unsigned int __attribute__((thiscall))
TextureDIB_BlitToSurface(LoadedImage *self, HANDLE hbmp)
{
    DDSURFACEDESC2 ddsd;
    ddsd.dwSize = sizeof(DDSURFACEDESC2);  // written before GetObjectA

    BITMAP bm;
    GetObjectA(hbmp, sizeof(BITMAP), &bm);

    static LONG seen_blit = 0;
    if (InterlockedIncrement(&seen_blit) <= 4)
        log_write("texturedib: Blit this=%p hbmp=%p %ldx%ld bpp=%d\n",
                  self, hbmp, bm.bmWidth, bm.bmHeight, bm.bmBitsPixel);

    HDC hdcSrc = CreateCompatibleDC(NULL);
    SelectObject(hdcSrc, (HGDIOBJ)hbmp);

    // Inherit the destination's description, then override only what makes
    // this a system-memory scratch copy.  Unlike CreateSurface, ddsd is never
    // zeroed: the rest is whatever GetSurfaceDesc left.
    self->pTextureSurface->GetSurfaceDesc(&ddsd);
    ddsd.dwFlags        = 0x1007;  // CAPS | HEIGHT | WIDTH | PIXELFORMAT
    ddsd.ddsCaps.dwCaps = 0x1800;  // TEXTURE | SYSTEMMEMORY

    // The interface the surface was created from, which is an IDirectDraw4.
    IDirectDraw4 *dd = NULL;
    self->pTextureSurface->GetDDInterface((void **)&dd);

    IDirectDrawSurface4 *tmp = NULL;
    HRESULT hr = dd->CreateSurface(&ddsd, &tmp, NULL);
    if (hr < 0) {
        unsigned int r = fwrite(GS_TEX_CREATESURFACE_FAILED,
                                        (int)dib_strlen(GS_TEX_CREATESURFACE_FAILED),
                                        1, stderr);
        self->loadStatus = 3;
        return r & 0xffffff00u;  // upper bytes: fwrite
    }

    HDC hdcDst = NULL;
    hr = tmp->GetDC(&hdcDst);
    if (hr < 0) {
        unsigned int r = fwrite(GS_TEX_GETDC_FAILED,
                                        (int)dib_strlen(GS_TEX_GETDC_FAILED),
                                        1, stderr);
        self->loadStatus = 4;
        return r & 0xffffff00u;  // upper bytes: fwrite
    }

    BitBlt(hdcDst, 0, 0, bm.bmWidth, bm.bmHeight, hdcSrc, 0, 0, SRCCOPY);
    tmp->ReleaseDC(hdcDst);

    self->pTextureSurface->BltFast(0, 0, tmp, NULL, DDBLTFAST_WAIT);

    if (tmp != NULL)  // always true here
        tmp->Release();

    unsigned int last = (unsigned int)DeleteDC(hdcSrc);
    return (last & 0xffffff00u) | 1u;  // upper bytes: DeleteDC
}

/* ─── CreateSurface ─────────────────────────────────────────────────────────
 *
 * Loads a BMP, creates a matching DirectDraw surface for it, fills it via
 * BlitToSurface and records the file name.  bSysMem adds DDSCAPS_SYSTEMMEMORY
 * to DDSCAPS_OFFSCREENPLAIN. */
__declspec(dllexport) unsigned int __attribute__((thiscall))
TextureDIB_CreateSurface(LoadedImage *self, RenderDevice *dev, LPCSTR name,
                         char bSysMem)
{
    IDirectDraw4 *dd = dev->native()->dd;
    // The first load asks for a BITMAP resource of that name in the exe, which
    // fails for every real texture; the fallback loads the file.
    HANDLE hbmp = LoadImageA(GetModuleHandleA(NULL), name, IMAGE_BITMAP,
                             0, 0, LR_CREATEDIBSECTION);
    if (hbmp == NULL) {
        hbmp = LoadImageA(NULL, name, IMAGE_BITMAP, 0, 0,
                          LR_LOADFROMFILE | LR_CREATEDIBSECTION);
        if (hbmp == NULL) {
            self->loadStatus = 1;
            return 0;
        }
    }

    static LONG seen_create = 0;
    if (InterlockedIncrement(&seen_create) <= 4)
        log_write("texturedib: CreateSurface this=%p dd=%p sysmem=%d name=%s\n",
                  self, dd, (int)bSysMem, name ? name : "(null)");

    Texture_ReleaseSurfaces(self);

    BITMAP bm;
    GetObjectA(hbmp, sizeof(BITMAP), &bm);

    DDSURFACEDESC2 ddsd;
    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize         = sizeof(DDSURFACEDESC2);
    ddsd.dwFlags        = 7;  // CAPS | HEIGHT | WIDTH
    ddsd.dwHeight       = (DWORD)bm.bmHeight;
    ddsd.dwWidth        = (DWORD)bm.bmWidth;
    ddsd.ddsCaps.dwCaps = bSysMem ? 0x840  // OFFSCREENPLAIN | SYSTEMMEMORY
                                  : 0x40;  // OFFSCREENPLAIN

    HRESULT hr = dd->CreateSurface(&ddsd, &self->pTextureSurface, NULL);
    if (hr < 0) {
        unsigned int d = (unsigned int)DeleteObject((HGDIOBJ)hbmp);
        self->loadStatus = 2;
        return d & 0xffffff00u;  // upper bytes: DeleteObject
    }

    if ((TextureDIB_BlitToSurface(self, hbmp) & 0xff) == 0) {
        DeleteObject((HGDIOBJ)hbmp);
        Texture_ReleaseSurfaces(self);
        return 0;
    }

    if (self->ImageName != NULL)
        free(self->ImageName);

    char *copy = (char *)malloc(dib_strlen(name) + 1u);
    self->ImageName = copy;
    sprintf(copy, GS_FMT_S, name);

    self->loadedState = 1;

    unsigned int last = (unsigned int)DeleteObject((HGDIOBJ)hbmp);
    return (last & 0xffffff00u) | 1u;  // upper bytes: DeleteObject
}

}  // extern "C"
