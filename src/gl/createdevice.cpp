/* RenderDevice::Create and the enumeration the launcher uses: puts the
 * game's window full-screen in the display mode asked for, makes an OpenGL
 * 3.3 core context on it, and builds the program and the buffers every draw
 * shares.
 *
 * The mode list is the 4:3 modes of 32 bits and then of 16, each group by
 * ascending size.  openroo.ini stores an index into it, so the order is part
 * of the config format.  A display's modes are all 32-bit; the 16-bit group
 * repeats their sizes, and means the game's textures and images are
 * quantised to 16 bits as they were on such a display, while the window
 * itself stays 32.
 *
 * Headless (KAROO_HEADLESS=1) creates nothing: no context, no display mode.
 * Create just builds a fixed mode list, so the config's index means the same
 * thing on every machine, and every other RenderDevice method sees an
 * inactive device and does nothing. */

#include "glnative.h"
#include "sysdev.h"
#include "logger.h"
#include <algorithm>
#include <string.h>
#include <strings.h>

/* The 4:3 sizes a display usually offers, for the headless mode list. */
static const struct { unsigned w, h; } kHeadlessSizes[] = {
    { 320, 240 }, { 640, 480 }, { 800, 600 }, { 1024, 768 },
    { 1152, 864 }, { 1280, 960 }, { 1400, 1050 }, { 1440, 1080 },
};

bool RenderDevice::headless()
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = sysdev::getEnv("KAROO_HEADLESS", buf, sizeof(buf))
                 && buf[0] != '\0' && buf[0] != '0';
    }
    return cached != 0;
}

/* The display's modes: 4:3 only unless anyAspect, 32-bit ones and then
 * 16-bit ones, each group sorted by size. */
static void list_modes(unsigned display, bool anyAspect, std::vector<DisplayMode> &out)
{
    std::vector<windev::DisplaySize> sizes;
    windev::listDisplayModes(display, sizes);
    std::sort(sizes.begin(), sizes.end(),
              [](const windev::DisplaySize &a, const windev::DisplaySize &b) {
                  return a.width != b.width ? a.width < b.width : a.height < b.height;
              });
    for (unsigned depth : { 32u, 16u })
        for (const windev::DisplaySize &s : sizes) {
            const float aspect = (float)((double)s.width / (double)s.height);
            if (anyAspect || (aspect < 1.4f && aspect > 1.3f))
                out.push_back({ s.width, s.height, depth });
        }
}

/* The display an AdapterId names; the primary one for NULL or an all-zero id
 * or one that is no longer attached. */
static bool find_display(const AdapterId *id, windev::DisplayInfo *out)
{
    std::vector<windev::DisplayInfo> displays;
    if (!windev::listDisplays(displays))
        return false;
    static const AdapterId zero = {};
    *out = displays[0];
    if (id != NULL && memcmp(id, &zero, sizeof(zero)) != 0)
        for (const windev::DisplayInfo &d : displays)
            if (memcmp(d.stableId, id, sizeof(AdapterId)) == 0)
                *out = d;
    return true;
}

/* KAROO_DEVICE_FX: controls that change geometry, which only Create decides.
 *   halfvp -- halve the viewport: the scene renders into the top-left
 *             quarter of the screen.
 *   mode0  -- force nModeIndex to 0: the game comes up in a different
 *             resolution from openroo.ini's. */
static bool devfx_is(const char *name)
{
    char buf[16];
    return sysdev::getEnv("KAROO_DEVICE_FX", buf, sizeof(buf))
           && strcasecmp(buf, name) == 0;
}

/* The objects every draw shares: the program, a vertex array, the uniform
 * blocks, the sampler and the buffer Draw streams through. */
static bool create_objects(RenderDevice::Native *n, char *error, size_t errorSize)
{
    n->program = gl_build_program(error, errorSize);
    if (!n->program)
        return false;

    gl.GenVertexArrays(1, &n->vao);
    gl.BindVertexArray(n->vao);

    GLuint ubos[2];
    gl.GenBuffers(2, ubos);
    n->sceneUbo = ubos[0];
    n->drawUbo  = ubos[1];
    gl.BindBuffer(GL_UNIFORM_BUFFER, n->sceneUbo);
    gl.BufferData(GL_UNIFORM_BUFFER, sizeof(SceneBlock), NULL, GL_DYNAMIC_DRAW);
    gl.BindBufferBase(GL_UNIFORM_BUFFER, 0, n->sceneUbo);
    gl.BindBuffer(GL_UNIFORM_BUFFER, n->drawUbo);
    gl.BufferData(GL_UNIFORM_BUFFER, sizeof(DrawBlock), NULL, GL_DYNAMIC_DRAW);
    gl.BindBufferBase(GL_UNIFORM_BUFFER, 1, n->drawUbo);

    gl.GenSamplers(1, &n->sampler);
    gl.BindSampler(0, n->sampler);

    // Draw's vertices: enough for a frame of text and quads; orphaned and
    // started over when it fills.
    n->streamSize = 4u << 20;
    gl.GenBuffers(1, &n->stream);
    gl.BindBuffer(GL_ARRAY_BUFFER, n->stream);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)n->streamSize, NULL, GL_STREAM_DRAW);
    n->streamUsed = 0;

    // The indices go through a buffer of their own, which the VAO keeps bound.
    n->indexStreamSize = 1u << 20;
    gl.GenBuffers(1, &n->indexStream);
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, n->indexStream);
    gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)n->indexStreamSize, NULL, GL_STREAM_DRAW);
    n->indexStreamUsed = 0;

    // State no setter changes.
    gl.ActiveTexture(GL_TEXTURE0);
    gl.Disable(GL_DITHER);
    gl.Disable(GL_MULTISAMPLE);
    gl.FrontFace(GL_CCW);
    gl.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl.ClearDepth(1.0);
    gl.ClearStencil(0);
    gl.StencilMask(0xFF);
    gl.DepthFunc(GL_LEQUAL);
    return true;
}

bool RenderDevice::Create(windev::Window *window, const AdapterId *adapter, int nModeIndex)
{
    Release();
    Native *n = native_;

    windev::DisplayInfo display = {};
    if (headless()) {
        for (unsigned depth : { 32u, 16u })
            for (const auto &s : kHeadlessSizes)
                modes_.push_back({ s.w, s.h, depth });
    } else {
        if (window == NULL || !find_display(adapter, &display)) {
            strcpy(lastError_, "No display was found.");
            return false;
        }
        n->window  = window;
        n->display = display.id;
        list_modes(display.id, false, modes_);
    }
    if (modes_.empty()) {
        strcpy(lastError_, "No usable display mode.");
        return false;
    }

    if (devfx_is("mode0"))
        nModeIndex = 0;
    // An index that is out of range gets the first mode.
    if (nModeIndex < 0 || (size_t)nModeIndex >= modes_.size())
        nModeIndex = 0;

    if (headless()) {
        mode_ = &modes_[nModeIndex];
        n->stencil = true;
        g_logger.write("renderdevice: Create headless %lux%lux%lu\n",
                       mode_->dwWidth, mode_->dwHeight, mode_->dwBitDepth);
        return true;
    }

    // Try the chosen mode, then the first one.
    for (int attempt = 0; attempt < 2 && mode_ == NULL; attempt++) {
        const DisplayMode &m = modes_[attempt == 0 ? nModeIndex : 0];
        if (window->enterFullscreen(display.id, m.dwWidth, m.dwHeight))
            mode_ = &modes_[attempt == 0 ? nModeIndex : 0];
        else
            strcpy(lastError_, "Could not set the display mode.");
        if (nModeIndex == 0)
            break;
    }
    if (mode_ == NULL)
        return false;

    const windev::GLContextConfig want = { 3, 3, 24, 8 };
    if (!window->createGLContext(want)) {
        strcpy(lastError_, "Could not create an OpenGL 3.3 context.");
        mode_ = NULL;
        return false;
    }
    gl_set_window(window);
    const char *missing = NULL;
    if (!gl_load(*window, &missing)) {
        snprintf(lastError_, sizeof(lastError_), "OpenGL lacks %s.", missing);
        window->destroyGLContext();
        mode_ = NULL;
        return false;
    }
    char error[900];
    if (!create_objects(n, error, sizeof(error))) {
        g_logger.write("renderdevice: %s\n", error);
        snprintf(lastError_, sizeof(lastError_), "%s", error);
        window->destroyGLContext();
        n->program = 0;
        mode_ = NULL;
        return false;
    }
    n->active  = true;
    n->stencil = true;

    // Vsync, unless KAROO_NOVSYNC asks the replay harness's way of running
    // unthrottled.
    char novsync[4];
    window->setSwapInterval(sysdev::getEnv("KAROO_NOVSYNC", novsync, sizeof(novsync))
                            && novsync[0] == '1' ? 0 : 1);

    // The viewport is the whole window, or its top-left quarter (OpenGL
    // counts rows from the bottom).
    unsigned w = 0, h = 0;
    window->drawableSize(&w, &h);
    if (w == 0 || h == 0) {
        w = mode_->dwWidth;
        h = mode_->dwHeight;
    }
    n->modeW = mode_->dwWidth;
    n->modeH = mode_->dwHeight;
    // Letterbox to the mode's aspect, centred; the bars keep the black clear.
    n->vpW = w;
    n->vpH = h;
    if ((uint64_t)w * n->modeH > (uint64_t)h * n->modeW)
        n->vpW = (unsigned)((uint64_t)h * n->modeW / n->modeH);
    else
        n->vpH = (unsigned)((uint64_t)w * n->modeH / n->modeW);
    n->vpX = (w - n->vpW) / 2;
    n->vpY = (h - n->vpH) / 2;
    if (devfx_is("halfvp")) {
        n->vpW = w / 2;
        n->vpH = h / 2;
        n->vpX = 0;
        n->vpY = h - n->vpH;
    }
    n->dirty = GLDirty::All;

    g_logger.write("renderdevice: Create display=%u (%s) mode=%d -> %lux%lux%lu, "
                   "window %ux%u\n",
                   display.id, display.name, nModeIndex, mode_->dwWidth,
                   mode_->dwHeight, mode_->dwBitDepth, w, h);
    g_logger.write("renderdevice: OpenGL %s on %s (%s)\n",
                   (const char *)gl.GetString(GL_VERSION),
                   (const char *)gl.GetString(GL_RENDERER),
                   (const char *)gl.GetString(GL_VENDOR));
    return true;
}

// ── Enumeration for the launcher ──

bool RenderDevice::EnumerateAdapters(std::vector<Adapter> &out)
{
    std::vector<windev::DisplayInfo> displays;
    if (!windev::listDisplays(displays))
        return false;
    for (const windev::DisplayInfo &d : displays) {
        Adapter a = {};
        snprintf(a.name, sizeof(a.name), "%s", d.name);
        a.hasGuid = !d.primary;  // the primary one is stored as no id
        memcpy(&a.id, d.stableId, sizeof(a.id));
        out.push_back(a);
    }
    return true;
}

bool RenderDevice::EnumerateDisplayModes(const AdapterId *adapter,
                                         std::vector<DisplayMode> &out,
                                         bool anyAspect)
{
    windev::DisplayInfo display;
    if (!find_display(adapter, &display))
        return false;
    list_modes(display.id, anyAspect, out);
    return !out.empty();
}
