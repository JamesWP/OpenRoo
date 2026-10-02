/* texture.h -- Texture, a game-side texture, and TextureManager, the
 * name-keyed texture cache.
 *
 * A Texture is a name, the DeviceTexture the render device made from the
 * file's pixels, and the decoded Image itself.  The Image stays resident so
 * that reupload() can refill the device texture -- after the device lost it, or
 * on a window activation -- without reading the file again.  It costs
 * width * height * 4 bytes a texture (see TextureManager).
 *
 * Nothing here knows what the device's texture is; that is renderdevice.h's
 * business, and the backend's. */

#pragma once

#include <stdint.h>
#include "image.h"
#include "linkedlist.h"

class RenderDevice;
struct DeviceTexture;

/* Every member is a plain pointer, so a zeroed Texture is an empty one (the
 * theme block is memset to zero once its textures are released). */
class Texture {
public:
    Texture();
    /* Frees the name and the image, not the device texture: release() does
     * that, and the statics outlive the device. */
    ~Texture();
    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;

    /* Decode the TGA at `path` and upload it.  alphaFlag's low byte asks for a
     * pixel format with alpha; `depth` is RenderDevice::CreateTexture's.  On
     * failure the texture is as it was. */
    bool load(RenderDevice *dev, const char *path, uint32_t alphaFlag, unsigned depth);

    /* The same, choosing the decoder by extension (.bmp or .tga); no alpha.
     * The sky builder (sky.cpp) is its one caller. */
    bool loadByExtension(RenderDevice *dev, const char *path, unsigned depth);

    /* Write the retained image into the device texture again.  False if there
     * is nothing loaded.  Called on window activation. */
    bool reupload(RenderDevice *dev);

    /* Free the device texture, the image and the name. */
    void release();

    DeviceTexture *handle() const { return handle_; }
    char          *name() const { return name_; }
    const Image   *image() const { return image_; }

private:
    bool adopt(RenderDevice *dev, Image *img, uint32_t flags, unsigned depth);

    DeviceTexture *handle_;
    char          *name_;
    Image         *image_;
};

/* textures\shadow.tga and textures\karoo128.tga, loaded once at startup
 * (renderstate.cpp). */
extern Texture g_texShadow;
extern Texture g_texKaroo128;

/* ─── TextureManager -- the name-keyed Texture cache ───────────────────────
 *
 * The theme's instance is g_textureManager; the Scene has its own (scene.h).
 * Same shape as ModelManager (model.h).  The window procedure re-uploads both
 * on WM_ACTIVATE.
 *
 * Memory: the cache keeps every texture's image, 4 bytes a pixel whatever the
 * file's depth.  A level's worth is a few tens of megabytes at most. */
class TextureManager {
public:
    Texture *getOrLoad(RenderDevice *dev, char *filename, uint32_t alphaFlag,
                       unsigned depth);

    void releaseAll();

    TextureManager();
    /* Empties the cache's nodes, not the textures. */
    virtual ~TextureManager();
    TextureManager(const TextureManager &) = delete;
    TextureManager &operator=(const TextureManager &) = delete;

    void reuploadAll(RenderDevice *dev);

private:
    LinkedList   cache_;     // Texture *, game-heap nodes
};

extern TextureManager g_textureManager;
