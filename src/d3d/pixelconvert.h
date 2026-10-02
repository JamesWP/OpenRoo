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
 * the row stride in bytes.  The arithmetic is the original TGA loader's, kept
 * as it was so a texture's bytes do not change: the 32-bit path adds the four
 * shifted channels; the 16-bit path rescales 0..255 to the channel's maximum
 * and truncates -- green and alpha through float32 first, blue and red at
 * extended precision.  Two consequences are preserved because they change
 * pixels:
 *   - an image with no alpha channel writes alpha 0, not 255;
 *   - a destination with no alpha mask still receives the alpha byte, added
 *     at bit 0 of a 32-bit pixel (the 16-bit path ignores it).
 * Formats other than 16 and 32 bits are left untouched. */
void PixelConvert_ToTexture(const Image &img, const PixelFormat &pf,
                            uint8_t *dst, long pitch);

/* The same for an image drawn to the screen: each channel is truncated to the
 * format's width, alpha is not written, and nothing else is touched.  What a
 * GDI BitBlt of the bitmap did. */
void PixelConvert_ToDisplay(const Image &img, const PixelFormat &pf,
                            uint8_t *dst, long pitch);
