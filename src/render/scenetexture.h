/* scenetexture.h -- the owner header for scenetexture.cpp's exports: the
 * texture loaders, the SceneTexture lifecycle, and TextureManager, the
 * name-keyed texture cache.  The SceneTexture type itself lives in texture.h,
 * with LoadedImage.
 */
#pragma once

#include "texture.h"

class RenderDevice;

/* The TGA path: build the surface from `name`, decode into it, and leave the
 * IDirect3DTexture2 in `self->pTexture2`.  Only the low byte of the result is
 * the success flag. */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Texture_ImportSceneTextures(SceneTexture *self, RenderDevice *dev, LPCSTR name,
                            DWORD alphaFlag, UINT bpp, DWORD textureStage);

/* The SceneTexture constructor, destructor body and scalar deleting
 * destructor (the one vtable slot). */
extern "C" __declspec(dllexport) SceneTexture *__attribute__((thiscall))
Texture_SceneCtor(SceneTexture *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Texture_SceneDtorBody(SceneTexture *self);
extern "C" __declspec(dllexport) SceneTexture *__attribute__((thiscall))
Texture_SceneScalarDtor(SceneTexture *self, unsigned int flags);

/* Load by extension (mode 0), DIB (1) or TGA (2).  The sky builder
 * (sky.cpp) is its one outside caller. */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Texture_SelectTextureLoader(SceneTexture *self, RenderDevice *dev, LPCSTR name, UINT bpp,
                            int mode);

/* ─── TextureManager -- the name-keyed SceneTexture cache ──────────────────
 *
 * The theme's instance is g_textureManager; the Scene has its own (scene.h).
 * Same shape as ModelManager (model.h).  The window procedure reloads both
 * on WM_ACTIVATE. */
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

extern TextureManager g_textureManager;

extern "C" __declspec(dllexport) SceneTexture *__attribute__((thiscall))
TextureManager_GetOrLoad(TextureManager *self, RenderDevice *dev, char *filename,
                         DWORD alphaFlag, UINT bpp, DWORD textureStage);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
TextureManager_ReleaseAll(TextureManager *self);

/* Constructor, destructor body and scalar deleting destructor (the one
 * vtable slot). */
extern "C" __declspec(dllexport) TextureManager *__attribute__((thiscall))
TextureManager_Construct(TextureManager *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
TextureManager_Destruct(TextureManager *self);
extern "C" __declspec(dllexport) TextureManager *__attribute__((thiscall))
TextureManager_ScalarDestructor(TextureManager *self, unsigned char flags);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
TextureManager_SetLogger(TextureManager *self, GameLogger *logger);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
TextureManager_LoadAll(TextureManager *self);
