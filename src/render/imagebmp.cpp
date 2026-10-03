/* The BMP decoder: uncompressed Windows bitmaps at 1, 4, 8, 16 (5-5-5), 24 and
 * 32 bits per pixel, as RGBA8.  Palettised images are expanded through their
 * colour table.  The game ships 24-bit loading screens (and a 1-bit mask that
 * nothing loads); the other depths are here so a bitmap of any ordinary kind
 * decodes. */

#include "image.h"
#include <string.h>
#include <algorithm>

namespace {

uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

}  // namespace

bool Image_LoadBMP(const char *path, Image &out)
{
    std::vector<uint8_t> file;
    if (!Image_ReadFile(path, file) || file.size() < 54)
        return false;
    const uint8_t *f = file.data();
    if (f[0] != 'B' || f[1] != 'M')
        return false;

    const uint32_t dataOffset = le32(f + 10);
    const uint32_t headerSize = le32(f + 14);
    if (headerSize < 40)  // the 12-byte OS/2 header is not a game format
        return false;
    const int32_t  w    = (int32_t)le32(f + 18);
    const int32_t  hRaw = (int32_t)le32(f + 22);
    const unsigned bpp  = le16(f + 28);
    const uint32_t compression = le32(f + 30);
    uint32_t colours = le32(f + 46);

    const bool topDown = hRaw < 0;
    const int32_t h = topDown ? -hRaw : hRaw;
    if (w <= 0 || h <= 0 || compression != 0)
        return false;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)
        return false;

    // The colour table, as RGBA.
    uint8_t palette[256][4];
    if (bpp <= 8) {
        if (colours == 0 || colours > (1u << bpp))
            colours = 1u << bpp;
        const size_t tableAt = 14 + (size_t)headerSize;
        if (tableAt + (size_t)colours * 4 > file.size())
            return false;
        for (uint32_t i = 0; i < colours; ++i) {
            const uint8_t *e = f + tableAt + i * 4;  // B, G, R, reserved
            palette[i][0] = e[2]; palette[i][1] = e[1]; palette[i][2] = e[0];
            palette[i][3] = 255;
        }
        for (uint32_t i = colours; i < 256; ++i)
            palette[i][0] = palette[i][1] = palette[i][2] = 0, palette[i][3] = 255;
    }

    const size_t rowBytes = (((size_t)w * bpp + 31) / 32) * 4;
    if ((size_t)dataOffset + rowBytes * (size_t)h > file.size())
        return false;

    std::vector<uint8_t> rgba((size_t)w * h * 4);
    for (int32_t y = 0; y < h; ++y) {
        const int32_t srcRow = topDown ? y : h - 1 - y;
        const uint8_t *row = f + dataOffset + rowBytes * (size_t)srcRow;
        uint8_t *dst = &rgba[(size_t)y * w * 4];
        for (int32_t x = 0; x < w; ++x, dst += 4) {
            switch (bpp) {
            case 1: std::copy_n(palette[(row[x >> 3] >> (7 - (x & 7))) & 1], 4, dst); break;
            case 4: std::copy_n(palette[(x & 1) ? (row[x >> 1] & 15) : (row[x >> 1] >> 4)], 4, dst); break;
            case 8: std::copy_n(palette[row[x]], 4, dst); break;
            case 16: {
                unsigned px = le16(row + x * 2);
                unsigned r = (px >> 10) & 0x1f, g = (px >> 5) & 0x1f, b = px & 0x1f;
                dst[0] = (uint8_t)((r << 3) | (r >> 2));
                dst[1] = (uint8_t)((g << 3) | (g >> 2));
                dst[2] = (uint8_t)((b << 3) | (b >> 2));
                dst[3] = 255;
                break;
            }
            case 24:
                dst[0] = row[x * 3 + 2]; dst[1] = row[x * 3 + 1]; dst[2] = row[x * 3]; dst[3] = 255;
                break;
            default:  // 32: the fourth byte is padding in a BI_RGB bitmap
                dst[0] = row[x * 4 + 2]; dst[1] = row[x * 4 + 1]; dst[2] = row[x * 4]; dst[3] = 255;
                break;
            }
        }
    }

    out.rgba.swap(rgba);
    out.width      = w;
    out.height     = h;
    out.sourceBits = bpp;
    out.hasAlpha   = false;
    strncpy(out.name, path, sizeof(out.name) - 1);
    out.name[sizeof(out.name) - 1] = '\0';
    return true;
}
