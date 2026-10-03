/* The TGA decoder: true-colour images, uncompressed (type 2) or run-length
 * encoded (type 10), 16, 24 or 32 bits per pixel, as RGBA8.
 *
 * Every TGA the game ships is square, bottom-left origin, 24 or 32 bits and
 * has no colour map or image ID; the decoder reads the header properly rather
 * than assuming that, and rejects (returns false for) what it cannot decode.
 *
 * Alpha: a 32-bit file's alpha byte is kept and hasAlpha is set; a 24-bit
 * file's pixels come out opaque with hasAlpha clear.  A 16-bit file has one
 * alpha bit, expanded to 0 or 255, and its 5-bit channels are expanded by bit
 * replication. */

#include "image.h"
#include <string.h>
#include <algorithm>

namespace {

struct TgaHeader {
    uint8_t  idLength, colourMapType, imageType;
    uint16_t colourMapLength;
    uint8_t  colourMapDepth;
    uint16_t width, height;
    uint8_t  bpp, descriptor;
};

uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

void put_pixel(uint8_t *dst, const uint8_t *src, unsigned bytes)
{
    if (bytes == 4) {
        dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = src[3];
    } else if (bytes == 3) {
        dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = 255;
    } else {
        unsigned px = src[0] | (src[1] << 8);
        unsigned r = (px >> 10) & 0x1f, g = (px >> 5) & 0x1f, b = px & 0x1f;
        dst[0] = (uint8_t)((r << 3) | (r >> 2));
        dst[1] = (uint8_t)((g << 3) | (g >> 2));
        dst[2] = (uint8_t)((b << 3) | (b >> 2));
        dst[3] = (px & 0x8000) ? 255 : 0;
    }
}

}  // namespace

bool Image_LoadTGA(const char *path, Image &out)
{
    std::vector<uint8_t> file;
    if (!Image_ReadFile(path, file) || file.size() < 18)
        return false;

    const uint8_t *f = file.data();
    TgaHeader h;
    h.idLength        = f[0];
    h.colourMapType   = f[1];
    h.imageType       = f[2];
    h.colourMapLength = le16(f + 5);
    h.colourMapDepth  = f[7];
    h.width           = le16(f + 12);
    h.height          = le16(f + 14);
    h.bpp             = f[16];
    h.descriptor      = f[17];

    if (h.imageType != 2 && h.imageType != 10)
        return false;
    if (h.bpp != 16 && h.bpp != 24 && h.bpp != 32)
        return false;
    if (h.width == 0 || h.height == 0)
        return false;

    const unsigned bytes = h.bpp / 8;
    size_t pos = 18 + h.idLength;
    if (h.colourMapType != 0)
        pos += (size_t)h.colourMapLength * ((h.colourMapDepth + 7u) / 8u);

    const size_t npix = (size_t)h.width * h.height;
    std::vector<uint8_t> pix(npix * 4);

    if (h.imageType == 2) {
        if (pos + npix * bytes > file.size())
            return false;
        for (size_t i = 0; i < npix; ++i)
            put_pixel(&pix[i * 4], f + pos + i * bytes, bytes);
    } else {
        size_t i = 0;
        while (i < npix) {
            if (pos >= file.size())
                return false;
            const uint8_t pkt = f[pos++];
            const size_t count = (size_t)(pkt & 0x7f) + 1;
            if (i + count > npix)
                return false;
            if (pkt & 0x80) {
                if (pos + bytes > file.size())
                    return false;
                for (size_t k = 0; k < count; ++k)
                    put_pixel(&pix[(i + k) * 4], f + pos, bytes);
                pos += bytes;
            } else {
                if (pos + count * bytes > file.size())
                    return false;
                for (size_t k = 0; k < count; ++k)
                    put_pixel(&pix[(i + k) * 4], f + pos + k * bytes, bytes);
                pos += count * bytes;
            }
            i += count;
        }
    }

    // The file is bottom-up unless the descriptor's origin bit says otherwise;
    // Image is top-down.
    const bool topDown = (h.descriptor & 0x20) != 0;
    out.rgba.assign(npix * 4, 0);
    const size_t stride = (size_t)h.width * 4;
    for (unsigned y = 0; y < h.height; ++y) {
        const unsigned srcRow = topDown ? y : (unsigned)(h.height - 1 - y);
        std::copy_n(&pix[srcRow * stride], stride, &out.rgba[y * stride]);
    }

    out.width      = h.width;
    out.height     = h.height;
    out.sourceBits = h.bpp;
    out.hasAlpha   = h.bpp != 24;
    strncpy(out.name, path, sizeof(out.name) - 1);
    out.name[sizeof(out.name) - 1] = '\0';
    return true;
}
