/* RenderDevice: the game's one rendering device, and the only way game code
 * talks to the rendering backend.  One instance exists, at g_renderDevice.
 *
 * The backend -- Direct3D 6 and DirectDraw -- lives in src/d3d/, and nothing
 * outside it includes their headers (cmake/CheckNativeD3D.cmake enforces
 * it).  RenderDevice owns the backend's objects (RenderDevice::Native, in
 * src/d3d/d3dnative.h): the display mode, the primary/back/z-buffer
 * surfaces, the device, the viewport, the material and the light.  Game code
 * speaks rendertypes.h's vocabulary to it, and hands it textures as
 * DeviceTextures, made from Images (image.h) and opaque outside src/d3d/. */

#pragma once
#include <stdint.h>
#include <vector>
#include "rendertypes.h"

class LoadedImage;
class Texture;
struct Image;

/* A texture on the device: opaque to everything outside src/d3d/.  Made by
 * CreateTexture from an Image, freed by DestroyTexture. */
struct DeviceTexture;

/* Bits for CreateTexture's flags. */
namespace TextureFlag {
enum : uint32_t {
    Alpha = 1,  // prefer a pixel format with an alpha channel
};
}

/* One enumerated display mode. */
struct DisplayMode {
    uint32_t dwWidth, dwHeight, dwBitDepth;
};

/* An adapter's identity, opaque to game code: 16 bytes the backend converts
 * to and from its driver GUID.  The launcher stores it in the config file as
 * it is. */
struct AdapterId {
    uint8_t bytes[16];
};

/* One display adapter, as EnumerateAdapters lists them. */
struct Adapter {
    char name[128];
    bool hasGuid;  // false for the primary (default) adapter
    AdapterId id;
};

/* Bits for Draw's flags. */
namespace DrawFlag {
enum : uint32_t {
    NoLight         = 1,  // the vertices are lit already
    NoUpdateExtents = 2,  // leave the device's dirty-rectangle extents alone
};
}

class RenderDevice {
public:
    RenderDevice();
    ~RenderDevice();

    // ── Lifetime ──

    /* Brings up the display mode, the surfaces, the device and the viewport
     * (createdevice.cpp).  hWnd is the native window handle to go full-screen on; the
     * adapter may be NULL for the default.  On failure lastError() says
     * why. */
    bool Create(void *hWnd, const AdapterId *adapter, int nModeIndex, bool bHardware);

    /* Releases everything and forgets the display modes. */
    void Release();

    const char *lastError() const { return lastError_; }

    /* The adapters Create can take an id for. */
    static bool EnumerateAdapters(std::vector<Adapter> &out);

    /* The modes Create's nModeIndex indexes, on the given adapter (NULL for
     * the default): the 4:3 modes of 16 bits or more that the hardware
     * device can render to.  anyAspect lifts the 4:3 restriction, which
     * makes the indices disagree with Create's. */
    static bool EnumerateDisplayModes(const AdapterId *adapter,
                                      std::vector<DisplayMode> &out,
                                      bool anyAspect = false);

    // ── Display ──

    unsigned width() const    { return mode_->dwWidth; }
    unsigned height() const   { return mode_->dwHeight; }
    unsigned bitDepth() const { return mode_->dwBitDepth; }

    /* The z-buffer has stencil bits. */
    bool hasStencil() const;

    /* Puts the desktop's display mode back. */
    void RestoreDisplayMode();

    // ── Frames ──

    /* Clears depth, and stencil if there is one, over the whole target. */
    void ClearDepth();
    bool BeginScene();
    void EndScene();

    /* Shows the back buffer. */
    void Flip();

    /* Fills the back buffer with black. */
    void ClearBackBuffer();

    /* Copies img over the back buffer and flips it to the screen. */
    void PresentImage(LoadedImage *img);

    /* The same for a CPU-side image, scaled to the back buffer.  An empty
     * image just flips. */
    void PresentImage(const Image &img);

    // ── Textures ──

    /* Uploads img in the pixel format the device prefers and returns the
     * texture, or NULL if the device could not make it.  `depth` asks for 16
     * or 32 bits per pixel; anything else means the image's own depth.  The
     * choice of format, and the conversion into it, stay behind this call. */
    DeviceTexture *CreateTexture(const Image &img, uint32_t flags, unsigned depth);

    /* Writes img into t again, restoring the texture first if the device lost
     * it.  img must be the size t was made at. */
    bool UpdateTexture(DeviceTexture *t, const Image &img);

    /* NULL is ignored.  Needs no device: the texture knows its owner. */
    static void DestroyTexture(DeviceTexture *t);

    // ── State ──

    void     SetRenderState(RS state, uint32_t value);
    uint32_t GetRenderState(RS state);

    void SetTransform(Transform which, const Mat4 *m);
    void GetTransform(Transform which, Mat4 *m);

    /* NULL unbinds the stage. */
    void SetTexture(int stage, const DeviceTexture *tex);
    void SetTexture(int stage, const Texture *tex);
    void SetTexture(int stage, decltype(nullptr)) { SetTexture(stage, (const DeviceTexture *)nullptr); }

    /* The ambient light colour, 0x00RRGGBB. */
    void SetAmbientLight(uint32_t rgb);

    /* The one material and the one directional light the scene is lit by;
     * each call replaces the last. */
    void SetMaterial(const Material &m);
    void SetDirectionalLight(const DirectionalLight &l);

    // ── Drawing ──

    /* count vertices of `format` from verts; flags are DrawFlag bits.
     * Returns false if the backend refused the draw. */
    bool Draw(Prim prim, VertexFormat format, const void *verts,
              uint32_t count, uint32_t flags = 0);
    bool DrawStrided(Prim prim, VertexFormat format, StridedVertices *verts,
                     uint32_t count, uint32_t flags = 0);

    /* Logs the device's current render, texture-stage and light state
     * (diagnostics). */
    void LogState(const char *tag);

    /* The backend's own objects; defined in d3dnative.h, for backend files
     * only. */
    struct Native;
    Native *native() { return native_; }

private:
    friend struct DeviceCreation;

    bool BltImageToBackBuffer(const Image &img);

    Native                  *native_;
    std::vector<DisplayMode> modes_;
    DisplayMode             *mode_;
    uint32_t                 modeFilterFlags_;
    char                     lastError_[100];
};

extern RenderDevice *g_renderDevice;
