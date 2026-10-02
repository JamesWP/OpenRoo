#include "image.h"
#include <string.h>
#include "gamestr.h"
#include "assetio.h"

bool Image_Load(const char *path, Image &out)
{
    const char *ext = strrchr(path, '.');
    if (ext == NULL)
        return false;
    if (strcmp(ext, GS_TEX_DOT_BMP_LOWER) == 0 || strcmp(ext, GS_TEX_DOT_BMP_UPPER) == 0)
        return Image_LoadBMP(path, out);
    if (strcmp(ext, GS_TEX_DOT_TGA_LOWER) == 0 || strcmp(ext, GS_TEX_DOT_TGA_UPPER) == 0)
        return Image_LoadTGA(path, out);
    return false;
}

bool Image_ReadFile(const char *path, std::vector<uint8_t> &out)
{
    void *fp = hooks_fopen(path, "rb");
    if (fp == NULL)
        return false;
    out.clear();
    uint8_t chunk[65536];
    for (;;) {
        unsigned got = hooks_fread(chunk, 1, sizeof(chunk), fp);
        if (got == 0)
            break;
        out.insert(out.end(), chunk, chunk + got);
    }
    hooks_fclose(fp);
    return true;
}
