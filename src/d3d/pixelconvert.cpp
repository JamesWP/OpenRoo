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

static unsigned int mask_max(unsigned int bits)
{
    return (1u << (unsigned char)bits) - 1u;  // the shift count is taken mod 32
}

/* 1/255 as the float32 the original used, by bit pattern. */
static float f32_from_bits(unsigned int bits)
{
    union { unsigned int u; float f; } c;
    c.u = bits;
    return c.f;
}
#define K_INV_255  f32_from_bits(0x3b808180u)

/* __ftol as the original used it: truncate into a 64-bit temporary and keep
 * the low byte. */
static unsigned int ftol8(long double v)
{
    return (unsigned int)(long long)v & 0xffu;
}

void PixelConvert_ToTexture(const Image &img, const PixelFormat &pf,
                            uint8_t *dst, long pitch)
{
    const unsigned int alphaScale = mask_max(PixelMask_Popcount(pf.aMask));
    const unsigned int redScale   = mask_max(PixelMask_Popcount(pf.rMask));
    const unsigned int greenScale = mask_max(PixelMask_Popcount(pf.gMask));
    const unsigned int blueScale  = mask_max(PixelMask_Popcount(pf.bMask));

    const unsigned int alphaShift = PixelMask_Shift(pf.aMask);
    const unsigned int redShift   = PixelMask_Shift(pf.rMask);
    const unsigned int greenShift = PixelMask_Shift(pf.gMask);
    const unsigned int blueShift  = PixelMask_Shift(pf.bMask);

    if (pf.bits != 16 && pf.bits != 32)
        return;

    for (int y = 0; y < img.height; ++y, dst += pitch) {
        uint8_t *p = dst;
        for (int x = 0; x < img.width; ++x, p += pf.bits / 8) {
            const uint8_t *px = img.pixel(x, y);
            float       blue  = (float)(int)px[2];
            float       green = (float)(int)px[1];
            long double red   = (long double)(int)px[0];
            float       alpha = img.hasAlpha ? (float)(int)px[3] : 0.0f;

            if (pf.bits == 32) {
                unsigned int v = ftol8((long double)blue)  << blueShift;
                v += ftol8((long double)green) << greenShift;
                v += ftol8((long double)alpha) << alphaShift;
                v += ftol8(red)                << redShift;
                *(unsigned int *)p = v;
            } else {
                long double la = (long double)(int)(alphaScale & 0xffff)
                                 * alpha * K_INV_255;
                float alpha16 = (float)la;

                red = red * (long double)(int)(redScale & 0xffff) * K_INV_255;

                float green16 = (float)((long double)(int)(greenScale & 0xffff)
                                        * green * K_INV_255);

                long double lb = (long double)(int)(blueScale & 0xffff)
                                 * blue * K_INV_255;

                unsigned int v = ftol8(lb) << blueShift;
                v += ftol8((long double)green16) << greenShift;
                v += ftol8((long double)alpha16) << alphaShift;
                v += ftol8(red) << redShift;
                *(unsigned short *)p = (unsigned short)v;
            }
        }
    }
}

void PixelConvert_ToDisplay(const Image &img, const PixelFormat &pf,
                            uint8_t *dst, long pitch)
{
    if (pf.bits != 16 && pf.bits != 32)
        return;
    const unsigned rBits = PixelMask_Popcount(pf.rMask), rSh = PixelMask_Shift(pf.rMask);
    const unsigned gBits = PixelMask_Popcount(pf.gMask), gSh = PixelMask_Shift(pf.gMask);
    const unsigned bBits = PixelMask_Popcount(pf.bMask), bSh = PixelMask_Shift(pf.bMask);
    for (int y = 0; y < img.height; ++y, dst += pitch) {
        uint8_t *p = dst;
        for (int x = 0; x < img.width; ++x) {
            const uint8_t *px = img.pixel(x, y);
            uint32_t v = ((uint32_t)(px[0] >> (8 - rBits)) << rSh)
                       | ((uint32_t)(px[1] >> (8 - gBits)) << gSh)
                       | ((uint32_t)(px[2] >> (8 - bBits)) << bSh);
            if (pf.bits == 32) {
                *(uint32_t *)p = v;
                p += 4;
            } else {
                *(uint16_t *)p = (uint16_t)v;
                p += 2;
            }
        }
    }
}
