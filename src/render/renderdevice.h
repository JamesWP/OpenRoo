/* RenderDevice: the game's one rendering device, and the only way game code
 * talks to the rendering backend.  One instance exists, at g_renderDevice.
 *
 * The backend -- Direct3D 9 -- lives in src/d3d/, and nothing outside it
 * includes its headers (cmake/CheckNativeD3D.cmake enforces it).
 * RenderDevice owns the backend's objects (RenderDevice::Native, in
 * src/d3d/d3dnative.h): the Direct3D device and a shadow of the state the
 * game has set on it.  Game code speaks rendertypes.h's vocabulary to it,
 * and hands it textures and vertex buffers as opaque handles.
 *
 * The interface is shaped for a backend without a fixed-function pipeline
 * (OpenGL, WebGL 2, Direct3D 11 and later):
 *   - pipeline state is set in whole value types (BlendState, DepthState,
 *     StencilState, ...), which such a backend turns into a pipeline or
 *     state object;
 *   - lighting, fog and the transforms are plain parameters, which such a
 *     backend turns into uniforms;
 *   - vertices live in VertexBuffers, or are handed to Draw for one use;
 *   - nothing reads state back from the device.
 *
 * A headless RenderDevice (KAROO_HEADLESS=1) has no Direct3D behind it: it
 * answers every query as a real one would and draws nothing. */

#pragma once
#include <stdint.h>
#include <vector>
#include "rendertypes.h"

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

/* Bits for Draw's and DrawBuffer's flags. */
namespace DrawFlag {
enum : uint32_t {
    NoLight = 1,  // the vertices are lit already
};
}

/* Bits for Clear. */
namespace ClearFlag {
enum : uint32_t {
    Color = 1,  // the back buffer, to black
    Depth = 2,  // the z-buffer, and the stencil if there is one
};
}

/* A vertex buffer on the device: opaque to everything outside src/d3d/.
 * Made by CreateVertexBuffer, freed by DestroyVertexBuffer. */
struct VertexBuffer;

enum class BufferUsage {
    Static,   // filled at creation; never rewritten
    Dynamic,  // rewritten as often as every frame
};

/* Everything the game has set on the device: the shadow the backend keeps,
 * so the getters need no device, and a device that has been reset can be put
 * back the way it was. */
struct PipelineState {
    BlendState          blend;
    DepthState          depth;
    StencilState        stencil;
    RasterState         raster;
    FogState            fog;
    SamplerState        samplers[2];  // by stage; the game's vertex formats address two
    UVTransform         uvTransform[2];
    bool                specular = false;
    uint32_t            ambient  = 0;
    Material            material = {};
    bool                materialSet = false;
    DirectionalLight    light = {};
    bool                lightSet = false;
    Mat4                world = identity(), view = identity(), projection = identity();
    const DeviceTexture *bound[2] = { nullptr, nullptr };  // by stage

    static Mat4 identity()
    {
        Mat4 m = {};
        m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.0f;
        return m;
    }
};

class RenderDevice {
public:
    RenderDevice();
    ~RenderDevice();

    // ── Lifetime ──

    /* Whether this run is headless: KAROO_HEADLESS is set.  The window should
     * not be shown, and Create makes no device. */
    static bool headless();

    /* Brings up the display mode and the device (createdevice.cpp).  hWnd is
     * the native window handle to go full-screen on; the adapter may be NULL
     * for the default.  On failure lastError() says why. */
    bool Create(void *hWnd, const AdapterId *adapter, int nModeIndex);

    /* Releases everything and forgets the display modes. */
    void Release();

    const char *lastError() const { return lastError_; }

    /* The adapters Create can take an id for. */
    static bool EnumerateAdapters(std::vector<Adapter> &out);

    /* The modes Create's nModeIndex indexes, on the given adapter (NULL for
     * the default): the 4:3 modes of 32 bits and then of 16.  anyAspect lifts
     * the 4:3 restriction, which makes the indices disagree with Create's. */
    static bool EnumerateDisplayModes(const AdapterId *adapter,
                                      std::vector<DisplayMode> &out,
                                      bool anyAspect = false);

    // ── Display ──

    unsigned width() const    { return mode_->dwWidth; }
    unsigned height() const   { return mode_->dwHeight; }
    unsigned bitDepth() const { return mode_->dwBitDepth; }

    /* The z-buffer has stencil bits. */
    bool hasStencil() const;

    // ── Frames ──

    /* Clears what ClearFlag bits ask for, over the whole target. */
    void Clear(uint32_t flags);

    /* Bracket the frame's drawing.  BeginFrame is false if the device is
     * lost and cannot yet be got back; draw nothing then. */
    bool BeginFrame();
    void EndFrame();

    /* Shows the back buffer. */
    void Present();

    /* Copies img over the back buffer, scaled to fit, and shows it.  An empty
     * image just shows the back buffer. */
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
    //
    // Each setter replaces the whole of its state, and each state stays as
    // set until set again.  A fresh device is in every state's default (see
    // rendertypes.h) and no texture is bound.

    void SetBlend(const BlendState &s);
    void SetDepth(const DepthState &s);
    void SetStencil(const StencilState &s);
    void SetRaster(const RasterState &s);
    void SetFog(const FogState &s);

    const BlendState   &blend() const   { return state_.blend; }
    const DepthState   &depth() const   { return state_.depth; }
    const StencilState &stencil() const { return state_.stencil; }
    const RasterState  &raster() const  { return state_.raster; }
    const FogState     &fog() const     { return state_.fog; }

    /* How `stage` samples its texture.  The game draws with stage 0. */
    void SetSampler(int stage, const SamplerState &s);
    /* Changes only the address modes of `stage`'s sampler. */
    void SetSamplerAddress(int stage, AddressMode u, AddressMode v);

    /* Transforms the texture coordinates `stage` samples with.  Applies to
     * vertices the game supplies in world space, not to Screen vertices. */
    void SetUVTransform(int stage, const UVTransform &t);

    /* NULL unbinds the stage. */
    void SetTexture(int stage, const DeviceTexture *tex);
    void SetTexture(int stage, const Texture *tex);
    void SetTexture(int stage, decltype(nullptr)) { SetTexture(stage, (const DeviceTexture *)nullptr); }

    /* Whether lit geometry shows its specular highlight, and vertices carry
     * their specular colour. */
    void SetSpecular(bool on);

    /* The ambient light colour, 0x00RRGGBB. */
    void SetAmbientLight(uint32_t rgb);

    /* The one material and the one directional light the scene is lit by;
     * each call replaces the last. */
    void SetMaterial(const Material &m);
    void SetDirectionalLight(const DirectionalLight &l);

    /* The transforms.  Projection takes the matrix of the game's original
     * Direct3D 6 viewport, whose clip volume put the y axis at +-aspect, and
     * the backend adapts it. */
    void SetWorld(const Mat4 &m);
    void SetView(const Mat4 &m);
    void SetProjection(const Mat4 &m);

    const Mat4 &world() const { return state_.world; }
    const Mat4 &view() const  { return state_.view; }

    // ── Vertex buffers ──

    /* A buffer of `count` vertices of `format`, filled from `data` if that is
     * not NULL.  Returns NULL if the device could not make it. */
    VertexBuffer *CreateVertexBuffer(VertexFormat format, uint32_t count,
                                     BufferUsage usage, const void *data = nullptr);

    /* Overwrites `count` vertices from `first` with `verts`, which must be in
     * the buffer's format.  The buffer was made Dynamic or has never been
     * drawn. */
    bool UpdateVertexBuffer(VertexBuffer *vb, uint32_t first, const void *verts,
                            uint32_t count);

    /* NULL is ignored.  Needs no device: the buffer knows its owner. */
    static void DestroyVertexBuffer(VertexBuffer *vb);

    // ── Drawing ──

    /* count vertices of `format` from verts, which Draw copies before it
     * returns: for geometry made fresh each time, like text and the HUD.
     * flags are DrawFlag bits.  Returns false if the backend refused the
     * draw. */
    bool Draw(Prim prim, VertexFormat format, const void *verts,
              uint32_t count, uint32_t flags = 0);

    /* `count` vertices of vb from `first`. */
    bool DrawBuffer(Prim prim, const VertexBuffer *vb, uint32_t first,
                    uint32_t count, uint32_t flags = 0);

    /* Logs the device's current render, texture-stage and light state
     * (diagnostics). */
    void LogState(const char *tag);

    /* The backend's own objects; defined in d3dnative.h, for backend files
     * only. */
    struct Native;
    Native *native() { return native_; }
    const PipelineState &state() const { return state_; }

private:
    Native                  *native_;
    PipelineState            state_;
    std::vector<DisplayMode> modes_;
    DisplayMode             *mode_;
    char                     lastError_[100];
};

extern RenderDevice *g_renderDevice;
