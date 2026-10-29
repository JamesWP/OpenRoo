/* GLRenderDevice, the OpenGL implementation of the RenderDevice interface, and
 * the OpenGL objects behind it (GLRenderDevice::Native).  For backend files
 * only -- game code goes through renderdevice.h, and gets its device from
 * glrenderdevice.h.
 *
 * The backend is OpenGL ES 3.2, or 3.3 core where there is no ES (glapi_internal.h): everything is drawn by one GLSL
 * program (shaders.cpp) from vertex buffers, with the transforms, light,
 * material and fog in two uniform blocks and the texture sampler in a sampler
 * object.  There is no fixed-function state to set.  The game's pipeline
 * state is set on the device lazily, when a draw needs it: a setter only
 * records the value in GLRenderDevice's PipelineState and marks its group
 * dirty.
 *
 * A headless GLRenderDevice has no context at all: `active`
 * stays false, and every RenderDevice method that would reach OpenGL does
 * nothing instead.  Everything else -- the mode list, the shadow state, the
 * texture sizes -- works as normal, so the game cannot tell. */

#pragma once
#ifndef OPENROO_GL_BACKEND
#error "Only the gl group's own sources may include this header; use glrenderdevice.h"
#endif
#include <vector>
#include "glapi_internal.h"
#include "renderdevice.h"
#include "pixelconvert_internal.h"
#include "windev.h"

/* The two uniform blocks of the program (shaders.cpp declares the same, in
 * std140; every member is a vec4 or a mat4, so the C++ layout is the GLSL
 * one).  Matrices are the game's, row-major for row vectors, which OpenGL
 * reads as the transposed matrix for column vectors: the shader multiplies
 * `uProj * uView * uWorld * v`. */
struct SceneBlock {         // binding 0: changes between passes
    float view[16];
    float proj[16];         // the game's, with the y scale of its viewport
    float eye[4];           // the camera in world space
    float lightDir[4];      // the way the light travels, unit length
    float lightDiffuse[4];
    float lightSpecular[4];
    float ambient[4];
    float fogColor[4];
    float fog[4];           // start, end, density, mode (0 off, 1 exp, 2 exp2, 3 linear)
    float target[4];        // 1/width, -1/height (the half pixel), width, height
};

struct DrawBlock {          // binding 1: changes between draws
    float world[16];
    float normal[16];       // the matrix the world's normals are carried by
    float matDiffuse[4];
    float matAmbient[4];
    float matSpecular[4];   // .w is the specular power
    float matEmissive[4];
    float uv[4];            // scaleU, scaleV, offsetU, offsetV
    float flags[4];         // screen vertices, lit, specular on, texture mode
};

/* Everything the game has set on the device: the shadow the backend keeps,
 * so the getters need no device, and a device that has been reset can be put
 * back the way it was. */
struct PipelineState {
    BlendState          blend;
    DepthState          depth;
    StencilState        stencil;
    RasterState         raster;
    ScissorState        scissor;
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

/* The groups of PipelineState the device is lazily brought up to date with. */
namespace GLDirty {
enum : uint32_t {
    Blend = 1, Depth = 2, Stencil = 4, Raster = 8, Sampler = 16,
    Texture = 32, Viewport = 64, Scene = 128, Scissor = 256,
    All = 0x1FF,
};
}

struct GLTexture : DeviceTexture {
    ~GLTexture() override;
    GLuint   texture = 0; // 0 when headless
    bool     alpha = false;  // its format has an alpha channel
    int      width = 0, height = 0;
    PixelFormat format;   // what the game's pixels were converted to first
};

/* How many particles a draw shows at most, and the shapes they tumble with. */
struct GLParticleBuffer : ParticleBuffer {
    ~GLParticleBuffer() override;
    uint32_t count = 0;
    GLuint   shapes = 0;             // RGBA32F, seven texels a ParticleShape; 0 when headless
    uint32_t shapeCount = 0;
};

/* Vertices to a row of a GLShardBuffer's texture. */
static const unsigned SHARD_ROW_VERTICES = 512;

/* A mesh frame's triangles as a texture, two texels a vertex -- position and
 * U, normal and V -- SHARD_ROW_VERTICES vertices to a row, for DrawShards'
 * vertex shader to read its triangle's three corners from. */
struct GLShardBuffer : ShardBuffer {
    ~GLShardBuffer() override;
    uint32_t count = 0;    // vertices
    GLuint   texture = 0;  // RGBA32F; 0 when headless
};

struct GLVertexBuffer : VertexBuffer {
    ~GLVertexBuffer() override;
    VertexFormat format;
    BufferUsage  usage;
    uint32_t     count;
    uint32_t     stride;
    GLuint       buffer = 0;  // 0 when headless
};

/* The OpenGL implementation of RenderDevice. */
class GLRenderDevice final : public RenderDevice {
public:
    GLRenderDevice();
    ~GLRenderDevice() override;

    bool headless() const override;
    bool Create(windev::Window *window, const AdapterId *adapter, int nModeIndex) override;
    void Release() override;
    const char *lastError() const override { return lastError_; }
    bool EnumerateAdapters(std::vector<Adapter> &out) const override;
    bool EnumerateDisplayModes(const AdapterId *adapter,
        std::vector<DisplayMode> &out) const override;
    unsigned width() const override { return mode_->dwWidth; }
    unsigned height() const override { return mode_->dwHeight; }
    unsigned overlayWidth() const override;
    unsigned overlayHeight() const override;
    unsigned bitDepth() const override { return mode_->dwBitDepth; }
    bool hasStencil() const override;
    void Clear(uint32_t flags) override;
    bool BeginFrame() override;
    void EndFrame() override;
    void Present() override;
    void SetOverlay(OverlayFn fn) override;
    void PresentImage(const Image &img) override;
    DeviceTexture *CreateTexture(const Image &img, uint32_t flags, unsigned depth) override;
    bool UpdateTexture(DeviceTexture *t, const Image &img) override;
    void SetBlend(const BlendState &s) override;
    void SetDepth(const DepthState &s) override;
    void SetStencil(const StencilState &s) override;
    void SetRaster(const RasterState &s) override;
    void SetFog(const FogState &s) override;
    void SetScissor(const ScissorState &s) override;
    const BlendState &blend() const override { return state_.blend; }
    const DepthState &depth() const override { return state_.depth; }
    const StencilState &stencil() const override { return state_.stencil; }
    const RasterState &raster() const override { return state_.raster; }
    const FogState &fog() const override { return state_.fog; }
    const ScissorState &scissor() const override { return state_.scissor; }
    void SetSampler(int stage, const SamplerState &s) override;
    void SetSamplerAddress(int stage, AddressMode u, AddressMode v) override;
    void SetUVTransform(int stage, const UVTransform &t) override;
    void SetTexture(int stage, const DeviceTexture *tex) override;
    void SetSpecular(bool on) override;
    void SetAmbientLight(uint32_t rgb) override;
    void SetMaterial(const Material &m) override;
    void SetDirectionalLight(const DirectionalLight &l) override;
    void SetWorld(const Mat4 &m) override;
    void SetView(const Mat4 &m) override;
    void SetProjection(const Mat4 &m) override;
    const Mat4 &world() const override { return state_.world; }
    const Mat4 &view() const override { return state_.view; }
    VertexBuffer *CreateVertexBuffer(VertexFormat format, uint32_t count,
        BufferUsage usage, const void *data) override;
    bool UpdateVertexBuffer(VertexBuffer *vb, uint32_t first, const void *verts,
        uint32_t count) override;
    bool Draw(Prim prim, VertexFormat format, const void *verts,
        uint32_t count, uint32_t flags) override;
    bool DrawIndexed(Prim prim, VertexFormat format, const void *verts,
        uint32_t vertexCount, const uint16_t *indices,
        uint32_t indexCount, uint32_t flags) override;
    bool DrawBuffer(Prim prim, const VertexBuffer *vb, uint32_t first,
        uint32_t count, uint32_t flags) override;
    void LogState(const char *tag) override;
    ParticleBuffer *CreateParticleBuffer(uint32_t count) override;
    void SetParticleShapes(ParticleBuffer *pb, const ParticleShape *shapes,
        uint32_t count) override;
    bool DrawParticles(const ParticleBuffer *pb, const ParticleEmit &emit,
        const ParticleField &field, const ParticleLook &look,
        const ParticleInstance *instances, uint32_t count) override;
    ShardBuffer *CreateShardBuffer(VertexFormat format, const void *verts,
                                   uint32_t count) override;
    bool DrawShards(const ShardBuffer *sb, const ShardBurst &burst) override;

    /* The backend's own objects. */
    struct Native;

private:
    Native                  *native_;
    PipelineState            state_;
    std::vector<DisplayMode> modes_;
    DisplayMode             *mode_;
    char                     lastError_[900];
};

struct GLRenderDevice::Native {
    windev::Window *window  = NULL;
    unsigned        display = 0;       // the windev display it is full-screen on
    bool            active  = false;   // a context and its objects exist
    bool            stencil = false;

    unsigned vpX = 0, vpY = 0, vpW = 0, vpH = 0;  // the viewport, in pixels
    unsigned winW = 1, winH = 1;      // the whole window, which the overlay draws in
    unsigned modeW = 1, modeH = 1;     // the display mode the game draws in

    GLuint program = 0, vao = 0, sceneUbo = 0, drawUbo = 0, sampler = 0;
    GLuint stream  = 0;                // the buffer Draw's vertices go through
    size_t streamSize = 0, streamUsed = 0;
    GLuint indexStream = 0;            // the same for DrawIndexed's indices
    size_t indexStreamSize = 0, indexStreamUsed = 0;

    // The particle programs (particles.cpp); 0 if they did not build.
    GLuint particleDraw = 0;
    GLuint particleDrawVao = 0;
    GLuint particleInstances = 0;       // DrawParticles' ParticleInstances
    // The shattered-mesh program and its vertex array (shards.cpp), which has
    // no attributes: the vertex shader reads the ShardBuffer's texture.
    GLuint shardDraw = 0, shardVao = 0;

    unsigned  presented = 0;           // frames shown so far
    uint32_t  dirty = GLDirty::All;
    Mat4      normalMatrix = PipelineState::identity();
    float     eye[3] = { 0, 0, 0 };
    DrawBlock lastDraw;
    bool      lastDrawValid = false;

    GLTexture     *image = NULL;       // PresentImage's texture
    RenderDevice::OverlayFn overlay = NULL;
};

/* Whether OpenGL can be called: a context exists and has not been closed with
 * its window.  Everything that frees an OpenGL object checks it, because the
 * game frees its textures after the window has gone. */
bool gl_usable();
void gl_set_window(windev::Window *window);

/* The program's source, built into a linked program (0 and a message in
 * `error` if it fails). */
GLuint gl_build_program(char *error, size_t errorSize);

/* The particle programs: the step, by transform feedback, and the draw.
 * False, with a message in `error`, if either did not build. */
bool gl_build_particle_program(GLuint *draw, char *error, size_t errorSize);
void gl_create_particle_objects(GLRenderDevice::Native *n);
void gl_release_particle_objects(GLRenderDevice::Native *n);

/* The shattered-mesh program: 0, with a message in `error`, if it did not
 * build. */
GLuint gl_build_shard_program(char *error, size_t errorSize);
void gl_create_shard_objects(GLRenderDevice::Native *n);
void gl_release_shard_objects(GLRenderDevice::Native *n);

/* renderdevice.cpp's: whether draws are skipped (KAROO_RENDER_FX=nodraw), and
 * the state, uniforms and fixed-function equivalents a draw of `format`
 * needs, brought up to date. */
bool gl_fx_nodraw();
void gl_prepare_draw(GLRenderDevice::Native *n, const PipelineState &st, float aspect,
                     VertexFormat format, uint32_t flags);

/* The pixel layout of a texture format and of the display's. */
PixelFormat gl_texture_format(unsigned depth, bool alpha);
PixelFormat gl_display_format(unsigned bitDepth);

/* Hashes a converted surface into the file named by KAROO_TEXTURE_DUMP, if
 * there is one: one line of kind, name, size, layout and an FNV-1a of the
 * pixel bytes.  Diffing two runs' dumps shows whether a change moved a
 * pixel. */
void gl_dump_pixels(const char *kind, const char *name, int width, int height,
                    const PixelFormat &pf, const uint8_t *bits, long pitch);
bool gl_dump_enabled();

/* Uploads img, converted to pf, into t (which has its storage if `create`). */
bool gl_upload_image(GLTexture *t, const Image &img, bool display, bool create);

/* KAROO_DRAW_TRACE (src/gl/drawtrace.cpp): called by each draw, just before
 * it is issued. */
bool gl_trace_enabled();
void gl_trace_draw(const PipelineState &st, Prim prim, VertexFormat format,
                   const void *verts, uint32_t count, uint32_t flags);
