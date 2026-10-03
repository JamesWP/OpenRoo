#include "pixelconvert.h"

unsigned int PixelMask_Popcount(uint32_t mask)
{
    unsigned int n = 0;
    while (mask != 0) {
        mask &= mask - 1;
        ++n;
    }
    return n;
}

unsigned int PixelMask_Shift(uint32_t m)
{
    unsigned int n = 0;
    if (m == 0)
        return 0;
    while ((m & 1) == 0) { m >>= 1; ++n; }
    return n;
}

/* One 0..255 channel value scaled to the width of its mask, truncating, and
 * placed in the mask.  A format without the channel gets nothing. */
static uint32_t pack_channel(uint32_t value, uint32_t mask)
{
    if (mask == 0)
        return 0;
    const uint32_t max = mask >> PixelMask_Shift(mask);
    return ((value * max) / 255u) << PixelMask_Shift(mask);
}

static uint32_t pack_pixel(const PixelFormat &pf, uint32_t r, uint32_t g,
                           uint32_t b, uint32_t a)
{
    return pack_channel(r, pf.rMask) | pack_channel(g, pf.gMask)
         | pack_channel(b, pf.bMask) | pack_channel(a, pf.aMask);
}

static void store_pixel(uint8_t *p, unsigned bits, uint32_t v)
{
    if (bits == 32)
        *(uint32_t *)p = v;
    else
        *(uint16_t *)p = (uint16_t)v;
}

void PixelConvert_ToTexture(const Image &img, const PixelFormat &pf,
                            uint8_t *dst, long pitch)
{
    if (pf.bits != 16 && pf.bits != 32)
        return;

    for (int y = 0; y < img.height; ++y, dst += pitch) {
        uint8_t *p = dst;
        for (int x = 0; x < img.width; ++x, p += pf.bits / 8) {
            const uint8_t *px = img.pixel(x, y);
            store_pixel(p, pf.bits,
                        pack_pixel(pf, px[0], px[1], px[2],
                                   img.hasAlpha ? px[3] : 0));
        }
    }
}

void PixelConvert_ToDisplay(const Image &img, const PixelFormat &pf,
                            uint8_t *dst, long pitch)
{
    if (pf.bits != 16 && pf.bits != 32)
        return;
    for (int y = 0; y < img.height; ++y, dst += pitch) {
        uint8_t *p = dst;
        for (int x = 0; x < img.width; ++x, p += pf.bits / 8) {
            const uint8_t *px = img.pixel(x, y);
            store_pixel(p, pf.bits, pack_pixel(pf, px[0], px[1], px[2], 0));
        }
    }
}
