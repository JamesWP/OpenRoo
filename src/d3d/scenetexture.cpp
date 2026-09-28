/* SceneTexture's loaders, the texture-format picker, the SceneTexture
 * lifecycle and TextureManager (scenetexture.h).
 *
 * SelectTextureLoader picks a loader by file extension (or by an explicit
 * mode); BindTextureResource is the BMP/DIB path and ImportSceneTextures the
 * TGA path.  Both build the DirectDraw texture surface and hand the pixels to
 * TextureDIB_BlitToSurface or TextureTGA_Parse.
 *
 * Shared by both loaders: - The texture memory pool is chosen from the
 * hardware device
 *    description's dcmColorModel, not from a capability flag; the software
 *    description is fetched and never read.
 * - The bit-depth argument is honoured only if it is exactly 16 or 32;
 *    anything else, including 24, takes the source image's own depth.
 * - Only the low byte of the result is the success flag; each failure
 *    return names the call its upper bytes come from. */

#include <string.h>
#include <stdio.h>
#include "scenetexture.h"
#include "tga.h"
#include "log.h"
#include <stdlib.h>
#include "gamestr.h"
#include "gameglobals.h"
#include "gamelog.h"
#include "d3dnative.h"
#include "ddrawdiag.h"

/* LoadedImage and SceneTexture are packed for the packed records that embed
 * them; their members are 4-aligned all the same, so passing a member's
 * address as a COM out-parameter is safe. */
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
TextureManager g_textureManager;

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
TextureDIB_BlitToSurface(LoadedImage *self, HANDLE hbmp);
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
TextureTGA_Parse(LoadedImage *self, LPCSTR path);

/* D3DDEVICEDESC as 0x3f raw dwords, zeroed, dwSize at [0], so the size used is
 * 0xfc whatever the SDK header defines.  Only dcmColorModel is read. */
struct DevDescRaw { DWORD dw[0x3f]; };
static_assert(sizeof(DevDescRaw) == 0xfc, "D3DDEVICEDESC must be 0xfc bytes");
#define DEVDESC_COLORMODEL 2

/* The true length, for the log calls; the name copies allocate one more. */
static unsigned int st_strlen(const char *s)
{
    const char *p = s;
    while (*p != '\0')
        ++p;
    return (unsigned int)(p - s);
}

static void st_log_str(const char *s)
{
    fwrite(s, (int)st_strlen(s), 1, stderr);
}

/* 0 when equal, otherwise -1 or 1 from the first differing byte. */
static int st_strcmp(const unsigned char *a, const unsigned char *b)
{
    for (;;) {
        if (*a != *b)
            return (*a < *b) ? -1 : 1;
        if (*a == 0)
            return 0;
        ++a; ++b;
    }
}

/* Replace the image name: free the old, allocate strlen+1, sprintf("%s"). */
void SceneTexture::setImageName(LPCSTR name)
{
    if (ImageName_ != NULL)
        free(ImageName_);
    char *copy = (char *)malloc(st_strlen(name) + 1u);
    ImageName_ = copy;
    sprintf(copy, GS_FMT_S, name);
}

/* The texture surface's DDSCAPS, from the hardware device description. */
static DWORD st_texture_caps(const DevDescRaw *hw)
{
    return hw->dw[DEVDESC_COLORMODEL] != 0
             ? (DWORD)(DDSCAPS_TEXTURE)                          // 0x1000
             : (DWORD)(DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY);  // 0x1800
}

/* pow(2.0, log(n)/log(2.0)), which is n again to within rounding.  It only
 * ever reaches a log line. */
static double st_size_report_value(unsigned int n)
{
    double      ln2 = (double)__builtin_logl(2.0L);
    long double e   = __builtin_logl((long double)(int)n) / (long double)ln2;
    return (double)__builtin_powl(2.0L, e);
}

/* KAROO_TEXTURE_FX:
 *   bpp16    force the resolved bit depth to 16 in both loaders: banding on
 *            the sky gradient and every smooth-shaded texture.
 *   solid    once the decoder has filled the texture surface, overwrite it
 *            with flat magenta.
 *   deepfmt  reverse the picker's preference (see st_fmt_preferred). */
enum TextureFx { TEXFX_OFF = 0, TEXFX_BPP16, TEXFX_SOLID, TEXFX_DEEPFMT };

static int texture_fx_mode(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = TEXFX_OFF;
        if (GetEnvironmentVariableA("KAROO_TEXTURE_FX", buf, sizeof(buf))) {
            if (lstrcmpiA(buf, "bpp16") == 0)
                cached = TEXFX_BPP16;
            else if (lstrcmpiA(buf, "solid") == 0)
                cached = TEXFX_SOLID;
            else if (lstrcmpiA(buf, "deepfmt") == 0)
                cached = TEXFX_DEEPFMT;
        }
        log_write("scenetexture: FX mode = %s\n",
                  cached == TEXFX_BPP16 ? "bpp16" :
                  cached == TEXFX_SOLID ? "solid" :
                  cached == TEXFX_DEEPFMT ? "deepfmt" : "off");
    }
    return cached;
}

static bool texture_fx_bpp16(void) { return texture_fx_mode() == TEXFX_BPP16; }

/* What the picker chose, to tell "this code did not reach the choice" from
 * "the picker returns the same format whatever it is asked". */
static void texture_log_format(const char *who, UINT requested,
                               const DDPIXELFORMAT *pf)
{
    static LONG seen = 0;
    if (InterlockedIncrement(&seen) <= 8)
        log_write("scenetexture: %s req=%u -> chosen %lubpp flags=%08lX "
                  "r=%08lX g=%08lX b=%08lX a=%08lX\n",
                  who, requested, (unsigned long)pf->dwRGBBitCount,
                  (unsigned long)pf->dwFlags,
                  (unsigned long)pf->dwRBitMask, (unsigned long)pf->dwGBitMask,
                  (unsigned long)pf->dwBBitMask,
                  (unsigned long)pf->dwRGBAlphaBitMask);
}

/* Flat-fill for KAROO_TEXTURE_FX=solid, through the surface's own pitch and
 * bit count; anything other than 16 or 32 bpp is left alone. */
static void texture_fx_fill_solid(IDirectDrawSurface4 *surf)
{
    DDSURFACEDESC2 d;
    memset(&d, 0, sizeof(d));
    d.dwSize = sizeof(d);
    HRESULT hr = surf->Lock(NULL, &d, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, NULL);
    ddiag_lock(hr, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, &d);

    // Never fail silently: an unlockable surface would look like "the FX did
    // nothing".
    static LONG seen = 0;
    if (InterlockedIncrement(&seen) <= 4)
        log_write("scenetexture: solid fill lock=%08lX %lux%lu %lubpp pitch=%ld\n",
                  (unsigned long)hr, (unsigned long)d.dwWidth,
                  (unsigned long)d.dwHeight,
                  (unsigned long)d.ddpfPixelFormat.dwRGBBitCount,
                  (long)d.lPitch);
    if (hr < 0)
        return;

    BYTE *row = (BYTE *)d.lpSurface;
    for (DWORD y = 0; y < d.dwHeight; ++y, row += d.lPitch) {
        if (d.ddpfPixelFormat.dwRGBBitCount == 32) {
            DWORD *p = (DWORD *)row;
            for (DWORD x = 0; x < d.dwWidth; ++x)
                p[x] = 0xFFFF00FFu;  // opaque magenta
        } else if (d.ddpfPixelFormat.dwRGBBitCount == 16) {
            WORD *p = (WORD *)row;
            for (DWORD x = 0; x < d.dwWidth; ++x)
                p[x] = (WORD)(d.ddpfPixelFormat.dwRBitMask |
                              d.ddpfPixelFormat.dwBBitMask |
                              d.ddpfPixelFormat.dwRGBAlphaBitMask);
        }
    }
    surf->Unlock(NULL);
}

/* ─── The texture-format picker ─────────────────────────────────────────────
 *
 * st_pick_texture_format enumerates the device's texture formats and keeps the
 * one the callback prefers.
 *
 * Gate: DDPF_ALPHA formats are always skipped.  At 8 bits or fewer a format
 * must be palettised and exactly 8 bits wide; above 8 bits it must be
 * DDPF_RGB.
 *
 * Preference: with nothing kept yet, take it.  Otherwise, with no alpha asked
 * for, the format must be at least as deep as the request and strictly closer
 * to it than the kept one -- the smallest depth at or above the request wins,
 * and the first of a tie keeps its place.  With alpha asked for, a format that
 * is not strictly closer can still win as an equal-depth alternative: same bit
 * count, strictly more alpha bits than the kept one, and no more alpha bits
 * than a quarter of its own depth.
 *
 * All the arithmetic is unsigned, including `bits - request` where the kept
 * format is shallower than the request and the subtraction wraps.
 *
 * Both branches log the candidate's six fields before it is kept. */

/* Kernighan's popcount.  The callers compare it truncated to 16 bits. */
static unsigned int st_mask_popcount(DWORD mask)
{
    unsigned int n = 0;
    while (mask != 0) {
        mask &= mask - 1;
        ++n;
    }
    return n;
}

struct PickFormatCtx {
    DWORD         dwRequestedBpp;
    BYTE          bWantAlpha;
    DDPIXELFORMAT kept;
};

/* KAROO_TEXTURE_FX=deepfmt keeps the format furthest above the request instead
 * of the closest.  The gate is untouched, so the choice is still an RGB format
 * the device offered. */
static bool st_fmt_preferred(DWORD cand, DWORD kept)
{
    return texture_fx_mode() == TEXFX_DEEPFMT ? (cand > kept) : (cand < kept);
}

/* Always returns D3DENUMRET_OK: every format is enumerated every time. */
static bool s_log_texfmts;  // set by st_pick_texture_format

static HRESULT WINAPI st_enum_texture_formats_picker(LPDDPIXELFORMAT pf,
                                                     LPVOID param)
{
    if (s_log_texfmts)
        ddiag_pixfmt("texfmt", pf);
    DWORD flags = pf->dwFlags;
    if (flags & 0x02)  // DDPF_ALPHA
        return D3DENUMRET_OK;

    DWORD bits = pf->dwRGBBitCount;
    if (bits <= 8) {
        if (!(flags & 0x28))  // DDPF_PALETTEINDEXED4|8
            return D3DENUMRET_OK;
        if (bits != 8)  // 4-bit palettised is rejected
            return D3DENUMRET_OK;
    } else {
        if (!(flags & 0x40))  // DDPF_RGB
            return D3DENUMRET_OK;
    }

    PickFormatCtx *ctx = (PickFormatCtx *)param;
    DWORD req  = ctx->dwRequestedBpp;
    DWORD kept = ctx->kept.dwRGBBitCount;  // 0 until something is kept

    if (ctx->bWantAlpha == 0) {
        if (kept != 0) {
            if (bits < req)
                return D3DENUMRET_OK;
            if (!st_fmt_preferred(bits - req, kept - req))
                return D3DENUMRET_OK;
        }
    } else if (kept != 0) {
        bool closer = (bits >= req) && st_fmt_preferred(bits - req, kept - req);
        if (!closer) {
            if (bits != kept)
                return D3DENUMRET_OK;
            unsigned short keptAlpha =
                (unsigned short)st_mask_popcount(ctx->kept.dwRGBAlphaBitMask);
            unsigned short candAlpha =
                (unsigned short)st_mask_popcount(pf->dwRGBAlphaBitMask);
            if (candAlpha <= keptAlpha)
                return D3DENUMRET_OK;
            if ((DWORD)candAlpha > (bits >> 2))
                return D3DENUMRET_OK;
        }
    }

    char msg[100];
    sprintf(msg, GS_TEX_FMT_PIXELFORMAT, (int)flags, (int)bits,
            (unsigned int)pf->dwRBitMask, (unsigned int)pf->dwGBitMask,
            (unsigned int)pf->dwBBitMask, (unsigned int)pf->dwRGBAlphaBitMask);
    st_log_str(msg);

    ctx->kept = *pf;
    return D3DENUMRET_OK;
}

static void __stdcall st_pick_texture_format(RenderDevice *dev, DWORD bpp,
                                             DWORD alphaFlag,
                                             DDPIXELFORMAT *out)
{
    // REVIEW: the original zeroed one byte short and also wrote the alpha
    // flag into the top byte of the kept format's alpha mask, visible only
    // when no format was kept.  Now the context is zeroed whole.
    PickFormatCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.bWantAlpha     = (BYTE)alphaFlag;
    ctx.dwRequestedBpp = bpp;

    // KAROO_DDRAW_DIAG logs the formats offered the first time only.
    static LONG enumerations = 0;
    s_log_texfmts = InterlockedIncrement(&enumerations) == 1;
    dev->native()->device->EnumTextureFormats(st_enum_texture_formats_picker, &ctx);
    s_log_texfmts = false;

    *out = ctx.kept;
}

extern "C" {

/* ─── The SceneTexture lifecycle ────────────────────────────────────────────
 *
 * The scalar deleting destructor is reachable only through the vtable. */

static void *const g_SceneTextureVtable[1] = { (void *)&SceneTexture::scalarDtor };

static void *scene_vtable(void)
{
    return (void *)g_SceneTextureVtable;
}

void SceneTexture::releaseD3DTexture()
{
    IDirect3DTexture2 *tex = pTexture2_;
    if (tex != NULL)
        tex->Release();
    pTexture2_ = NULL;  // unconditional

    releaseSurfaces();
}

SceneTexture *SceneTexture::construct()
{
    static unsigned long seen; Texture_ImageFirstCall("SceneTexture::Constructor", &seen);
    LoadedImage::construct();
    unknown00_ = scene_vtable();
    pTexture2_      = NULL;
    return this;
}

void SceneTexture::dtorBody()
{
    static unsigned long seen; Texture_ImageFirstCall("SceneTexture::DtorBody", &seen);
    unknown00_ = scene_vtable();  // dead: DtorBody installs the base table
    LoadedImage::dtorBody();
}

SceneTexture *__attribute__((thiscall))
SceneTexture::scalarDtor(SceneTexture *self, unsigned int flags)
{
    static unsigned long seen; Texture_ImageFirstCall("SceneTexture::ScalarDeletingDtor", &seen);
    self->dtorBody();
    if ((flags & 1) != 0)
        free(self);  // flag 1: heap-allocated by its factory
    return self;
}

/* ─── BindTextureResource ───────────────────────────────────────────────────
 *
 * The BMP/DIB path.  Load the file, create a texture surface matching its
 * dimensions and the device's chosen pixel format, attach a palette if the
 * format is 8-bit or less, blit the DIB in and query the IDirect3DTexture2. */
unsigned int SceneTexture::bindTextureResource(RenderDevice *dev, LPCSTR name, UINT bpp,
                            DWORD textureStage)
{
    // Resources first, then the file system; the game ships no bitmap
    // resources, so the first always fails.
    HANDLE hbmp = LoadImageA(GetModuleHandleA(NULL), name, IMAGE_BITMAP,
                             0, 0, LR_CREATEDIBSECTION);
    if (hbmp == NULL) {
        hbmp = LoadImageA(NULL, name, IMAGE_BITMAP, 0, 0,
                          LR_LOADFROMFILE | LR_CREATEDIBSECTION);
        if (hbmp == NULL)
            return 0;
    }

    static LONG seen_bind = 0;
    if (InterlockedIncrement(&seen_bind) <= 4)
        log_write("scenetexture: Bind this=%p dev=%p bpp=%u stage=%lu name=%s\n",
                  this, dev, bpp, (unsigned long)textureStage,
                  name ? name : "(null)");

    releaseD3DTexture();

    BITMAP bm;
    GetObjectA(hbmp, sizeof(BITMAP), &bm);

    DDSURFACEDESC2 ddsd;
    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwWidth        = (DWORD)bm.bmWidth;
    ddsd.dwTextureStage = textureStage;
    ddsd.dwSize         = sizeof(DDSURFACEDESC2);
    ddsd.dwFlags        = 0x101007;  // CAPS|HEIGHT|WIDTH|PIXELFORMAT|TEXTURESTAGE
    ddsd.dwHeight       = (DWORD)bm.bmHeight;

    st_log_str(name);

    // Only an exact 16 or 32 is honoured.
    if (bpp != 16 && bpp != 32)
        bpp = (UINT)((DWORD)bm.bmBitsPixel & 0xffff);

    if (texture_fx_bpp16())
        bpp = 16;

    st_pick_texture_format(dev, bpp, 0, &ddsd.ddpfPixelFormat);
    texture_log_format("Bind", bpp, &ddsd.ddpfPixelFormat);

    DevDescRaw hw, sw;
    HRESULT hr;
    memset(&hw, 0, sizeof(hw));
    memset(&sw, 0, sizeof(sw));
    hw.dw[0] = 0xfc;
    sw.dw[0] = 0xfc;
    hr = dev->native()->device->GetCaps((LPD3DDEVICEDESC)&hw, (LPD3DDEVICEDESC)&sw);
    ddiag_device_caps(hr, &hw, &sw);

    ddsd.ddsCaps.dwCaps = st_texture_caps(&hw);

    hr = dev->native()->dd->CreateSurface(&ddsd, &pTextureSurface_, NULL);
    ddiag_create_surface(hr, &ddsd);
    if (hr < 0) {
        unsigned int d = (unsigned int)DeleteObject((HGDIOBJ)hbmp);
        return d & 0xffffff00u;  // upper bytes: DeleteObject
    }

    // A palettised format gets a palette from the DIB's colour table.
    // pTexturePalette is typed IDirectDrawSurface4 * but holds an
    // IDirectDrawPalette *.
    if (ddsd.ddpfPixelFormat.dwRGBBitCount <= 8) {
        IDirectDrawPalette *pal = Texture_CreatePaletteFromDIB(dev->native()->dd, (HBITMAP)hbmp);
        pTexturePalette_ = (IDirectDrawSurface4 *)pal;
        if (pal != NULL)
            pTextureSurface_->SetPalette(pal);
    }

    if ((TextureDIB_BlitToSurface(this, hbmp) & 0xff) != 0) {
        if (texture_fx_mode() == TEXFX_SOLID)
            texture_fx_fill_solid(pTextureSurface_);
        hr = pTextureSurface_->QueryInterface(IID_IDirect3DTexture2,
                                                        (void **)&pTexture2_);
        if (hr >= 0) {
            setImageName(name);
            loadedState_ = 1;
            unsigned int last = (unsigned int)DeleteObject((HGDIOBJ)hbmp);
            return (last & 0xffffff00u) | 1u;  // upper bytes: DeleteObject
        }
    }

    DeleteObject((HGDIOBJ)hbmp);
    releaseD3DTexture();
    return 0;
}

/* ─── ImportSceneTextures ───────────────────────────────────────────────────
 *
 * The TGA path.  Reads the header itself to get the dimensions and to reject
 * anything that is not a true-colour image, then builds the surface and hands
 * the file to TextureTGA_Parse to decode. */
unsigned int SceneTexture::importSceneTextures(RenderDevice *dev, LPCSTR name,
                            DWORD alphaFlag, UINT bpp, DWORD textureStage)
{
    st_log_str(name);
    st_log_str(GS_FMT_NEWLINE);

    FILE *fp = fopen(name, "rb");
    if (fp == NULL)
        return 0;

    // Header: twelve reads, no error checking.
    TgaHeader h;
    fread(&h.idLength,        1, 1, fp);
    fread(&h.colourMapType,   1, 1, fp);
    fread(&h.imageType,       1, 1, fp);
    fread(&h.colourMapOrigin, 2, 1, fp);
    fread(&h.colourMapLength, 2, 1, fp);
    fread(&h.colourMapDepth,  1, 1, fp);
    fread(&h.xOrigin,         2, 1, fp);
    fread(&h.yOrigin,         2, 1, fp);
    fread(&h.width,           2, 1, fp);
    fread(&h.height,          2, 1, fp);
    fread(&h.bpp,             1, 1, fp);
    fread(&h.descriptor,      1, 1, fp);
    fclose(fp);
    fp = NULL;

    static LONG seen_import = 0;
    if (InterlockedIncrement(&seen_import) <= 4)
        log_write("scenetexture: Import this=%p type=%u %ux%u src=%ubpp req=%u name=%s\n",
                  this, (unsigned)h.imageType, (unsigned)h.width,
                  (unsigned)h.height, (unsigned)h.bpp, bpp,
                  name ? name : "(null)");

    // PRESERVED: the old texture is destroyed before the type is checked, so a
    // bad TGA loses the texture that was there.
    releaseD3DTexture();

    // True-colour only (types 2 and 0x0a), compressed or not; TextureTGA_Parse
    // itself would read other types.
    if (h.imageType != 2 && h.imageType != 0x0a) {
        if (fp != NULL) fclose(fp);
        return 0;
    }

    st_log_str(GS_TEX_TYPE_OK);

    char msg[256];
    unsigned int w = h.width;
    sprintf(msg, GS_TEX_FMT_X_SIZE, w, st_size_report_value(w));
    st_log_str(msg);
    unsigned int ht = h.height;
    sprintf(msg, GS_TEX_FMT_Y_SIZE, ht, st_size_report_value(ht));
    st_log_str(msg);

    DDSURFACEDESC2 ddsd;
    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize         = sizeof(DDSURFACEDESC2);
    ddsd.dwFlags        = 0x101007;
    ddsd.dwTextureStage = textureStage;
    ddsd.dwWidth        = w;
    ddsd.dwHeight       = ht;

    if (bpp != 16 && bpp != 32)
        bpp = (UINT)h.bpp;

    if (texture_fx_bpp16())
        bpp = 16;

    st_pick_texture_format(dev, bpp, alphaFlag, &ddsd.ddpfPixelFormat);
    texture_log_format("Import", bpp, &ddsd.ddpfPixelFormat);

    DevDescRaw hw, sw;
    HRESULT hr;
    memset(&hw, 0, sizeof(hw));
    memset(&sw, 0, sizeof(sw));
    hw.dw[0] = 0xfc;
    sw.dw[0] = 0xfc;
    hr = dev->native()->device->GetCaps((LPD3DDEVICEDESC)&hw, (LPD3DDEVICEDESC)&sw);
    ddiag_device_caps(hr, &hw, &sw);

    ddsd.ddsCaps.dwCaps = st_texture_caps(&hw);

    hr = dev->native()->dd->CreateSurface(&ddsd, &pTextureSurface_, NULL);
    ddiag_create_surface(hr, &ddsd);
    if (hr < 0) {
        st_log_str(GS_TEX_NO_TEXTURE_SURFACE);
        if (fp != NULL) fclose(fp);
        return 0;
    }

    if ((TextureTGA_Parse(this, name) & 0xff) == 0) {
        st_log_str(GS_TEX_NO_TGA_COPY);
        releaseD3DTexture();
        if (fp != NULL) fclose(fp);
        return 0;
    }

    if (texture_fx_mode() == TEXFX_SOLID)
        texture_fx_fill_solid(pTextureSurface_);

    hr = pTextureSurface_->QueryInterface(IID_IDirect3DTexture2,
                                                    (void **)&pTexture2_);
    if (hr < 0) {
        // Release first, then log.
        releaseD3DTexture();
        st_log_str(GS_TEX_NO_TEXTURE_IFACE);
        if (fp != NULL) fclose(fp);
        return 0;
    }

    setImageName(name);
    loadedState_ = 2;
    if (fp != NULL) fclose(fp);
    return 1;
}

/* ─── SelectTextureLoader ───────────────────────────────────────────────────
 *
 * mode 0 picks by extension, 1 forces the DIB loader, 2 forces the TGA loader,
 * anything else fails silently.  The extension test is four case-exact
 * compares against ".bmp", ".BMP", ".tga", ".TGA", so ".Bmp" is rejected. */
unsigned int SceneTexture::selectTextureLoader(RenderDevice *dev, LPCSTR name, UINT bpp,
                            int mode)
{
    if (mode != 0) {
        if (mode == 1)
            return bindTextureResource(dev, name, bpp, 0);
        if (mode == 2)
            return importSceneTextures(dev, name, 0, bpp, 0);
        // The low byte cleared; the upper bytes are mode - 2's.
        return (unsigned int)(mode - 2) & 0xffffff00u;
    }

    // PRESERVED: no NULL check: a name with no '.' faults.
    const unsigned char *ext = (const unsigned char *)strrchr(name, '.');

    if (st_strcmp(ext, (const unsigned char *)GS_TEX_DOT_BMP_LOWER) == 0 ||
        st_strcmp(ext, (const unsigned char *)GS_TEX_DOT_BMP_UPPER) == 0)
        return bindTextureResource(dev, name, bpp, 0);

    if (st_strcmp(ext, (const unsigned char *)GS_TEX_DOT_TGA_LOWER) == 0)
        return importSceneTextures(dev, name, 0, bpp, 0);

    int cmp = st_strcmp(ext, (const unsigned char *)GS_TEX_DOT_TGA_UPPER);
    if (cmp != 0)
        return (unsigned int)cmp & 0xffffff00u;  // upper bytes: the compare

    return importSceneTextures(dev, name, 0, bpp, 0);
}

}  // extern "C"

/* ─── TextureManager ───────────────────────────────────────────────────────
 *
 * GetOrLoad compares names by lowercasing both strings in place and then
 * strcmp'ing, so the caller's buffer and every cached ImageName stay
 * lowercased -- which is why "TM: %s loaded" logs the lowercased name.
 *
 * Only alphaFlag's low byte is meaningful. */
static void tm_lower_inplace(char *s)
{
    // 'A'..'Z' only.
    for (; *s; s++)
        if (*s > '@' && *s < '[')
            *s += ' ';
}

typedef void *(__attribute__((thiscall)) *tm_scalar_dtor_fn)(void *self, unsigned int flags);

static void tm_delete(SceneTexture *t)
{
    tm_scalar_dtor_fn dtor = *(tm_scalar_dtor_fn *)t->vtable();
    dtor(t, 1);
}

SceneTexture *TextureManager::getOrLoad(RenderDevice *dev, char *filename,
                         DWORD alphaFlag, UINT bpp, DWORD textureStage)
{
    for (LinkedListNode *node = cache_.head(); node != NULL; ) {
        SceneTexture *cached = (SceneTexture *)node->value();
        node = node->next();
        tm_lower_inplace(filename);
        tm_lower_inplace(cached->imageName());
        if (strcmp(cached->imageName(), filename) == 0) {
            if (pLogger_ != NULL)
                pLogger_->logMessage(1, GS_TM_FOUND, filename);
            return cached;
        }
    }

    void *mem = malloc(sizeof(SceneTexture));
    SceneTexture *tex = (mem != NULL) ? ((SceneTexture *)mem)->construct() : NULL;
    unsigned int ok = tex->importSceneTextures(dev, filename,
                                                  alphaFlag, bpp, textureStage);
    if ((ok & 0xff) == 0) {
        if (tex != NULL)
            tm_delete(tex);
        if (pLogger_ != NULL)
            pLogger_->logMessage(3, GS_TM_FAILED, filename);
        return NULL;
    }
    if (pLogger_ != NULL)
        pLogger_->logMessage(1, GS_TM_LOADED, filename);
    cache_.append(tex);
    return tex;
}

void TextureManager::releaseAll()
{
    for (LinkedListNode *node = cache_.head(); node != NULL; ) {
        SceneTexture *tex = (SceneTexture *)node->value();
        node = node->next();
        if (tex != NULL) {
            tex->releaseD3DTexture();
            tm_delete(tex);
        }
    }
    cache_.clear();
}

/* ─── TextureManager lifecycle ──────────────────────────────────────────────
 *
 * Every instance is static (scenetexture.h), so nothing deletes one and the
 * scalar dtor's free is never reached. */
static void *const g_TextureManagerVtable[1] = { (void *)&TextureManager::scalarDestructor };

TextureManager *TextureManager::construct()
{
    cache_.init();
    vtable_  = (void *)g_TextureManagerVtable;
    pLogger_ = NULL;
    return this;
}

void TextureManager::destruct()
{
    vtable_ = (void *)g_TextureManagerVtable;
    cache_.destruct();
}

TextureManager *__attribute__((thiscall))
TextureManager::scalarDestructor(TextureManager *self, unsigned char flags)
{
    self->destruct();
    if (flags & 1)
        free(self);
    return self;
}

/* pLogger = logger. */
void TextureManager::setLogger(GameLogger *logger)
{
    pLogger_ = logger;
}

/* LoadedImage::load every non-NULL cached image, head to tail, reading the next
 * pointer before the load. */
void TextureManager::loadAll()
{
    for (LinkedListNode *n = cache_.head(); n != NULL; ) {
        LoadedImage *img = (LoadedImage *)n->value();
        n = n->next();
        if (img != NULL)
            img->load();
    }
}
