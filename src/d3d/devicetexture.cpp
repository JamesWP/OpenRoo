/* RenderDevice's textures and images (renderdevice.h): the Direct3D texture a
 * DeviceTexture holds, the choice of its pixel format, and the conversion of
 * an Image's RGBA8 pixels into that format (pixelconvert.cpp).
 *
 * Format choice.  The depth asked for is `depth` if that is exactly 16 or 32,
 * else the image's own depth, so an 8-bit image gets a 16-bit format and a
 * 24-bit image a 32-bit one.  Each depth has a short preference list --
 * with or without alpha -- and the first format the device supports wins.
 *
 * Textures live in the managed pool, so they survive a device Reset.  A
 * headless device makes no texture at all; it only converts, when
 * KAROO_TEXTURE_DUMP wants to see the pixels. */

#include <fstream>
#include <stdio.h>
#include <string.h>
#include "d3dnative.h"
#include "image.h"
#include "logger.h"
#include "sysdev.h"

struct DeviceTexture {
    IDirect3DTexture9 *texture;  // NULL when headless
    D3DFORMAT          format;
    bool               alpha;
    int                width, height;
};

IDirect3DTexture9 *d3d_texture_object(const DeviceTexture *t)
{
    return t ? t->texture : NULL;
}

bool d3d_texture_has_alpha(const DeviceTexture *t)
{
    return t->alpha;
}

PixelFormat d3d_pixel_format(D3DFORMAT f)
{
    switch (f) {
    case D3DFMT_A8R8G8B8: return { 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000 };
    case D3DFMT_R5G6B5:   return { 16, 0x0000F800, 0x000007E0, 0x0000001F, 0x00000000 };
    case D3DFMT_X1R5G5B5: return { 16, 0x00007C00, 0x000003E0, 0x0000001F, 0x00000000 };
    case D3DFMT_A1R5G5B5: return { 16, 0x00007C00, 0x000003E0, 0x0000001F, 0x00008000 };
    case D3DFMT_A4R4G4B4: return { 16, 0x00000F00, 0x000000F0, 0x0000000F, 0x0000F000 };
    default:              return { 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0x00000000 };  // X8R8G8B8
    }
}

// ── The texture dump ──

bool d3d_dump_enabled()
{
    static int enabled = -1;
    if (enabled < 0) {
        char path[4];
        enabled = sysdev::getEnv("KAROO_TEXTURE_DUMP", path, sizeof(path)) ? 1 : 0;
    }
    return enabled != 0;
}

void d3d_dump_pixels(const char *kind, const char *name, int width, int height,
                     const PixelFormat &pf, const uint8_t *bits, long pitch)
{
    static char path[MAX_PATH];
    if (!d3d_dump_enabled())
        return;
    if (path[0] == '\0')
        sysdev::getEnv("KAROO_TEXTURE_DUMP", path, sizeof(path));

    unsigned long long h = 1469598103934665603ull;
    const unsigned rowBytes = (unsigned)width * (pf.bits / 8);
    for (int y = 0; y < height; ++y, bits += pitch)
        for (unsigned x = 0; x < rowBytes; ++x)
            h = (h ^ bits[x]) * 1099511628211ull;

    char line[512];
    snprintf(line, sizeof(line),
             "%s|%s|%dx%d|%ubpp r=%08lX g=%08lX b=%08lX a=%08lX|%016llX\n",
             kind, name, width, height, pf.bits,
             (unsigned long)pf.rMask, (unsigned long)pf.gMask,
             (unsigned long)pf.bMask, (unsigned long)pf.aMask, h);
    std::ofstream f(path, std::ios::binary | std::ios::app);
    f << line;
}

// ── Format choice ──

static D3DFORMAT pick_format(RenderDevice::Native *n, unsigned depth, bool alpha)
{
    static const D3DFORMAT p32alpha[] = { D3DFMT_A8R8G8B8 };
    static const D3DFORMAT p32[]      = { D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8 };
    static const D3DFORMAT p16alpha[] = { D3DFMT_A4R4G4B4, D3DFMT_A1R5G5B5, D3DFMT_A8R8G8B8 };
    static const D3DFORMAT p16[]      = { D3DFMT_X1R5G5B5, D3DFMT_R5G6B5, D3DFMT_X8R8G8B8 };

    const D3DFORMAT *list;
    size_t count;
    if (depth > 16) {
        list = alpha ? p32alpha : p32;
        count = alpha ? sizeof p32alpha / sizeof *p32alpha : sizeof p32 / sizeof *p32;
    } else {
        list = alpha ? p16alpha : p16;
        count = alpha ? sizeof p16alpha / sizeof *p16alpha : sizeof p16 / sizeof *p16;
    }
    // Headless has no device to ask; the first choice stands.
    if (!n->device)
        return list[0];
    for (size_t i = 0; i < count; i++)
        if (SUCCEEDED(n->d3d->CheckDeviceFormat(n->adapter, D3DDEVTYPE_HAL,
                                                n->displayFormat, 0,
                                                D3DRTYPE_TEXTURE, list[i])))
            return list[i];
    return D3DFMT_A8R8G8B8;
}

static bool format_has_alpha(D3DFORMAT f)
{
    return d3d_pixel_format(f).aMask != 0;
}

/* Converts img into t's texture, or, headless, into a scratch buffer that
 * only the dump looks at. */
static bool fill_texture(DeviceTexture *t, const Image &img)
{
    const PixelFormat pf = d3d_pixel_format(t->format);
    if (t->texture) {
        D3DLOCKED_RECT lr;
        if (FAILED(t->texture->LockRect(0, &lr, NULL, 0))) {
            g_logger.write("devicetexture: LockRect failed (%s)\n", img.name);
            return false;
        }
        PixelConvert_ToTexture(img, pf, (uint8_t *)lr.pBits, lr.Pitch);
        d3d_dump_pixels("tex", img.name, img.width, img.height, pf,
                        (const uint8_t *)lr.pBits, lr.Pitch);
        t->texture->UnlockRect(0);
    } else if (d3d_dump_enabled()) {
        std::vector<uint8_t> buf((size_t)img.width * img.height * (pf.bits / 8));
        const long pitch = (long)img.width * (pf.bits / 8);
        PixelConvert_ToTexture(img, pf, buf.data(), pitch);
        d3d_dump_pixels("tex", img.name, img.width, img.height, pf, buf.data(), pitch);
    }
    return true;
}

// ── RenderDevice ──

DeviceTexture *RenderDevice::CreateTexture(const Image &img, uint32_t flags,
                                           unsigned depth)
{
    if (img.empty())
        return NULL;

    // Only an exact 16 or 32 is honoured.
    if (depth != 16 && depth != 32)
        depth = img.sourceBits;

    DeviceTexture *t = new DeviceTexture();
    t->format = pick_format(native_, depth, (flags & TextureFlag::Alpha) != 0);
    t->alpha  = format_has_alpha(t->format);
    t->width  = img.width;
    t->height = img.height;

    static LONG seen = 0;
    if (InterlockedIncrement(&seen) <= 8)
        g_logger.write("devicetexture: %s req=%u -> format %d\n",
                       img.name, depth, (int)t->format);

    if (native_->device) {
        HRESULT hr = native_->device->CreateTexture(
            img.width, img.height, 1, 0, t->format, D3DPOOL_MANAGED,
            &t->texture, NULL);
        if (FAILED(hr)) {
            g_logger.write("devicetexture: couldn't create texture for %s (%08lX)\n",
                           img.name, (unsigned long)hr);
            delete t;
            return NULL;
        }
    }
    if (!fill_texture(t, img)) {
        DestroyTexture(t);
        return NULL;
    }
    return t;
}

bool RenderDevice::UpdateTexture(DeviceTexture *t, const Image &img)
{
    if (t == NULL || img.width != t->width || img.height != t->height)
        return false;
    return fill_texture(t, img);
}

void RenderDevice::DestroyTexture(DeviceTexture *t)
{
    if (t == NULL)
        return;
    if (t->texture != NULL)
        t->texture->Release();
    delete t;
}

// ── Images ──
//
// PresentImage's source: the image converted into a system-memory surface in
// the display's format, copied to video memory and stretched over the back
// buffer.

void RenderDevice::PresentImage(const Image &img)
{
    Native *n = native_;
    const PixelFormat pf = d3d_pixel_format(d3d_display_format(mode_->dwBitDepth));

    if (!n->device) {
        if (!img.empty() && d3d_dump_enabled()) {
            const long pitch = (long)img.width * (pf.bits / 8);
            std::vector<uint8_t> buf((size_t)img.height * pitch);
            PixelConvert_ToDisplay(img, pf, buf.data(), pitch);
            d3d_dump_pixels("img", img.name, img.width, img.height, pf,
                            buf.data(), pitch);
        }
        return;
    }

    bool ok = false;
    if (!img.empty()) {
        if (n->imageW != img.width || n->imageH != img.height)
            d3d_release_image_surfaces(n);
        if (!n->imageSys &&
            SUCCEEDED(n->device->CreateOffscreenPlainSurface(
                img.width, img.height, n->displayFormat, D3DPOOL_SYSTEMMEM,
                &n->imageSys, NULL)) &&
            SUCCEEDED(n->device->CreateOffscreenPlainSurface(
                img.width, img.height, n->displayFormat, D3DPOOL_DEFAULT,
                &n->imageGpu, NULL))) {
            n->imageW = img.width;
            n->imageH = img.height;
        }
        D3DLOCKED_RECT lr;
        if (n->imageW == img.width && n->imageGpu &&
            SUCCEEDED(n->imageSys->LockRect(&lr, NULL, 0))) {
            PixelConvert_ToDisplay(img, pf, (uint8_t *)lr.pBits, lr.Pitch);
            d3d_dump_pixels("img", img.name, img.width, img.height, pf,
                            (const uint8_t *)lr.pBits, lr.Pitch);
            n->imageSys->UnlockRect();

            IDirect3DSurface9 *back = NULL;
            if (SUCCEEDED(n->device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back))) {
                ok = SUCCEEDED(n->device->UpdateSurface(n->imageSys, NULL, n->imageGpu, NULL))
                  && SUCCEEDED(n->device->StretchRect(n->imageGpu, NULL, back, NULL,
                                                      D3DTEXF_LINEAR));
                back->Release();
            }
        }
    }

    Present();

    static LONG logged = 0;
    if (InterlockedIncrement(&logged) <= 8)
        g_logger.write("renderdevice: PresentImage %s %dx%d %s\n",
                       img.name, img.width, img.height, ok ? "ok" : "not drawn");
}
