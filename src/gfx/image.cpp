#include <fstream>
#include <iterator>
#include "image.h"
#include <string.h>
#include "sysdev.h"

bool Image_Load(const char *path, Image &out)
{
    const char *ext = strrchr(path, '.');
    if (ext == NULL)
        return false;
    if (strcmp(ext, ".bmp") == 0 || strcmp(ext, ".BMP") == 0)
        return Image_LoadBMP(path, out);
    if (strcmp(ext, ".tga") == 0 || strcmp(ext, ".TGA") == 0)
        return Image_LoadTGA(path, out);
    return false;
}

bool Image_ReadFile(const char *path, std::vector<uint8_t> &out)
{
    std::ifstream in(sysdev::nativePath(path), std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size < 0)
        return false;
    out.resize((size_t)size);
    in.read(reinterpret_cast<char *>(out.data()), size);
    out.resize((size_t)in.gcount());
    return true;
}
