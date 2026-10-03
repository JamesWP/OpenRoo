#include <fstream>
#include <iterator>
#include "image.h"
#include <string.h>
#include "gamestr.h"

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
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}
