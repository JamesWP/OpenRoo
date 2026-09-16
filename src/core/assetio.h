#pragma once

/* The CRT file hooks in assetio.cpp.  patch.py routes the game's fread at
 * 0x45158a here, so DLL code that reads game files calls this rather than the
 * original by address. */
extern "C" unsigned __cdecl hooks_fread(void *buf, unsigned size, unsigned count, void *fp);
extern "C" void *__cdecl hooks_fopen(const char *path, const char *mode);
extern "C" int __cdecl hooks_fclose(void *fp);
