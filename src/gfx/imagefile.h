/* The image file decoders: an Image (renderapi/image.h) from one of the
 * game's .tga or .bmp files. */

#pragma once
#include <vector>
#include <stdint.h>
#include "image.h"

/* Decoders.  Each returns false, leaving `out` untouched, when the file is
 * missing or is not a format the game ships. */
bool Image_LoadTGA(const char *path, Image &out);
bool Image_LoadBMP(const char *path, Image &out);

/* By extension: ".bmp" or ".BMP" is a BMP, ".tga" or ".TGA" a TGA; anything
 * else, including mixed case, fails. */
bool Image_Load(const char *path, Image &out);

/* The whole of a file.  The decoders' only
 * file access. */
bool Image_ReadFile(const char *path, std::vector<uint8_t> &out);
