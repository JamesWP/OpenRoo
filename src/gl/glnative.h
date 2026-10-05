/* RenderDevice::Native: the OpenGL objects behind the RenderDevice.  For
 * backend files only -- game code goes through renderdevice.h.
 *
 * The backend is OpenGL 3.3 core profile: everything is drawn by one GLSL
 * program (shaders.cpp) from vertex buffers, with the transforms, light,
 * material and fog in two uniform blocks and the texture sampler in a sampler
 * object.  There is no fixed-function state to set.  The game's pipeline
 * state is set on the device lazily, when a draw needs it: a setter only
 * records the value in RenderDevice's PipelineState and marks its group
 * dirty.
 *
 * A headless RenderDevice (renderdevice.h) has no context at all: `active`
 * stays false, and every RenderDevice method that would reach OpenGL does
 * nothing instead.  Everything else -- the mode list, the shadow state, the
 * texture sizes -- works as normal, so the game cannot tell. */

#pragma once
#include <vector>
#include "glapi.h"
#include "renderdevice.h"
#include "pixelconvert.h"
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

/* The groups of PipelineState the device is lazily brought up to date with. */
namespace GLDirty {
enum : uint32_t {
    Blend = 1, Depth = 2, Stencil = 4, Raster = 8, Sampler = 16,
    Texture = 32, Viewport = 64, Scene = 128, Scissor = 256,
    All = 0x1FF,
};
}

struct DeviceTexture {
    GLuint   texture;     // 0 when headless
    bool     alpha;       // its format has an alpha channel
    int      width, height;
    PixelFormat format;   // what the game's pixels were converted to first
};

struct VertexBuffer {
    VertexFormat format;
    BufferUsage  usage;
    uint32_t     count;
    uint32_t     stride;
    GLuint       buffer;  // 0 when headless
};

struct RenderDevice::Native {
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

    unsigned  presented = 0;           // frames shown so far
    uint32_t  dirty = GLDirty::All;
    Mat4      normalMatrix = PipelineState::identity();
    float     eye[3] = { 0, 0, 0 };
    DrawBlock lastDraw;
    bool      lastDrawValid = false;

    DeviceTexture *image = NULL;       // PresentImage's texture
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
bool gl_upload_image(DeviceTexture *t, const Image &img, bool display, bool create);

/* KAROO_DRAW_TRACE (src/gl/drawtrace.cpp): called by each draw, just before
 * it is issued. */
bool gl_trace_enabled();
void gl_trace_draw(const PipelineState &st, Prim prim, VertexFormat format,
                   const void *verts, uint32_t count, uint32_t flags);
