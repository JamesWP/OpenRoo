/* The file reads for game data (assetio.cpp).  Our code that reads a game file
 * opens and reads it through these. */

#pragma once

extern "C" unsigned __cdecl hooks_fread(void *buf, unsigned size, unsigned count, void *fp);
extern "C" void *__cdecl hooks_fopen(const char *path, const char *mode);
extern "C" int __cdecl hooks_fclose(void *fp);
