/* Image -> device pixel formats, with no driver types: a format is described
 * by its bit count and channel masks, so this can be tested on its own. */

#pragma once
#include <stdint.h>
#include "image.h"

struct PixelFormat {
    uint32_t bits;
    uint32_t rMask, gMask, bMask, aMask;
};

unsigned int PixelMask_Popcount(uint32_t mask);
unsigned int PixelMask_Shift(uint32_t mask);

/* Write img into a locked surface of format pf: dst is its first row, pitch
 * the row stride in bytes.  Each channel is scaled from 0..255 to the width of
 * its mask, truncating.  An image with no alpha channel writes alpha 0, and a
 * format with no alpha mask drops the alpha byte.  Formats other than 16 and
 * 32 bits are left untouched. */
void PixelConvert_ToTexture(const Image &img, const PixelFormat &pf,
                            uint8_t *dst, long pitch);

/* The same for an image drawn to the screen, which has no alpha: alpha is not
 * written. */
void PixelConvert_ToDisplay(const Image &img, const PixelFormat &pf,
                            uint8_t *dst, long pitch);

/* The reverse: a converted surface of format pf (dst of the functions above)
 * widened to RGBA8, four bytes a pixel in memory order R, G, B, A, for a
 * graphics API that has no such format.  Each channel is widened by
 * replicating its bits, so a full channel is 255; a format without an alpha
 * channel is opaque. */
void PixelConvert_ToRGBA8(const PixelFormat &pf, const uint8_t *src, long pitch,
                          int width, int height, uint8_t *dst);
