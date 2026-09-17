/* LoadedImage DIB loader reimplementation.
 *
 *   0x43de40 LoadedImage::CreateSurfaceDIB   __thiscall, ret 0xc (3 stack args)
 *            — 3 E8 call sites, no E9/PUSH/DATA refs, no vtable slots
 *   0x43dfc0 LoadedImage::BlitDIBToSurface   __thiscall, ret 0x4 (1 stack arg)
 *            — 3 E8 call sites, no E9/PUSH/DATA refs, no vtable slots
 *
 * Both were reconstructed from the *disassembly*, not the decompile.
 * BlitDIBToSurface decompiles with "Exceeded maximum restarts": every COM call
 * loses its arguments and the bitmap dimensions appear as phantom `unaff_ESI`
 * / `unaff_EBX`.  The stack frame is unambiguous once laid out by hand, and it
 * tiles exactly, which is what confirms it:
 *
 *   CreateSurfaceDIB  sub esp,0x94  BITMAP @ +0x00 (0x18)  DDSURFACEDESC2 @ +0x18 (0x7c)
 *   BlitDIBToSurface  sub esp,0xa0  3 dwords @ +0x00       BITMAP @ +0x0c (0x18)
 *                                   DDSURFACEDESC2 @ +0x24 (0x7c)
 *
 * 0x18 + 0x7c == 0x94 and 0x0c + 0x18 + 0x7c == 0xa0, with no slack in either.
 *
 * Argument order comes from the frame, not from Ghidra's parameter list
 * (CreateSurfaceDIB is `ret 0xc`; its args sit at frame+0x98/+0x9c/+0xa0):
 *   CreateSurfaceDIB(this, IDirectDraw4 *dd, LPCSTR name, char bSysMem)
 * Ghidra types the first as `int *` and the second as `uchar *`; the order is
 * the same, but the types are only recoverable from use — arg1 is dispatched
 * through vtable +0x18 (IDirectDraw4::CreateSurface) and arg2 is handed to
 * LoadImageA as a filename.
 *
 * Two IDirectDrawSurface4 slots that RENDER_PLAN.md listed as unconfirmed are
 * settled here, from BlitDIBToSurface's dispatches:
 *   +0x58 -> slot 22 GetSurfaceDesc      (was "probably GetSurfaceDesc")
 *   +0x90 -> slot 36 GetDDInterface      (was "GetDDInterface or GetPixelFormat")
 *
 * Preserved deliberately:
 *
 *  - The odd return convention.  Both functions return a bool in AL and leave
 *    the upper three bytes of EAX as whatever the last call happened to
 *    return.  Callers test AL only, but the replacement reproduces the whole
 *    dword; each return below names the call its upper bytes come from.
 *  - The shared `push esi` at 0x43df1b, which is DeleteObject's argument on
 *    the CreateSurface-failure branch and BlitDIBToSurface's argument on the
 *    success branch.  Nothing to reproduce in C, but it is why the failure
 *    path reads as though it deletes the bitmap twice.
 *  - BlitDIBToSurface does *not* zero its descriptor.  It inherits the
 *    destination's via GetSurfaceDesc and overwrites only dwFlags and dwCaps,
 *    keeping the inherited pixel format.  CreateSurfaceDIB, by contrast, does
 *    zero its own.  Not unified.
 *  - CreateSurfaceDIB's name copy is `operator new(strlen+1)` + sprintf("%s"),
 *    not a strdup.  Its inline REPNE SCASB length is `not ecx` alone, giving
 *    strlen+1; BlitDIBToSurface's two error paths use the same idiom with a
 *    trailing `dec ecx`, so they log the true length.  Both as written.
 *  - The first LoadImageA passes LR_CREATEDIBSECTION only (0x2000), with
 *    hInst = the exe module, so it looks for a BITMAP *resource* of that name
 *    and fails for every real texture; the fallback adds LR_LOADFROMFILE
 *    (0x2010) and passes hInst=NULL, and is what actually loads the file.
 *    This comment and the code originally read the constant as 0x0010
 *    (LR_LOADFROMFILE), which made the first call succeed and produced a
 *    device-dependent bitmap rather than a DIB section — corrected 2026-09-02
 *    while replacing BindTextureResource, which has the identical prologue.
 *
 * texture.cpp's ORIG_BLIT_DIB pointed at 0x43dfc0, which is now UD2-stubbed,
 * so Texture_Load calls TextureDIB_BlitToSurface directly instead.
 */
#include <string.h>
#include "texture.h"
#include "log.h"
#include "alloc.h"
#include "gamestr.h"
#include "gameglobals.h"

/* The CRT's fwrite — __cdecl(const void *buf, size_t size, size_t count,
 * FILE *stream).  This address was called "ImageLogger::Log" here until
 * 2026-09-04; the name was wrong, the four arguments were always right.  The
 * game logs by calling fwrite(msg, strlen(msg), 1, logFILE), which is why the
 * call sites read like a logger; 0x469cf8 is the log FILE *, not a sink
 * object.  Settled by ASSET_PLAN.md Phase 2: SaveConfig calls the same address
 * as fwrite(blob, 0x144e, 1, fp).  Left live in the binary. */
typedef unsigned int (__cdecl *fwrite_fn)(const char *, int, int, FILE *);
#define ORIG_FWRITE ((fwrite_fn)0x004513c7)

/* operator new — __cdecl(size_t); FactAlloc::Free2 — __cdecl(void *);
 * MaybeSprintf — __cdecl(char *, const char *fmt, ...).  Shared helpers that
 * stay live for the rest of the binary. */
typedef int (__cdecl *sprintf_fn)(char *, const char *, ...);
#define ORIG_MAYBE_SPRINTF ((sprintf_fn)0x00450655)

/* The strings BlitDIBToSurface logs, at their original addresses so the
 * pointer handed to the logger is identical to the original's. */

/* Defined in texture.cpp; the original's 0x43ebb0 is UD2-stubbed. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Texture_ReleaseSurfaces(LoadedImage *self);

/* The inline REPNE SCASB the originals use, so no CRT/import dependency is
 * introduced for something the original computes it(self). */
static unsigned int dib_strlen(const char *s)
{
    const char *p = s;
    while (*p != '\0')
        ++p;
    return (unsigned int)(p - s);
}

extern "C" {

/* ─── LoadedImage::BlitDIBToSurface (0x43dfc0) ─────────────────────────────
 *
 * Blits a GDI bitmap into pTextureSurface by way of a temporary system-memory
 * DirectDraw surface: create it with the destination's pixel format, GetDC it,
 * BitBlt the DIB in, then BltFast the result across.
 */
__declspec(dllexport) unsigned int __attribute__((thiscall))
TextureDIB_BlitToSurface(LoadedImage *self, HANDLE hbmp)
{
    DDSURFACEDESC2 ddsd;
    ddsd.dwSize = sizeof(DDSURFACEDESC2);   /* 0x7c, written before GetObjectA */

    BITMAP bm;
    GetObjectA(hbmp, sizeof(BITMAP), &bm);

    static LONG seen_blit = 0;
    if (InterlockedIncrement(&seen_blit) <= 4)
        log_write("texturedib: Blit this=%p hbmp=%p %ldx%ld bpp=%d\n",
                  self, hbmp, bm.bmWidth, bm.bmHeight, bm.bmBitsPixel);

    HDC hdcSrc = CreateCompatibleDC(NULL);
    SelectObject(hdcSrc, (HGDIOBJ)hbmp);

    /* Inherit the destination's description, then override only what makes
     * this a system-memory scratch copy.  The rest of ddsd is whatever
     * GetSurfaceDesc left; this function never zeroes it. */
    self->pTextureSurface->GetSurfaceDesc(&ddsd);
    ddsd.dwFlags        = 0x1007;   /* CAPS | HEIGHT | WIDTH | PIXELFORMAT */
    ddsd.ddsCaps.dwCaps = 0x1800;   /* TEXTURE | SYSTEMMEMORY */

    /* GetDDInterface hands back the interface the surface was created from,
     * which for these surfaces is the IDirectDraw4 — confirmed by the
     * dispatch at +0x18 below being CreateSurface, which is IDirectDraw4's
     * layout, not IDirectDraw's. */
    IDirectDraw4 *dd = NULL;
    self->pTextureSurface->GetDDInterface((void **)&dd);

    IDirectDrawSurface4 *tmp = NULL;
    HRESULT hr = dd->CreateSurface(&ddsd, &tmp, NULL);
    if (hr < 0) {
        unsigned int r = ORIG_FWRITE(GS_TEX_CREATESURFACE_FAILED,
                                        (int)dib_strlen(GS_TEX_CREATESURFACE_FAILED),
                                        1, GG_LOG_STREAM);
        self->loadStatus = 3;
        return r & 0xffffff00u;             /* upper bytes: the CRT fwrite  */
    }

    HDC hdcDst = NULL;
    hr = tmp->GetDC(&hdcDst);
    if (hr < 0) {
        unsigned int r = ORIG_FWRITE(GS_TEX_GETDC_FAILED,
                                        (int)dib_strlen(GS_TEX_GETDC_FAILED),
                                        1, GG_LOG_STREAM);
        self->loadStatus = 4;
        return r & 0xffffff00u;             /* upper bytes: the CRT fwrite  */
    }

    BitBlt(hdcDst, 0, 0, bm.bmWidth, bm.bmHeight, hdcSrc, 0, 0, SRCCOPY);
    tmp->ReleaseDC(hdcDst);

    self->pTextureSurface->BltFast(0, 0, tmp, NULL, DDBLTFAST_WAIT);

    if (tmp != NULL)                        /* the original re-tests it */
        tmp->Release();

    unsigned int last = (unsigned int)DeleteDC(hdcSrc);
    return (last & 0xffffff00u) | 1u;       /* upper bytes: DeleteDC */
}

/* ─── LoadedImage::CreateSurfaceDIB (0x43de40) ─────────────────────────────
 *
 * Loads a BMP from disk, creates a matching DirectDraw surface for it, fills
 * it via BlitDIBToSurface and records the file name.  bSysMem adds
 * DDSCAPS_SYSTEMMEMORY to DDSCAPS_OFFSCREENPLAIN.
 */
__declspec(dllexport) unsigned int __attribute__((thiscall))
TextureDIB_CreateSurface(LoadedImage *self, IDirectDraw4 *dd, LPCSTR name,
                         char bSysMem)
{
    HANDLE hbmp = LoadImageA(GetModuleHandleA(NULL), name, IMAGE_BITMAP,
                             0, 0, LR_CREATEDIBSECTION);
    if (hbmp == NULL) {
        hbmp = LoadImageA(NULL, name, IMAGE_BITMAP, 0, 0,
                          LR_LOADFROMFILE | LR_CREATEDIBSECTION);
        if (hbmp == NULL) {
            self->loadStatus = 1;
            return 0;                       /* EAX is exactly 0 here */
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
    memset(&ddsd, 0, sizeof(ddsd));         /* rep stosd, 0x1f dwords */
    ddsd.dwSize         = sizeof(DDSURFACEDESC2);
    ddsd.dwFlags        = 7;                /* CAPS | HEIGHT | WIDTH */
    ddsd.dwHeight       = (DWORD)bm.bmHeight;
    ddsd.dwWidth        = (DWORD)bm.bmWidth;
    ddsd.ddsCaps.dwCaps = bSysMem ? 0x840   /* OFFSCREENPLAIN | SYSTEMMEMORY */
                                  : 0x40;   /* OFFSCREENPLAIN */

    HRESULT hr = dd->CreateSurface(&ddsd, &self->pTextureSurface, NULL);
    if (hr < 0) {
        unsigned int d = (unsigned int)DeleteObject((HGDIOBJ)hbmp);
        self->loadStatus = 2;
        return d & 0xffffff00u;             /* upper bytes: DeleteObject */
    }

    if ((TextureDIB_BlitToSurface(self, hbmp) & 0xff) == 0) {
        DeleteObject((HGDIOBJ)hbmp);
        Texture_ReleaseSurfaces(self);
        /* upper bytes: ReleaseTextureSurfaces, which returns void — the
         * original reads back whatever it happened to leave in EAX.  AL is
         * zero either way, which is all any caller tests. */
        return 0;
    }

    if (self->ImageName != NULL)
        game_free2(self->ImageName);

    /* strlen+1: the original's `not ecx` with no matching `dec ecx`. */
    char *copy = (char *)game_operator_new(dib_strlen(name) + 1u);
    self->ImageName = copy;
    ORIG_MAYBE_SPRINTF(copy, GS_FMT_S, name);

    self->loadedState = 1;

    unsigned int last = (unsigned int)DeleteObject((HGDIOBJ)hbmp);
    return (last & 0xffffff00u) | 1u;       /* upper bytes: DeleteObject */
}

} // extern "C"
