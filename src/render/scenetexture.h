/* scenetexture.h -- the owner header for scenetexture.cpp's exports: the
 * texture loaders, the SceneTexture lifecycle, and TextureManager, the
 * name-keyed texture cache.  SceneTexture extends LoadedImage (texture.h).
 */
#pragma once

#include "texture.h"

class RenderDevice;
struct IDirect3DTexture2;

/* SceneTexture: a LoadedImage that is also a Direct3D texture.  The two
 * really are distinct objects: CreateSurfaceDIB and friends operate on plain
 * LoadedImages, so anything touching pTexture2 must be handed the extended
 * type. */
class __attribute__((packed)) SceneTexture : public LoadedImage {
public:
    IDirect3DTexture2 *texture2() const { return pTexture2_; }

    /* Release the IDirect3DTexture2 and both surfaces. */
    void releaseD3DTexture();

    /* The TGA path: build the surface from `name`, decode into it, and leave
     * the IDirect3DTexture2 in `self->pTexture2`.  Only the low byte of the
     * result is the success flag. */
    unsigned int importSceneTextures(RenderDevice *dev, LPCSTR name,
                                     DWORD alphaFlag, UINT bpp,
                                     DWORD textureStage);

    /* The SceneTexture constructor, destructor body and scalar deleting
     * destructor (the one vtable slot). */
    SceneTexture *construct();

    void dtorBody();

    static SceneTexture * 
    scalarDtor(SceneTexture *self, unsigned int flags);

    /* Load by extension (mode 0), DIB (1) or TGA (2).  The sky builder
     * (sky.cpp) is its one outside caller. */
    unsigned int selectTextureLoader(RenderDevice *dev, LPCSTR name, UINT bpp,
                                     int mode);

    unsigned int bindTextureResource(RenderDevice *dev, LPCSTR name, UINT bpp,
                                     DWORD textureStage);

private:
    /* Replace the image name: free the old, allocate strlen+1, sprintf("%s").
     */
    void setImageName(LPCSTR name);

    IDirect3DTexture2 *pTexture2_;
};

/* textures\shadow.tga and textures\karoo128.tga, loaded once at startup
 * (renderstate.cpp). */
extern SceneTexture g_texShadow;
extern SceneTexture g_texKaroo128;

/* ─── TextureManager -- the name-keyed SceneTexture cache ──────────────────
 *
 * The theme's instance is g_textureManager; the Scene has its own (scene.h).
 * Same shape as ModelManager (model.h).  The window procedure reloads both
 * on WM_ACTIVATE. */
#include "linkedlist.h"
class GameLogger;

class TextureManager {
public:
     

    SceneTexture *getOrLoad(RenderDevice *dev, char *filename, DWORD alphaFlag,
                            UINT bpp, DWORD textureStage);

    void releaseAll();

    /* Constructor, destructor body and scalar deleting destructor (the one
     * vtable slot). */
    TextureManager *construct();

    void destruct();
    static TextureManager * 
    scalarDestructor(TextureManager *self, unsigned char flags);

    void setLogger(GameLogger *logger);
    void loadAll();

    void        *vtable() const { return vtable_; }
    GameLogger  *logger() const { return pLogger_; }

private:
    void        *vtable_;    // 
    LinkedList   cache_;     // SceneTexture *, game-heap nodes
    GameLogger  *pLogger_;   // NULL = silent
     
};

 
extern TextureManager g_textureManager;

