/* RenderDevice's textures and images (renderdevice.h): the OpenGL texture a
 * DeviceTexture holds, the choice of its pixel format, and the conversion of
 * an Image's RGBA8 pixels into that format (pixelconvert.cpp).
 *
 * Format choice.  The depth asked for is `depth` if that is exactly 16 or 32,
 * else the image's own depth, so an 8-bit image gets a 16-bit format and a
 * 24-bit image a 32-bit one.  Each depth has a format with alpha and one
 * without.  The game's pixels are quantised to that format -- 4 bits a
 * channel, or 5-5-5 -- and then widened back to RGBA8 for the texture, which
 * is what keeps the game looking as it did on hardware that stored them
 * narrow, and what a 32-bit format passes through unchanged.
 *
 * A headless device makes no texture at all; it only converts, when
 * KAROO_TEXTURE_DUMP wants to see the pixels. */

#include <fstream>
#include <stdio.h>
#include <string.h>
#include "glnative.h"
#include "image.h"
#include "logger.h"
#include "sysdev.h"

PixelFormat gl_texture_format(unsigned depth, bool alpha)
{
    if (depth > 16)
        return alpha ? PixelFormat{ 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000 }  // A8R8G8B8
                     : PixelFormat{ 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0x00000000 }; // X8R8G8B8
    return alpha ? PixelFormat{ 16, 0x00000F00, 0x000000F0, 0x0000000F, 0x0000F000 }      // A4R4G4B4
                 : PixelFormat{ 16, 0x00007C00, 0x000003E0, 0x0000001F, 0x00000000 };     // X1R5G5B5
}

PixelFormat gl_display_format(unsigned bitDepth)
{
    return bitDepth == 16 ? PixelFormat{ 16, 0x0000F800, 0x000007E0, 0x0000001F, 0x00000000 }  // R5G6B5
                          : PixelFormat{ 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0x00000000 }; // X8R8G8B8
}

// ── The texture dump ──

bool gl_dump_enabled()
{
    static int enabled = -1;
    if (enabled < 0) {
        char path[4];
        enabled = sysdev::getEnv("KAROO_TEXTURE_DUMP", path, sizeof(path)) ? 1 : 0;
    }
    return enabled != 0;
}

void gl_dump_pixels(const char *kind, const char *name, int width, int height,
                    const PixelFormat &pf, const uint8_t *bits, long pitch)
{
    static char path[512];
    if (!gl_dump_enabled())
        return;
    if (path[0] == '\0')
        sysdev::getEnv("KAROO_TEXTURE_DUMP", path, sizeof(path));

    unsigned long long h = 1469598103934665603ull;
    const unsigned rowBytes = (unsigned)width * (pf.bits / 8);
    for (int y = 0; y < height; ++y, bits += pitch)
        for (unsigned x = 0; x < rowBytes; ++x)
            h = (h ^ bits[x]) * 1099511628211ull;

    char line[512];
    snprintf(line, sizeof(line),
             "%s|%s|%dx%d|%ubpp r=%08lX g=%08lX b=%08lX a=%08lX|%016llX\n",
             kind, name, width, height, pf.bits,
             (unsigned long)pf.rMask, (unsigned long)pf.gMask,
             (unsigned long)pf.bMask, (unsigned long)pf.aMask, h);
    std::ofstream f(path, std::ios::binary | std::ios::app);
    f << line;
}

// ── Upload ──

bool gl_upload_image(DeviceTexture *t, const Image &img, bool display, bool create)
{
    const PixelFormat &pf = t->format;
    const long pitch = (long)img.width * (pf.bits / 8);
    std::vector<uint8_t> converted((size_t)img.height * pitch);
    if (display)
        PixelConvert_ToDisplay(img, pf, converted.data(), pitch);
    else
        PixelConvert_ToTexture(img, pf, converted.data(), pitch);
    gl_dump_pixels(display ? "img" : "tex", img.name, img.width, img.height, pf,
                   converted.data(), pitch);

    if (t->texture) {
        std::vector<uint8_t> rgba((size_t)img.width * img.height * 4);
        PixelConvert_ToRGBA8(pf, converted.data(), pitch, img.width, img.height, rgba.data());
        gl.BindTexture(GL_TEXTURE_2D, t->texture);
        if (create) {
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0,
                          GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        } else {
            gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, img.width, img.height,
                             GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        }
    }
    return true;
}

// ── RenderDevice ──

DeviceTexture *RenderDevice::CreateTexture(const Image &img, uint32_t flags,
                                           unsigned depth)
{
    if (img.empty())
        return NULL;

    // Only an exact 16 or 32 is honoured.
    if (depth != 16 && depth != 32)
        depth = img.sourceBits;

    DeviceTexture *t = new DeviceTexture();
    t->texture = 0;
    t->format  = gl_texture_format(depth, (flags & TextureFlag::Alpha) != 0);
    t->alpha   = t->format.aMask != 0;
    t->width   = img.width;
    t->height  = img.height;

    static int seen = 0;
    if (++seen <= 8)
        g_logger.write("devicetexture: %s req=%u -> %u bits, alpha %d\n",
                       img.name, depth, (unsigned)t->format.bits, (int)t->alpha);

    if (native_->active) {
        gl.GenTextures(1, &t->texture);
        native_->dirty |= GLDirty::Texture;  // the upload binds it
    }
    if (!gl_upload_image(t, img, false, true)) {
        DestroyTexture(t);
        return NULL;
    }
    return t;
}

bool RenderDevice::UpdateTexture(DeviceTexture *t, const Image &img)
{
    if (t == NULL || img.width != t->width || img.height != t->height)
        return false;
    native_->dirty |= GLDirty::Texture;
    return gl_upload_image(t, img, false, false);
}

void RenderDevice::DestroyTexture(DeviceTexture *t)
{
    if (t == NULL)
        return;
    if (t->texture != 0 && gl_usable())
        gl.DeleteTextures(1, &t->texture);
    delete t;
}

// ── Images ──
//
// PresentImage's source: the image quantised to the display's format, as a
// texture drawn over the whole back buffer, filtered to fit.

void RenderDevice::PresentImage(const Image &img)
{
    Native *n = native_;
    const PixelFormat pf = gl_display_format(mode_->dwBitDepth);

    if (!n->active) {
        if (!img.empty() && gl_dump_enabled()) {
            const long pitch = (long)img.width * (pf.bits / 8);
            std::vector<uint8_t> buf((size_t)img.height * pitch);
            PixelConvert_ToDisplay(img, pf, buf.data(), pitch);
            gl_dump_pixels("img", img.name, img.width, img.height, pf, buf.data(), pitch);
        }
        return;
    }

    bool ok = false;
    if (!img.empty()) {
        if (n->image && (n->image->width != img.width || n->image->height != img.height)) {
            DestroyTexture(n->image);
            n->image = NULL;
        }
        const bool fresh = n->image == NULL;
        if (fresh) {
            n->image = new DeviceTexture();
            n->image->format = pf;
            n->image->alpha  = false;
            n->image->width  = img.width;
            n->image->height = img.height;
            gl.GenTextures(1, &n->image->texture);
        }
        ok = gl_upload_image(n->image, img, true, fresh);
        if (ok) {
            // Drawn with nothing of the game's state: no blend, depth, stencil,
            // culling or fog, and a linear clamped sampler.  The quad is half a
            // pixel up and left, which is where Screen vertices put whole pixels.
            const PipelineState saved = state_;
            PipelineState s;
            s.depth.test = s.depth.write = false;
            s.raster.cull = CullMode::None;
            s.samplers[0] = SamplerState{ Filter::Linear, Filter::Linear,
                                          AddressMode::Clamp, AddressMode::Clamp };
            s.bound[0] = n->image;
            state_ = s;
            n->dirty = GLDirty::All;

            const float x0 = -0.5f, y0 = -0.5f;
            const float x1 = (float)n->vpW - 0.5f, y1 = (float)n->vpH - 0.5f;
            const ScreenVertex q[4] = {
                { x0, y0, 0.0f, 1.0f, 0xFFFFFFFFu, 0, 0.0f, 0.0f },
                { x1, y0, 0.0f, 1.0f, 0xFFFFFFFFu, 0, 1.0f, 0.0f },
                { x0, y1, 0.0f, 1.0f, 0xFFFFFFFFu, 0, 0.0f, 1.0f },
                { x1, y1, 0.0f, 1.0f, 0xFFFFFFFFu, 0, 1.0f, 1.0f },
            };
            Draw(Prim::TriangleStrip, VertexFormat::Screen, q, 4, 0);

            state_ = saved;
            n->dirty = GLDirty::All;
        }
    }

    Present();

    static int logged = 0;
    if (++logged <= 8)
        g_logger.write("renderdevice: PresentImage %s %dx%d %s\n",
                       img.name, img.width, img.height, ok ? "ok" : "not drawn");
}
