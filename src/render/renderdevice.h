/* RenderDevice: the game's one rendering device, and the only way game code
 * talks to the rendering backend.  One instance exists, at g_renderDevice.
 *
 * The backend -- Direct3D 6 and DirectDraw -- lives in src/d3d/, and nothing
 * outside it includes their headers (the Makefile's check-backend enforces
 * it).  RenderDevice owns the backend's objects (RenderDevice::Native, in
 * src/d3d/d3dnative.h): the display mode, the primary/back/z-buffer
 * surfaces, the device, the viewport, the material and the light.  Game code
 * speaks rendertypes.h's vocabulary to it, and hands it textures as
 * SceneTextures (texture.h), whose insides only src/d3d/ looks at. */

#pragma once
#include <windows.h>
#include <vector>
#include "rendertypes.h"

class LoadedImage;
struct SceneTexture;

/* One enumerated display mode. */
struct DisplayMode {
    DWORD dwWidth, dwHeight, dwBitDepth;
};

/* One display adapter, as EnumerateAdapters lists them. */
struct Adapter {
    char name[128];
    bool hasGuid;  // false for the primary (default) adapter
    GUID guid;
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
     * (createdevice.cpp).  hWnd is the window to go full-screen on; the
     * driver GUID may be NULL for the default.  On failure lastError() says
     * why. */
    bool Create(HWND hWnd, GUID *pDriverGuid, int nModeIndex, bool bHardware);

    /* Releases everything and forgets the display modes. */
    void Release();

    const char *lastError() const { return lastError_; }

    /* The adapters Create can take a GUID for. */
    static bool EnumerateAdapters(std::vector<Adapter> &out);

    /* The modes Create's nModeIndex indexes, on the given adapter (NULL for
     * the default): the 4:3 modes of 16 bits or more that the hardware
     * device can render to.  anyAspect lifts the 4:3 restriction, which
     * makes the indices disagree with Create's. */
    static bool EnumerateDisplayModes(const GUID *adapter,
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

    /* The DirectDraw (version 1) interface and the primary surface's
     * version-1 interface, AddRef'd, as the movie player takes them. */
    void GetMovieTarget(void **directDraw, void **primarySurface);

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

    // ── State ──

    void     SetRenderState(RS state, uint32_t value);
    uint32_t GetRenderState(RS state);

    void SetTransform(Transform which, const Mat4 *m);
    void GetTransform(Transform which, Mat4 *m);

    /* NULL unbinds the stage. */
    void SetTexture(int stage, const SceneTexture *tex);
    void SetTexture(int stage, decltype(nullptr)) { SetTexture(stage, (const SceneTexture *)nullptr); }

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

    Native                  *native_;
    std::vector<DisplayMode> modes_;
    DisplayMode             *mode_;
    DWORD                    modeFilterFlags_;
    char                     lastError_[100];
};

extern RenderDevice *g_renderDevice;
