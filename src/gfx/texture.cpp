/* Texture and TextureManager (texture.h). */

#include "texture.h"
#include <new>
#include <stdio.h>
#include <string.h>
#include "renderdevice.h"
#include "logger.h"

Texture g_texKaroo128;
Texture g_texShadow;
TextureManager g_textureManager;

Texture::Texture()
    : handle_(NULL), image_(NULL)
{
    name_[0] = '\0';
}

Texture::~Texture()
{
    delete image_;
}

void Texture::release()
{
    RenderDevice::DestroyTexture(handle_);
    handle_ = NULL;
    name_[0] = '\0';
    delete image_;
    image_ = NULL;
}

bool Texture::adopt(RenderDevice *dev, Image *img, uint32_t flags, unsigned depth)
{
    DeviceTexture *t = dev->CreateTexture(*img, flags, depth);
    if (t == NULL) {
        RenderDevice::DestroyTexture(t);
        delete img;
        return false;
    }
    release();
    handle_ = t;
    snprintf(name_, sizeof(name_), "%s", img->name);
    image_  = img;
    return true;
}

bool Texture::load(RenderDevice *dev, const char *path, uint32_t alphaFlag,
                   unsigned depth)
{
    Image *img = new (std::nothrow) Image();
    if (img == NULL)
        return false;
    if (!Image_LoadTGA(path, *img)) {
        delete img;
        return false;
    }
    return adopt(dev, img, (alphaFlag & 0xff) ? (uint32_t)TextureFlag::Alpha : 0u, depth);
}

bool Texture::loadByExtension(RenderDevice *dev, const char *path, unsigned depth)
{
    Image *img = new (std::nothrow) Image();
    if (img == NULL)
        return false;
    if (!Image_Load(path, *img)) {
        delete img;
        return false;
    }
    return adopt(dev, img, 0, depth);
}

bool Texture::reupload(RenderDevice *dev)
{
    if (handle_ == NULL || image_ == NULL)
        return false;
    return dev->UpdateTexture(handle_, *image_);
}

void RenderDevice::SetTexture(int stage, const Texture *tex)
{
    SetTexture(stage, tex ? tex->handle() : (const DeviceTexture *)NULL);
}

/* ─── TextureManager ───────────────────────────────────────────────────────
 *
 * GetOrLoad compares names by lowercasing both strings in place and then
 * strcmp'ing, so the caller's buffer and every cached name stay lowercased --
 * which is why "TM: %s loaded" logs the lowercased name.
 *
 * Only alphaFlag's low byte is meaningful. */
static void tm_lower_inplace(char *s)
{
    // 'A'..'Z' only.
    for (; *s; s++)
        if (*s > '@' && *s < '[')
            *s += ' ';
}

Texture *TextureManager::getOrLoad(RenderDevice *dev, char *filename,
                                   uint32_t alphaFlag, unsigned depth)
{
    for (size_t i = 0; i < cache_.size(); ++i) {
        Texture *cached = cache_[i];
        tm_lower_inplace(filename);
        tm_lower_inplace(cached->name());
        if (strcmp(cached->name(), filename) == 0) {
            g_logger.logMessage(1, "TM: %s found", filename);
            return cached;
        }
    }

    Texture *tex = new (std::nothrow) Texture();
    if (tex == NULL || !tex->load(dev, filename, alphaFlag, depth)) {
        delete tex;
        g_logger.logMessage(3, "TM: *ERROR* failed loading %s", filename);
        return NULL;
    }
    g_logger.logMessage(1, "TM: %s loaded", filename);
    cache_.push_back(tex);
    return tex;
}

void TextureManager::releaseAll()
{
    std::vector<Texture *> doomed;
    doomed.swap(cache_);
    for (size_t i = 0; i < doomed.size(); ++i) {
        doomed[i]->release();
        delete doomed[i];
    }
}

/* Every instance is static (texture.h), so nothing deletes one and the scalar
 * dtor's free is never reached. */
TextureManager::TextureManager()
{
}

TextureManager::~TextureManager()
{
}

/* Reupload every cached texture, oldest first. */
void TextureManager::reuploadAll(RenderDevice *dev)
{
    for (size_t i = 0; i < cache_.size(); ++i)
        cache_[i]->reupload(dev);
}
