/* scenetexture.h -- the owner header for scenetexture.cpp's exports.
 *
 * It exists for CLAUDE.md's "call our own reimplementations through the
 * owning header, never by redeclaring the export".  Before this, nothing
 * outside scenetexture.cpp called into it, so the file had no header at all;
 * textrenderer.cpp's font loader is the first outside caller.
 *
 * Only what an outside caller needs is declared here.  The rest of
 * scenetexture.cpp's exports are reached by patch.py by name and are declared
 * by no header, which is the arrangement tools/check_homes.py expects.
 *
 * The SceneTexture type itself lives in texture.h, with LoadedImage.
 */
#pragma once

#include "texture.h"

/* 0x0043f770 SceneTexture::ImportSceneTextures -- the TGA path: build the
 * surface from `name`, decode into it, and leave the IDirect3DTexture2 in
 * `self->pTexture2`.  __thiscall, `RET 0x18` (six stack arguments).  The low
 * byte of the result is the success flag; the upper three carry whatever the
 * original left there, which callers that only test AL never look at. */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Texture_ImportSceneTextures(SceneTexture *self, IDirectDraw4 *dd,
                            IDirect3DDevice3 *dev, LPCSTR name,
                            DWORD alphaFlag, UINT bpp, DWORD textureStage);
