/* RenderDevice's textures and images (renderdevice.h): the DirectDraw surface
 * a texture lives in, the choice of its pixel format, and the conversion of
 * an Image's RGBA8 pixels into that format.
 *
 * Format choice.  The device is asked for its texture formats and one is kept
 * (see st_enum_texture_formats_picker).  The depth asked for is `depth` if
 * that is exactly 16 or 32, else the image's own depth, so a 24-bit image gets
 * a 32-bit format.  The texture memory pool comes from the hardware device
 * description's dcmColorModel.
 *
 * Conversion into the chosen format is pixelconvert.cpp's.
 *
 * The pixels are written into a system-memory scratch surface and Blt'ed to
 * the texture surface. */

#include <string.h>
#include <stdio.h>
#include "d3dnative.h"
#include "ddrawdiag.h"
#include "image.h"
#include "pixelconvert.h"
#include "logger.h"

struct DeviceTexture {
    IDirectDrawSurface4 *surface;
    IDirect3DTexture2   *texture;
    DDPIXELFORMAT        format;
    int                  width, height;
};

/* D3DDEVICEDESC as 0x3f raw dwords, zeroed, dwSize at [0], so the size used is
 * 0xfc whatever the SDK header defines.  Only dcmColorModel is read. */
struct DevDescRaw { DWORD dw[0x3f]; };
#define DEVDESC_COLORMODEL 2

/* ─── The texture-format picker ─────────────────────────────────────────────
 *
 * Enumerates the device's texture formats and keeps the one the callback
 * prefers.
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
 * format is shallower than the request and the subtraction wraps. */

struct PickFormatCtx {
    DWORD         dwRequestedBpp;
    BYTE          bWantAlpha;
    DDPIXELFORMAT kept;
};

static bool s_log_texfmts;  // set by pick_texture_format

/* Always returns D3DENUMRET_OK: every format is enumerated every time. */
static HRESULT WINAPI enum_texture_formats_picker(LPDDPIXELFORMAT pf, LPVOID param)
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
            if (!((bits - req) < (kept - req)))
                return D3DENUMRET_OK;
        }
    } else if (kept != 0) {
        bool closer = (bits >= req) && ((bits - req) < (kept - req));
        if (!closer) {
            if (bits != kept)
                return D3DENUMRET_OK;
            unsigned short keptAlpha =
                (unsigned short)PixelMask_Popcount(ctx->kept.dwRGBAlphaBitMask);
            unsigned short candAlpha =
                (unsigned short)PixelMask_Popcount(pf->dwRGBAlphaBitMask);
            if (candAlpha <= keptAlpha)
                return D3DENUMRET_OK;
            if ((DWORD)candAlpha > (bits >> 2))
                return D3DENUMRET_OK;
        }
    }

    ctx->kept = *pf;
    return D3DENUMRET_OK;
}

static DDPIXELFORMAT pick_texture_format(RenderDevice *dev, DWORD bpp, bool wantAlpha)
{
    // The context is zeroed whole; the original zeroed one byte short and
    // wrote the alpha flag into the kept format's alpha mask, visible only
    // when no format was kept.
    PickFormatCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.bWantAlpha     = wantAlpha ? 1 : 0;
    ctx.dwRequestedBpp = bpp;

    // KAROO_DDRAW_DIAG logs the formats offered the first time only.
    static LONG enumerations = 0;
    s_log_texfmts = InterlockedIncrement(&enumerations) == 1;
    dev->native()->device->EnumTextureFormats(enum_texture_formats_picker, &ctx);
    s_log_texfmts = false;
    return ctx.kept;
}

static PixelFormat to_pixel_format(const DDPIXELFORMAT &pf)
{
    PixelFormat f = { pf.dwRGBBitCount, pf.dwRBitMask, pf.dwGBitMask,
                      pf.dwBBitMask, pf.dwRGBAlphaBitMask };
    return f;
}

/* Create a system-memory scratch surface of `pf`, fill it from `img`, and Blt
 * it onto `target`. */
static bool upload_image(IDirectDraw4 *dd, IDirectDrawSurface4 *target,
                         const Image &img, const DDPIXELFORMAT &pf)
{
    DDSURFACEDESC2 ddsd;
    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize         = sizeof(ddsd);
    ddsd.dwFlags        = 0x1007;  // CAPS | HEIGHT | WIDTH | PIXELFORMAT
    ddsd.dwWidth        = (DWORD)img.width;
    ddsd.dwHeight       = (DWORD)img.height;
    ddsd.ddpfPixelFormat = pf;
    ddsd.ddsCaps.dwCaps = 0x1800;  // TEXTURE | SYSTEMMEMORY

    IDirectDrawSurface4 *tmp = NULL;
    HRESULT hr = dd->CreateSurface(&ddsd, &tmp, NULL);
    ddiag_create_surface(hr, &ddsd);
    if (hr < 0) {
        g_logger.write("devicetexture: scratch CreateSurface failed %08lX (%s)\n",
                       (unsigned long)hr, img.name);
        return false;
    }

    // Lock overwrites ddsd with the scratch surface's real pitch and pointer.
    hr = tmp->Lock(NULL, &ddsd, 0, NULL);
    ddiag_lock(hr, 0, &ddsd);
    if (hr < 0) {
        g_logger.write("devicetexture: scratch Lock failed %08lX (%s)\n",
                       (unsigned long)hr, img.name);
        tmp->Release();
        return false;
    }

    PixelConvert_ToTexture(img, to_pixel_format(ddsd.ddpfPixelFormat),
                           (uint8_t *)ddsd.lpSurface, ddsd.lPitch);

    bool ok = tmp->Unlock(NULL) >= 0;
    if (ok)
        target->Blt(NULL, tmp, NULL, DDBLT_WAIT, NULL);
    tmp->Release();
    return ok;
}

/* ─── RenderDevice ──────────────────────────────────────────────────────── */

DeviceTexture *RenderDevice::CreateTexture(const Image &img, uint32_t flags,
                                           unsigned depth)
{
    if (img.empty())
        return NULL;

    // Only an exact 16 or 32 is honoured.
    if (depth != 16 && depth != 32)
        depth = img.sourceBits;

    DDPIXELFORMAT pf = pick_texture_format(this, depth, (flags & TextureFlag::Alpha) != 0);

    static LONG seen = 0;
    if (InterlockedIncrement(&seen) <= 8)
        g_logger.write("devicetexture: %s req=%u -> chosen %lubpp flags=%08lX "
                       "r=%08lX g=%08lX b=%08lX a=%08lX\n",
                       img.name, depth, (unsigned long)pf.dwRGBBitCount,
                       (unsigned long)pf.dwFlags,
                       (unsigned long)pf.dwRBitMask, (unsigned long)pf.dwGBitMask,
                       (unsigned long)pf.dwBBitMask,
                       (unsigned long)pf.dwRGBAlphaBitMask);

    // The memory pool: hardware colour model or not.
    DevDescRaw hw, sw;
    memset(&hw, 0, sizeof(hw));
    memset(&sw, 0, sizeof(sw));
    hw.dw[0] = 0xfc;
    sw.dw[0] = 0xfc;
    HRESULT hr = native_->device->GetCaps((LPD3DDEVICEDESC)&hw, (LPD3DDEVICEDESC)&sw);
    ddiag_device_caps(hr, &hw, &sw);

    DDSURFACEDESC2 ddsd;
    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize          = sizeof(ddsd);
    ddsd.dwFlags         = 0x101007;  // CAPS|HEIGHT|WIDTH|PIXELFORMAT|TEXTURESTAGE
    ddsd.dwTextureStage  = 0;
    ddsd.dwWidth         = (DWORD)img.width;
    ddsd.dwHeight        = (DWORD)img.height;
    ddsd.ddpfPixelFormat = pf;
    ddsd.ddsCaps.dwCaps = hw.dw[DEVDESC_COLORMODEL] != 0
                            ? (DWORD)DDSCAPS_TEXTURE
                            : (DWORD)(DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY);

    DeviceTexture *t = new DeviceTexture();
    t->format = pf;
    t->width  = img.width;
    t->height = img.height;
    hr = native_->dd->CreateSurface(&ddsd, &t->surface, NULL);
    ddiag_create_surface(hr, &ddsd);
    if (hr < 0) {
        g_logger.write("devicetexture: couldn't create texture surface for %s (%08lX)\n",
                       img.name, (unsigned long)hr);
        delete t;
        return NULL;
    }

    if (!upload_image(native_->dd, t->surface, img, pf)) {
        t->surface->Release();
        delete t;
        return NULL;
    }

    hr = t->surface->QueryInterface(IID_IDirect3DTexture2, (void **)&t->texture);
    if (hr < 0) {
        g_logger.write("devicetexture: no Texture-Interface for %s\n", img.name);
        t->surface->Release();
        delete t;
        return NULL;
    }

    ddiag_dump_surface("tex", img.name, t->surface);
    return t;
}

bool RenderDevice::UpdateTexture(DeviceTexture *t, const Image &img)
{
    if (t == NULL || img.width != t->width || img.height != t->height)
        return false;

    // A lost surface (a mode switch, another application's full-screen)
    // comes back empty and is refilled here.
    HRESULT hr = t->surface->Restore();
    if (hr < 0)
        return false;
    return upload_image(native_->dd, t->surface, img, t->format);
}

void RenderDevice::DestroyTexture(DeviceTexture *t)
{
    if (t == NULL)
        return;
    if (t->texture != NULL)
        t->texture->Release();
    if (t->surface != NULL)
        t->surface->Release();
    delete t;
}

void RenderDevice::SetTexture(int stage, const DeviceTexture *tex)
{
    native_->device->SetTexture(stage, tex ? tex->texture : NULL);
}

/* ─── Images ───────────────────────────────────────────────────────────────
 *
 * PresentImage's source: an offscreen system-memory surface in the display's
 * own pixel format, filled from the image. */
/* The back buffer's contents become the image, scaled to fit. */
bool RenderDevice::BltImageToBackBuffer(const Image &img)
{
    Native *n = native_;
    DDSURFACEDESC2 ddsd;
    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    if (FAILED(n->backBuffer->GetSurfaceDesc(&ddsd)))
        return false;
    const DDPIXELFORMAT pf = ddsd.ddpfPixelFormat;
    if (pf.dwRGBBitCount != 16 && pf.dwRGBBitCount != 32)
        return false;

    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize         = sizeof(ddsd);
    ddsd.dwFlags        = 7;  // CAPS | HEIGHT | WIDTH
    ddsd.dwWidth        = (DWORD)img.width;
    ddsd.dwHeight       = (DWORD)img.height;
    ddsd.ddsCaps.dwCaps = 0x840;  // OFFSCREENPLAIN | SYSTEMMEMORY

    IDirectDrawSurface4 *surf = NULL;
    HRESULT hr = n->dd->CreateSurface(&ddsd, &surf, NULL);
    ddiag_create_surface(hr, &ddsd);
    if (hr < 0)
        return false;

    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize = sizeof(ddsd);
    hr = surf->Lock(NULL, &ddsd, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, NULL);
    ddiag_lock(hr, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, &ddsd);
    if (hr < 0) {
        surf->Release();
        return false;
    }
    PixelConvert_ToDisplay(img, to_pixel_format(ddsd.ddpfPixelFormat),
                           (uint8_t *)ddsd.lpSurface, ddsd.lPitch);
    surf->Unlock(NULL);

    ddiag_dump_surface("img", img.name, surf);
    hr = n->backBuffer->Blt(NULL, surf, NULL, DDBLT_WAIT, NULL);
    surf->Release();
    return hr >= 0;
}
