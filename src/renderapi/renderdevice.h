/* RenderDevice: the game's one rendering device, and the only way game code
 * talks to the rendering backend.  One instance exists; app owns it and
 * Renderer::device points at it.
 *
 * It is a pure interface.  The backend -- OpenGL ES 3.2 or 3.3 core, drawing
 * everything with one shader program -- lives in src/gl/, which implements it
 * and exposes one function that makes a device (gl/glrenderdevice.h); nothing
 * outside src/gl/ includes the backend's headers (cmake/CheckNativeGL.cmake
 * enforces it).  Game code speaks rendertypes.h's vocabulary to the device,
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
 * A headless RenderDevice (KAROO_HEADLESS=1) has no OpenGL behind it: it
 * answers every query as a real one would and draws nothing. */

#pragma once
#include <stdint.h>
#include "adapterid.h"
#include <vector>
#include "rendertypes.h"
#include "particletypes.h"

struct Image;
namespace windev { class Window; }

/* A texture on the device: opaque to everything outside the backend, which
 * derives its own from this.  Made by CreateTexture from an Image, freed by
 * DestroyTexture. */
struct DeviceTexture {
    virtual ~DeviceTexture() {}
};

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

/* One display, as EnumerateAdapters lists them. */
struct Adapter {
    char name[128];
    bool hasGuid;  // false for the primary display
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

/* A vertex buffer on the device: opaque to everything outside the backend,
 * which derives its own from this.  Made by CreateVertexBuffer, freed by
 * DestroyVertexBuffer. */
struct VertexBuffer {
    virtual ~VertexBuffer() {}
};

enum class BufferUsage {
    Static,   // filled at creation; never rewritten
    Dynamic,  // rewritten as often as every frame
};

class RenderDevice {
public:
    virtual ~RenderDevice() {}

    // ── Lifetime ──

    /* Whether this run is headless: KAROO_HEADLESS is set.  The window should
     * not be shown, and Create makes no device. */
    virtual bool headless() const = 0;

    /* Brings up the display mode and the device: puts
     * `window` full-screen on the adapter and makes the backend's context on it;
     * the adapter may be NULL for the primary one.  The window outlives the
     * device.  On failure lastError() says why. */
    virtual bool Create(windev::Window *window, const AdapterId *adapter, int nModeIndex) = 0;

    /* Releases everything and forgets the display modes. */
    virtual void Release() = 0;

    virtual const char *lastError() const = 0;

    /* The adapters Create can take an id for. */
    virtual bool EnumerateAdapters(std::vector<Adapter> &out) const = 0;

    /* The modes Create's nModeIndex indexes, on the given adapter (NULL for
     * the default): the 4:3 modes of 32 bits and then of 16. */
    virtual bool EnumerateDisplayModes(const AdapterId *adapter,
                                       std::vector<DisplayMode> &out) const = 0;

    // ── Display ──

    virtual unsigned width() const = 0;
    virtual unsigned height() const = 0;
    /* The size the overlay draws in: the whole window, native pixels, bars
     * included.  Equal to width()/height() where there are no bars. */
    virtual unsigned overlayWidth() const = 0;
    virtual unsigned overlayHeight() const = 0;
    virtual unsigned bitDepth() const = 0;

    /* The z-buffer has stencil bits. */
    virtual bool hasStencil() const = 0;

    // ── Frames ──

    /* Clears what ClearFlag bits ask for, over the whole target. */
    virtual void Clear(uint32_t flags) = 0;

    /* Bracket the frame's drawing.  BeginFrame is false if the device is
     * lost and cannot yet be got back; draw nothing then. */
    virtual bool BeginFrame() = 0;
    virtual void EndFrame() = 0;

    /* Shows the back buffer, after the overlay (if there is one) has drawn
     * over it. */
    virtual void Present() = 0;

    /* A function Present calls to draw over the finished frame: the debug UI.
     * It draws with the ordinary setters and returns true if it drew; the
     * state the game had set is then put back.  A frame it did not draw on
     * is shown exactly as if there were no overlay.  NULL for none. */
    typedef bool (*OverlayFn)(RenderDevice &dev);
    virtual void SetOverlay(OverlayFn fn) = 0;

    /* Copies img over the back buffer, scaled to fit, and shows it.  An empty
     * image just shows the back buffer. */
    virtual void PresentImage(const Image &img) = 0;

    // ── Textures ──

    /* Uploads img in the pixel format the device prefers and returns the
     * texture, or NULL if the device could not make it.  `depth` asks for 16
     * or 32 bits per pixel; anything else means the image's own depth.  The
     * choice of format, and the conversion into it, stay behind this call. */
    virtual DeviceTexture *CreateTexture(const Image &img, uint32_t flags, unsigned depth) = 0;

    /* Writes img into t again, restoring the texture first if the device lost
     * it.  img must be the size t was made at. */
    virtual bool UpdateTexture(DeviceTexture *t, const Image &img) = 0;

    /* NULL is ignored.  Needs no device, and may follow the device's and the
     * window's going. */
    static void DestroyTexture(DeviceTexture *t) { delete t; }

    // ── State ──
    //
    // Each setter replaces the whole of its state, and each state stays as
    // set until set again.  A fresh device is in every state's default (see
    // rendertypes.h) and no texture is bound.

    virtual void SetBlend(const BlendState &s) = 0;
    virtual void SetDepth(const DepthState &s) = 0;
    virtual void SetStencil(const StencilState &s) = 0;
    virtual void SetRaster(const RasterState &s) = 0;
    virtual void SetFog(const FogState &s) = 0;
    virtual void SetScissor(const ScissorState &s) = 0;

    virtual const BlendState &blend() const = 0;
    virtual const DepthState &depth() const = 0;
    virtual const StencilState &stencil() const = 0;
    virtual const RasterState &raster() const = 0;
    virtual const FogState &fog() const = 0;
    virtual const ScissorState &scissor() const = 0;

    /* How `stage` samples its texture.  The game draws with stage 0. */
    virtual void SetSampler(int stage, const SamplerState &s) = 0;
    /* Changes only the address modes of `stage`'s sampler. */
    virtual void SetSamplerAddress(int stage, AddressMode u, AddressMode v) = 0;

    /* Transforms the texture coordinates `stage` samples with.  Applies to
     * vertices the game supplies in world space, not to Screen vertices. */
    virtual void SetUVTransform(int stage, const UVTransform &t) = 0;

    /* NULL unbinds the stage. */
    virtual void SetTexture(int stage, const DeviceTexture *tex) = 0;

    /* Whether lit geometry shows its specular highlight, and vertices carry
     * their specular colour. */
    virtual void SetSpecular(bool on) = 0;

    /* The ambient light colour, 0x00RRGGBB. */
    virtual void SetAmbientLight(uint32_t rgb) = 0;

    /* The one material and the one directional light the scene is lit by;
     * each call replaces the last. */
    virtual void SetMaterial(const Material &m) = 0;
    virtual void SetDirectionalLight(const DirectionalLight &l) = 0;

    /* The transforms.  Projection takes the matrix of the game's original
     * Direct3D 6 viewport, whose clip volume put the y axis at +-aspect, and
     * the backend adapts it; its depth runs 0..w, as Direct3D's did. */
    virtual void SetWorld(const Mat4 &m) = 0;
    virtual void SetView(const Mat4 &m) = 0;
    virtual void SetProjection(const Mat4 &m) = 0;

    virtual const Mat4 &world() const = 0;
    virtual const Mat4 &view() const = 0;

    // ── Vertex buffers ──

    /* A buffer of `count` vertices of `format`, filled from `data` if that is
     * not NULL.  Returns NULL if the device could not make it. */
    virtual VertexBuffer *CreateVertexBuffer(VertexFormat format, uint32_t count,
                                             BufferUsage usage, const void *data = nullptr) = 0;

    /* Overwrites `count` vertices from `first` with `verts`, which must be in
     * the buffer's format.  The buffer was made Dynamic or has never been
     * drawn. */
    virtual bool UpdateVertexBuffer(VertexBuffer *vb, uint32_t first, const void *verts,
                                    uint32_t count) = 0;

    /* NULL is ignored.  Needs no device, and may follow the device's and the
     * window's going. */
    static void DestroyVertexBuffer(VertexBuffer *vb) { delete vb; }

    // ── Drawing ──

    /* count vertices of `format` from verts, which Draw copies before it
     * returns: for geometry made fresh each time, like text and the HUD.
     * flags are DrawFlag bits.  Returns false if the backend refused the
     * draw. */
    virtual bool Draw(Prim prim, VertexFormat format, const void *verts,
                      uint32_t count, uint32_t flags = 0) = 0;

    /* Like Draw, but the primitive is made of the vertices `indices` name:
     * `indexCount` of them, each less than `vertexCount`.  Returns false if
     * one is not, or if the backend refused the draw. */
    virtual bool DrawIndexed(Prim prim, VertexFormat format, const void *verts,
                             uint32_t vertexCount, const uint16_t *indices,
                             uint32_t indexCount, uint32_t flags = 0) = 0;

    /* `count` vertices of vb from `first`. */
    virtual bool DrawBuffer(Prim prim, const VertexBuffer *vb, uint32_t first,
                            uint32_t count, uint32_t flags = 0) = 0;

    // ── Particles (particletypes.h) ──

    /* A buffer of `count` particle slots, all dead.  shapes[i] is the
     * ParticleShape slot i tumbles with, or NULL for none.  NULL if the device
     * could not make it; a headless device makes one that holds nothing. */
    virtual ParticleBuffer *CreateParticleBuffer(uint32_t count, const uint32_t *shapes) = 0;

    /* The shapes pb's slots tumble with; replaces any earlier ones. */
    virtual void SetParticleShapes(ParticleBuffer *pb, const ParticleShape *shapes,
                                   uint32_t count) = 0;

    /* Advances every slot of pb by one step, on the device. */
    virtual void StepParticles(ParticleBuffer *pb, const ParticleStep &step) = 0;

    /* Draws pb's live particles at each of `count` instances, in one draw,
     * with the current state; the device's world transform is not used. */
    virtual bool DrawParticles(const ParticleBuffer *pb, const ParticleLook &look,
                               const ParticleInstance *instances, uint32_t count) = 0;

    /* NULL is ignored.  Needs no device, like DestroyVertexBuffer. */
    static void DestroyParticleBuffer(ParticleBuffer *pb) { delete pb; }

    /* Logs the device's current render, texture-stage and light state
     * (diagnostics). */
    virtual void LogState(const char *tag) = 0;
};
