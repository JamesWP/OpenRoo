/* Image: a decoded picture held in plain CPU memory -- no platform types, no
 * device.  The loaders (imagetga.cpp, imagebmp.cpp) produce one from a game
 * file; RenderDevice::CreateTexture and PresentImage take one and own
 * everything that is specific to the platform: pixel format, memory pool,
 * upload. */

#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>

struct Image {
    /* The file it came from, for logs and diagnostics. */
    char name[260];

    int width, height;

    /* The file's bits per pixel (1, 4, 8, 16, 24 or 32).  A texture asked for
     * "its own depth" is made at this depth. */
    unsigned sourceBits;

    /* The file carried an alpha channel (32-bit TGAs).  When false every
     * pixel's alpha byte is 255. */
    bool hasAlpha;

    /* width * height pixels, top row first, four bytes each: R, G, B, A. */
    std::vector<uint8_t> rgba;

    Image() : width(0), height(0), sourceBits(0), hasAlpha(false) { name[0] = '\0'; }

    bool empty() const { return rgba.empty(); }
    size_t bytes() const { return rgba.size(); }
    const uint8_t *pixel(int x, int y) const { return &rgba[((size_t)y * width + x) * 4]; }
};

/* Decoders.  Each returns false, leaving `out` untouched, when the file is
 * missing or is not a format the game ships.  The game's file reads go through
 * assetio.h. */
bool Image_LoadTGA(const char *path, Image &out);
bool Image_LoadBMP(const char *path, Image &out);

/* By extension: ".bmp" or ".BMP" is a BMP, ".tga" or ".TGA" a TGA; anything
 * else, including mixed case, fails. */
bool Image_Load(const char *path, Image &out);

/* The whole of a game file, read through assetio.h.  The decoders' only
 * file access. */
bool Image_ReadFile(const char *path, std::vector<uint8_t> &out);
