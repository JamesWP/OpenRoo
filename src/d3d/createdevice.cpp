/* RenderDevice::Create and the enumeration the launcher uses: brings up
 * Direct3D 9, picks the display mode and the depth format, and creates the
 * device on the game's window, full-screen.
 *
 * The mode list is the 4:3 modes of 16 and 32 bits, 32-bit ones first, each
 * group by ascending size.  Karoo.cfg stores an index into it, so the order
 * is part of the config format.
 *
 * Headless (KAROO_HEADLESS=1) creates nothing: no Direct3D, no display mode.
 * Create just builds a fixed mode list, so the config's index means the same
 * thing on every machine, and every other RenderDevice method sees a NULL
 * device and does nothing. */

#include "d3dnative.h"
#include "sysdev.h"
#include "logger.h"
#include <algorithm>
#include <string.h>

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

D3DFORMAT d3d_display_format(unsigned bitDepth)
{
    return bitDepth == 16 ? D3DFMT_R5G6B5 : D3DFMT_X8R8G8B8;
}

/* Direct3DCreate9 by LoadLibrary, so a headless run never loads d3d9.dll. */
static IDirect3D9 *create_d3d9()
{
    typedef IDirect3D9 *(WINAPI *create_fn)(UINT);
    HMODULE dll = LoadLibraryA("d3d9.dll");
    create_fn create = dll
        ? (create_fn)(void (*)(void))GetProcAddress(dll, "Direct3DCreate9")
        : NULL;
    return create ? create(D3D_SDK_VERSION) : NULL;
}

/* The adapter's modes: 4:3 only unless anyAspect, 32-bit ones and then
 * 16-bit ones, each group sorted by size with one entry per size. */
static void list_modes(IDirect3D9 *d3d, UINT adapter, bool anyAspect,
                       std::vector<DisplayMode> &out)
{
    const unsigned depths[] = { 32, 16 };
    for (unsigned depth : depths) {
        const D3DFORMAT fmt = d3d_display_format(depth);
        std::vector<DisplayMode> group;
        const UINT count = d3d->GetAdapterModeCount(adapter, fmt);
        for (UINT i = 0; i < count; i++) {
            D3DDISPLAYMODE m;
            if (FAILED(d3d->EnumAdapterModes(adapter, fmt, i, &m)))
                continue;
            const float aspect = (float)((double)m.Width / (double)m.Height);
            if (!anyAspect && !(aspect < 1.4f && aspect > 1.3f))
                continue;
            group.push_back({ m.Width, m.Height, depth });
        }
        std::sort(group.begin(), group.end(),
                  [](const DisplayMode &a, const DisplayMode &b) {
                      return a.dwWidth != b.dwWidth ? a.dwWidth < b.dwWidth
                                                    : a.dwHeight < b.dwHeight;
                  });
        for (const DisplayMode &m : group) {
            if (!out.empty() && out.back().dwBitDepth == depth
                && out.back().dwWidth == m.dwWidth
                && out.back().dwHeight == m.dwHeight)
                continue;  // the same size at another refresh rate
            out.push_back(m);
        }
    }
}

/* The adapter an AdapterId names, by its driver GUID; the default for NULL
 * or an all-zero id. */
static bool find_adapter(IDirect3D9 *d3d, const AdapterId *id, UINT *out)
{
    static const AdapterId zero = {};
    *out = D3DADAPTER_DEFAULT;
    if (id == NULL || memcmp(id, &zero, sizeof(zero)) == 0)
        return true;
    for (UINT i = 0; i < d3d->GetAdapterCount(); i++) {
        D3DADAPTER_IDENTIFIER9 ident;
        static_assert(sizeof(GUID) == sizeof(AdapterId), "AdapterId holds a GUID");
        if (SUCCEEDED(d3d->GetAdapterIdentifier(i, 0, &ident))
            && memcmp(&ident.DeviceIdentifier, id, sizeof(AdapterId)) == 0) {
            *out = i;
            return true;
        }
    }
    return false;
}

/* The deepest depth format the device can use with the display format,
 * preferring one with stencil. */
static bool pick_depth_format(IDirect3D9 *d3d, UINT adapter, D3DFORMAT display,
                              D3DFORMAT *out, bool *stencil)
{
    static const struct { D3DFORMAT fmt; bool stencil; } candidates[] = {
        { D3DFMT_D24S8, true }, { D3DFMT_D24X4S4, true }, { D3DFMT_D15S1, true },
        { D3DFMT_D32, false }, { D3DFMT_D24X8, false }, { D3DFMT_D16, false },
    };
    for (const auto &c : candidates) {
        if (SUCCEEDED(d3d->CheckDeviceFormat(adapter, D3DDEVTYPE_HAL, display,
                                             D3DUSAGE_DEPTHSTENCIL,
                                             D3DRTYPE_SURFACE, c.fmt))
            && SUCCEEDED(d3d->CheckDepthStencilMatch(adapter, D3DDEVTYPE_HAL,
                                                     display, display, c.fmt))) {
            *out = c.fmt;
            *stencil = c.stencil;
            return true;
        }
    }
    return false;
}

/* KAROO_D3DDEV_FX: controls that change geometry, which only Create decides.
 *   halfvp -- halve the viewport: the scene renders into the top-left
 *             quarter of the screen.
 *   mode0  -- force nModeIndex to 0: the game comes up in a different
 *             resolution from Karoo.cfg's. */
static bool devfx_is(const char *name)
{
    char buf[16];
    return sysdev::getEnv("KAROO_D3DDEV_FX", buf, sizeof(buf))
           && lstrcmpiA(buf, name) == 0;
}

/* The viewport: the whole back buffer, or its top-left quarter. */
void d3d_apply_viewport(RenderDevice::Native *n)
{
    D3DVIEWPORT9 vp = {};
    vp.Width  = n->pp.BackBufferWidth;
    vp.Height = n->pp.BackBufferHeight;
    vp.MaxZ   = 1.0f;
    if (devfx_is("halfvp")) {
        vp.Width  /= 2;
        vp.Height /= 2;
    }
    n->device->SetViewport(&vp);
}

bool RenderDevice::Create(void *hWnd, const AdapterId *adapter, int nModeIndex)
{
    Release();
    Native *n = native_;

    if (headless()) {
        for (const auto &s : kHeadlessSizes)
            modes_.push_back({ s.w, s.h, 32 });
        for (const auto &s : kHeadlessSizes)
            modes_.push_back({ s.w, s.h, 16 });
    } else {
        n->d3d = create_d3d9();
        if (n->d3d == NULL) {
            strcpy(lastError_, "Could not create the Direct3D 9 object.");
            return false;
        }
        if (!find_adapter(n->d3d, adapter, &n->adapter)) {
            strcpy(lastError_, "The display adapter was not found.");
            return false;
        }
        list_modes(n->d3d, n->adapter, false, modes_);
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
    for (int attempt = 0; attempt < 2; attempt++) {
        const DisplayMode &m = modes_[attempt == 0 ? nModeIndex : 0];
        n->displayFormat = d3d_display_format(m.dwBitDepth);
        if (!pick_depth_format(n->d3d, n->adapter, n->displayFormat,
                               &n->depthFormat, &n->stencil)) {
            strcpy(lastError_, "No depth buffer format for the display mode.");
        } else {
            D3DPRESENT_PARAMETERS &pp = n->pp;
            pp = D3DPRESENT_PARAMETERS();
            pp.BackBufferWidth        = m.dwWidth;
            pp.BackBufferHeight       = m.dwHeight;
            pp.BackBufferFormat       = n->displayFormat;
            pp.BackBufferCount        = 1;
            pp.SwapEffect             = D3DSWAPEFFECT_DISCARD;
            pp.hDeviceWindow          = (HWND)hWnd;
            pp.Windowed               = FALSE;
            pp.EnableAutoDepthStencil = TRUE;
            pp.AutoDepthStencilFormat = n->depthFormat;
            pp.PresentationInterval   = D3DPRESENT_INTERVAL_DEFAULT;

            // FPU_PRESERVE: by default Direct3D drops the x87 to single
            // precision, which the game's double arithmetic does not expect.
            const DWORD flags = D3DCREATE_FPU_PRESERVE;
            HRESULT hr = n->d3d->CreateDevice(
                n->adapter, D3DDEVTYPE_HAL, (HWND)hWnd,
                flags | D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &n->device);
            if (FAILED(hr))
                hr = n->d3d->CreateDevice(
                    n->adapter, D3DDEVTYPE_HAL, (HWND)hWnd,
                    flags | D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &n->device);
            if (SUCCEEDED(hr)) {
                mode_ = &modes_[attempt == 0 ? nModeIndex : 0];
                break;
            }
            strcpy(lastError_, "Could not create the Direct3D device.");
        }
        if (nModeIndex == 0)
            break;
    }
    if (mode_ == NULL)
        return false;

    d3d_restore_state(n);

    g_logger.write("renderdevice: Create hwnd=%p adapter=%u mode=%d -> %lux%lux%lu "
                   "depth=%d stencil=%d\n",
                   hWnd, n->adapter, nModeIndex, mode_->dwWidth, mode_->dwHeight,
                   mode_->dwBitDepth, (int)n->depthFormat, (int)n->stencil);
    return true;
}

// ── Enumeration for the launcher ──

bool RenderDevice::EnumerateAdapters(std::vector<Adapter> &out)
{
    IDirect3D9 *d3d = create_d3d9();
    if (d3d == NULL)
        return false;
    for (UINT i = 0; i < d3d->GetAdapterCount(); i++) {
        D3DADAPTER_IDENTIFIER9 ident;
        if (FAILED(d3d->GetAdapterIdentifier(i, 0, &ident)))
            continue;
        Adapter a = {};
        lstrcpynA(a.name, ident.Description, sizeof(a.name));
        a.hasGuid = i != D3DADAPTER_DEFAULT;  // the default one is stored as no id
        memcpy(&a.id, &ident.DeviceIdentifier, sizeof(a.id));
        out.push_back(a);
    }
    d3d->Release();
    return !out.empty();
}

bool RenderDevice::EnumerateDisplayModes(const AdapterId *adapter,
                                         std::vector<DisplayMode> &out,
                                         bool anyAspect)
{
    IDirect3D9 *d3d = create_d3d9();
    if (d3d == NULL)
        return false;
    UINT ordinal;
    if (!find_adapter(d3d, adapter, &ordinal))
        ordinal = D3DADAPTER_DEFAULT;
    list_modes(d3d, ordinal, anyAspect, out);
    d3d->Release();
    return !out.empty();
}
