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

/* The SceneTexture ctor/dtor family (0x43f540 / 0x43f580 / 0x43f560).
 * Declared here so patch.py's names have one home and check_homes.py has one
 * answer; nothing outside scenetexture.cpp calls them today — the game
 * reaches them through the call sites patch.py rewrites and through the
 * vtable slot this file installs. */
extern "C" __declspec(dllexport) SceneTexture *__attribute__((thiscall))
Texture_SceneCtor(SceneTexture *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Texture_SceneDtorBody(SceneTexture *self);
extern "C" __declspec(dllexport) SceneTexture *__attribute__((thiscall))
Texture_SceneScalarDtor(SceneTexture *self, unsigned int flags);

/* 0x0043feb0 -- by extension (mode 0), DIB (1) or TGA (2).  The sky builder
 * (sky.cpp) is its one outside caller. */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Texture_SelectTextureLoader(SceneTexture *self, IDirectDraw4 *dd,
                            IDirect3DDevice3 *dev, LPCSTR name, UINT bpp,
                            int mode);

/* ─── TextureManager -- the name-keyed SceneTexture cache ──────────────────
 *
 * One instance, g_TextureManagerGlobal at 0x004dc628, game-constructed.  The
 * theme loader and BuildSceneObjectList (0x00420c50) fetch through it; the
 * theme release and FreeSceneObjects (0x00420ee0) empty it.  Same shape as
 * ModelManager (model.h). */
#include "linkedlist.h"
struct GameLogger;

class __attribute__((packed)) TextureManager {
public:
    static const int ORIGIN = 0;

    void        *vtable;    // +0x00
    LinkedList   cache;     // +0x04  SceneTexture *, game-heap nodes
    GameLogger  *pLogger;   // +0x14  NULL = silent
private:
    KAROO_LAYOUT_REGISTER(TextureManager);
};

KAROO_LAYOUT_CHECKS(TextureManager)
{
    KAROO_LAYOUT_AT(cache,   0x04);
    KAROO_LAYOUT_AT(pLogger, 0x14);
    KAROO_LAYOUT_SIZE(0x18);
}

static TextureManager *const GG_TEXTURE_MANAGER = (TextureManager *)0x004dc628;
/* The second instance (Ghidra g_TextureManagerGlobal2); the WndProc reloads
 * both on WM_ACTIVATE. */
static TextureManager *const GG_TEXTURE_MANAGER2 = (TextureManager *)0x0046c480;

/* 0x004400d0 / 0x00440220. */
extern "C" __declspec(dllexport) SceneTexture *__attribute__((thiscall))
TextureManager_GetOrLoad(TextureManager *self, IDirectDraw4 *dd,
                         IDirect3DDevice3 *dev, char *filename,
                         DWORD alphaFlag, UINT bpp, DWORD textureStage);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
TextureManager_ReleaseAll(TextureManager *self);

/* The lifecycle three (0x440070 ctor, 0x4400b0 dtor body, 0x440090 scalar
 * deleting dtor = vtable slot 0).  The vtable is OURS, one slot; the game's
 * 0x0045d720 is left holding the UD2 at 0x440090 as a tripwire.  Instances:
 * g_TextureManagerGlobal (static init thunks 0x425e90 / 0x425eb0) and the
 * Scene's (scene.h). */
extern "C" __declspec(dllexport) TextureManager *__attribute__((thiscall))
TextureManager_Construct(TextureManager *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
TextureManager_Destruct(TextureManager *self);
extern "C" __declspec(dllexport) TextureManager *__attribute__((thiscall))
TextureManager_ScalarDestructor(TextureManager *self, unsigned char flags);
/* 0x004400c0 / 0x00440260. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
TextureManager_SetLogger(TextureManager *self, GameLogger *logger);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
TextureManager_LoadAll(TextureManager *self);
