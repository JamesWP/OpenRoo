/* KAROO_HEADLESS — DirectDraw and Direct3D, entirely inside this DLL.
 *
 * ─── Why ──────────────────────────────────────────────────────────────────
 *
 * The replay suite needs a display.  Not because the simulation needs one —
 * `replaytest.py --fast` already skips every draw call and the flip Blt — but
 * because `Direct3D::CreateD3DDevice` (createdevice.cpp) brings up real
 * DirectDraw: `SetCooperativeLevel(hWnd, 0x811)` is EXCLUSIVE|FULLSCREEN, and
 * `SetDisplayMode` then resizes the desktop.  That takes focus and the screen
 * away from whatever the user was doing, which makes a background test run
 * hostile, and it fails outright with no X server at all.
 *
 * With KAROO_HEADLESS=1 nothing here touches a display.  `DirectDrawCreate`
 * is not forwarded to ddraw.dll; the game gets the objects below instead.
 * There is no window to raise, no mode to switch, and no GL/Vulkan present to
 * block on.
 *
 * ─── What it is built from ────────────────────────────────────────────────
 *
 * NOT from the DirectDraw documentation, and not from guesses.  The game
 * *reads* what the driver reports — the mode list is indexed by Karoo.cfg,
 * `PickTextureFormatForDepth` (scenetexture.cpp) chooses from EnumTextureFormats,
 * `st_texture_caps` branches on D3DDEVICEDESC.dcmColorModel, and the game's
 * own EnumDisplayModesCallback filters on the FindDevice render bit depth.
 * Answer any of those differently and the game legitimately behaves
 * differently, and a replay diverges for a reason that has nothing to do with
 * headlessness.
 *
 * So every table below is a *recording* of what stock Wine ddraw answered on
 * this machine, captured with KAROO_DDRAW_DIAG=1 (ddrawdiag.cpp) and
 * transcribed verbatim:
 *
 *   s_devdesc     D3DDEVICEDESC, 0xfc bytes  — Device3::GetCaps, both HAL and
 *                 HEL (Wine returned byte-identical structures for the two)
 *   s_finddev     D3DFINDDEVICERESULT, 0x20c bytes — D3D3::FindDevice(HAL)
 *   s_texfmt      the 14 texture formats, in enumeration order
 *   s_zfmt        the 4 z-buffer formats, in enumeration order
 *   s_modes       the 31 resolutions, each enumerated at 32, 16 and 8 bpp in
 *                 that order — 93 modes, the order the game indexes into
 *
 * A consequence worth stating: the mode list is now FIXED rather than taken
 * from the host's monitor.  For a test harness that is the better property —
 * `Karoo.cfg`'s mode index means the same thing on every machine — but it
 * does mean a headless run and a windowed run agree only because this list
 * matches what Wine reports here.
 *
 * ─── Surfaces are real memory ─────────────────────────────────────────────
 *
 * The one thing that cannot be stubbed to S_OK.  The texture loaders write
 * pixels: texturetga.cpp Locks a staging surface and parses the TGA into it,
 * texturedib.cpp gets a DC and BitBlts a DIB onto it, scenetexture.cpp's
 * solid-fill FX Locks and fills.  A surface that hands back no memory turns
 * those into null-pointer writes.
 *
 * So every surface with an RGB pixel format is backed by a CreateDIBSection
 * allocation — which gives working Lock/Unlock (the bits) and working
 * GetDC/ReleaseDC (a memory DC with the section selected) from the same
 * storage, so a Lock after a BitBlt sees the blitted pixels.  Top-down, via a
 * negative biHeight, because DirectDraw surfaces are top-down and the game's
 * row arithmetic assumes it.  Z-buffers have no RGB format and nothing reads
 * them, so they get a plain heap allocation of the right size.
 *
 * Blt and BltFast really copy, for the same reason: TGA loading ends with a
 * Blt from the staging surface to the texture, and leaving that out would
 * make every texture's contents garbage.  Nothing samples them headless, but
 * "the pixels are right" is cheap here and keeps KAROO_TEXTURE_FX meaningful.
 * Stretching is NOT implemented — a Blt whose source and destination rects
 * differ in size logs once and copies nothing; the game does not do it.
 *
 * ─── What is deliberately not implemented ─────────────────────────────────
 *
 *   - Clippers.  CreateClipper returns DDERR_UNSUPPORTED; the capture shows
 *     the game never calls it.
 *   - Overlays, page locking, private data, uniqueness values: DD_OK or
 *     DDERR_UNSUPPORTED, none are called.
 *   - IDirectDraw4::GetCaps.  The capture shows no call, so there is no
 *     recording to replay; it zeroes the caller's buffers and returns DD_OK.
 *     UNVERIFIED — if something starts calling it, that is where to look.
 *   - Stretching Blts, colour-key blitting, and format conversion in Blt.
 *
 * Every one of those logs the first time it is reached, so a future caller
 * shows up in karoo_hooks.log rather than silently getting a wrong answer.
 */
#include <ddraw.h>
#include <d3d.h>
#include "nullddraw.h"
#include "log.h"
#include <string.h>

#define NOINLINE __attribute__((noinline))

/* ─── IIDs ─────────────────────────────────────────────────────────────── */

static const GUID IID_IDirectDraw4_g = {
    0x9c59509a, 0x39bd, 0x11d1, {0x8c,0x4a,0x00,0xc0,0x4f,0xd9,0x30,0xc5} };
static const GUID IID_IDirectDraw_g = {
    0x6c14db80, 0xa733, 0x11ce, {0xa5,0x21,0x00,0x20,0xaf,0x0b,0xe5,0x60} };
static const GUID IID_IDirect3D3_g = {
    0xbb223240, 0xe72b, 0x11d0, {0xa9,0xb4,0x00,0xaa,0x00,0xc0,0x99,0x3e} };
static const GUID IID_IDirect3DTexture2_g = {
    0x93281502, 0x8cf8, 0x11d0, {0x89,0xab,0x00,0xa0,0xc9,0x05,0x41,0x29} };

static bool guid_eq(REFIID a, const GUID *b)
{
    return memcmp(&a, b, sizeof(GUID)) == 0;
}

/* ─── The switch ───────────────────────────────────────────────────────── */

bool nulldd_enabled(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[8];
        cached = GetEnvironmentVariableA("KAROO_HEADLESS", buf, sizeof(buf))
                 && buf[0] != '0';
        log_write("nullddraw: headless mode %s\n", cached ? "ON" : "off");
    }
    return cached != 0;
}

/* One-shot log for a path the capture says is never taken. */
#define ONCE(name, ...)                                                  \
    do {                                                                 \
        static LONG once_##name = 0;                                     \
        if (InterlockedIncrement(&once_##name) == 1)                     \
            log_write(__VA_ARGS__);                                      \
    } while (0)

/* ─── Recorded tables (KAROO_DDRAW_DIAG=1, stock Wine ddraw) ───────────── */

/* D3DDEVICEDESC, 0xfc bytes.  Wine returned the same bytes for HAL and HEL.
 * dw[3] (dcmColorModel) is 0x9AFF1 — non-zero, which is what makes
 * st_texture_caps() ask for a video-memory texture rather than a
 * system-memory one, and dw[39] is the 0x700 render bit depth the game's
 * mode filter uses. */
static const DWORD s_devdesc[63] = {
    0x000000FC, 0x000007FF, 0x00000002, 0x0009AFF1, 0x00000008, 0x00000001,
    0x00000001, 0x00000010, 0x0000000F, 0x00000001, 0x00000008, 0x00000038,
    0x00000072, 0x003363B9, 0x000000FF, 0x00001FFF, 0x000007FF, 0x000000FF,
    0x000C528A, 0x00000D9F, 0x0703073F, 0x000000FF, 0x0000001F, 0x00000020,
    0x00000020, 0x00000038, 0x00000072, 0x003363B9, 0x000000FF, 0x00001FFF,
    0x000007FF, 0x000000FF, 0x000C528A, 0x00000D9F, 0x0703073F, 0x000000FF,
    0x0000001F, 0x00000020, 0x00000020, 0x00000700, 0x00000600, 0x00000000,
    0x00000800, 0x00000001, 0x00000001, 0x00004000, 0x00004000, 0x00000001,
    0x00000020, 0x00000001, 0x00000020, 0x00008000, 0x00004000, 0x00000010,
    0xC7000000, 0xC7000000, 0x47000000, 0x47000000, 0x00000000, 0x000000FF,
    0x00100008, 0x00FEF7FF, 0x00080008,
};

/* D3DFINDDEVICERESULT, 0x20c bytes: dwSize, the found GUID, then the HAL and
 * HEL D3DDEVICEDESCs.  CreateD3DDevice reads only ddHwDesc.dwFlags (non-zero
 * here) and ddHwDesc.dwDeviceRenderBitDepth out of this. */
static const DWORD s_finddev[131] = {
    0x0000020C, 0xAEF72D43, 0x4B7BB09A, 0x8AC698B7, 0x2A722D77, 0x000000FC,
    0x000007FF, 0x00000002, 0x0009AFF1, 0x00000008, 0x00000001, 0x00000001,
    0x00000010, 0x0000000F, 0x00000001, 0x00000008, 0x00000038, 0x00000072,
    0x003363B9, 0x000000FF, 0x00001FFF, 0x000007FF, 0x000000FF, 0x000C528A,
    0x00000D9F, 0x0703073F, 0x000000FF, 0x0000001F, 0x00000020, 0x00000020,
    0x00000038, 0x00000072, 0x003363B9, 0x000000FF, 0x00001FFF, 0x000007FF,
    0x000000FF, 0x000C528A, 0x00000D9F, 0x0703073F, 0x000000FF, 0x0000001F,
    0x00000020, 0x00000020, 0x00000700, 0x00000600, 0x00000000, 0x00000800,
    0x00000001, 0x00000001, 0x00004000, 0x00004000, 0x00000001, 0x00000020,
    0x00000001, 0x00000020, 0x00008000, 0x00004000, 0x00000010, 0xC7000000,
    0xC7000000, 0x47000000, 0x47000000, 0x00000000, 0x000000FF, 0x00100008,
    0x00FEF7FF, 0x00080008, 0x000000FC, 0x000007FF, 0x00000002, 0x0009AFF1,
    0x00000008, 0x00000001, 0x00000001, 0x00000010, 0x0000000F, 0x00000001,
    0x00000008, 0x00000038, 0x00000072, 0x003363B9, 0x000000FF, 0x00001FFF,
    0x000007FF, 0x000000FF, 0x000C528A, 0x00000D9F, 0x0703073F, 0x000000FF,
    0x0000001F, 0x00000020, 0x00000020, 0x00000038, 0x00000072, 0x003363B9,
    0x000000FF, 0x00001FFF, 0x000007FF, 0x000000FF, 0x000C528A, 0x00000D9F,
    0x0703073F, 0x000000FF, 0x0000001F, 0x00000020, 0x00000020, 0x00000700,
    0x00000600, 0x00000000, 0x00000800, 0x00000001, 0x00000001, 0x00004000,
    0x00004000, 0x00000001, 0x00000020, 0x00000001, 0x00000020, 0x00008000,
    0x00004000, 0x00000010, 0xC7000000, 0xC7000000, 0x47000000, 0x47000000,
    0x00000000, 0x000000FF, 0x00100008, 0x00FEF7FF, 0x00080008,
};

/* Compact pixel-format record: {flags, fourcc, bits, r, g, b, a}.  For a
 * z-buffer format the union members read as {stencil bit depth, z mask,
 * stencil mask} in the r/g/b slots — that is how DDPIXELFORMAT overlays
 * them, and how createdevice.cpp logs zdepth/stencil. */
struct PixFmtRec { DWORD flags, fourcc, bits, r, g, b, a; };

/* IDirect3DDevice3::EnumTextureFormats, in order.  PickTextureFormatForDepth
 * walks this, so ORDER IS LOAD-BEARING.
 *
 * The callback takes the FIRST eligible format unconditionally (nothing is kept
 * yet), and thereafter keeps whichever is closest to the request FROM ABOVE;
 * when alpha is asked for, an equal-depth format with strictly more alpha bits
 * also wins, up to a quarter of its own depth.  With this list and a 32-bit
 * request-with-alpha it climbs 16-555 -> 16-1555 -> 16-4444 -> 32-888 ->
 * 32-8888 and ends on the last, which is what karoo_hooks.log records.
 * See scenetexture.cpp for the full rule. */
static const PixFmtRec s_texfmt[] = {
    { 0x00000040, 0,          16, 0x00007C00, 0x000003E0, 0x0000001F, 0x00000000 },
    { 0x00000041, 0,          16, 0x00007C00, 0x000003E0, 0x0000001F, 0x00008000 },
    { 0x00000041, 0,          16, 0x00000F00, 0x000000F0, 0x0000000F, 0x0000F000 },
    { 0x00000040, 0,          16, 0x0000F800, 0x000007E0, 0x0000001F, 0x00000000 },
    { 0x00000040, 0,          32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0x00000000 },
    { 0x00000041, 0,          32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000 },
    { 0x00000004, 0x31545844,  0, 0, 0, 0, 0 },          /* DXT1 */
    { 0x00000004, 0x32545844,  0, 0, 0, 0, 0 },          /* DXT2 */
    { 0x00000004, 0x33545844,  0, 0, 0, 0, 0 },          /* DXT3 */
    { 0x00000004, 0x34545844,  0, 0, 0, 0, 0 },          /* DXT4 */
    { 0x00000004, 0x35545844,  0, 0, 0, 0, 0 },          /* DXT5 */
    { 0x00080000, 0,          16, 0x000000FF, 0x0000FF00, 0x00000000, 0x00000000 },
    { 0x000C0000, 0,          16, 0x0000001F, 0x000003E0, 0x0000FC00, 0x00000000 },
    { 0x000C0000, 0,          32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0x00000000 },
};

/* IDirect3D3::EnumZBufferFormats(HAL), in order.  createdevice.cpp's
 * EnumZBufferFormatsCallback picks from these; with this list it lands on the third — 32-bit
 * z with 8-bit stencil — which is what createdevice.cpp logs as
 * "zdepth=32 stencil=8" on a real run. */
static const PixFmtRec s_zfmt[] = {
    { 0x00000400, 0, 16, 0x00000000, 0x0000FFFF, 0x00000000, 0x00000000 },
    { 0x00000400, 0, 32, 0x00000000, 0x00FFFFFF, 0x00000000, 0x00000000 },
    { 0x00004400, 0, 32, 0x00000008, 0x00FFFFFF, 0xFF000000, 0x00000000 },
    { 0x00000400, 0, 24, 0x00000000, 0x00FFFFFF, 0x00000000, 0x00000000 },
};

/* The 31 resolutions, in enumeration order.  Each is enumerated three times,
 * at 32, 16 and 8 bpp in that order — 93 modes total.  Karoo.cfg's mode index
 * (3 in Karoo.cfg.default) indexes the list the game's own callback builds by
 * filtering these, and lands on 1024x768x32. */
static const struct { WORD w, h; } s_modes[] = {
    {  320,  200 }, {  320,  240 }, {  640,  350 }, {  640,  400 },
    {  640,  480 }, {  720,  400 }, {  720,  480 }, {  768,  480 },
    {  800,  500 }, {  800,  600 }, {  864,  486 }, {  928,  580 },
    {  960,  540 }, {  960,  600 }, { 1024,  576 }, { 1024,  768 },
    { 1152,  720 }, { 1152,  864 }, { 1280,  720 }, { 1280,  800 },
    { 1280,  960 }, { 1280, 1024 }, { 1368,  768 }, { 1400, 1050 },
    { 1440,  900 }, { 1440,  960 }, { 1440, 1080 }, { 1600,  900 },
    { 1680, 1050 }, { 1920,  800 }, { 1920, 1080 },
};
static const DWORD s_mode_depths[3] = { 32, 16, 8 };

/* Per-depth display-mode pixel formats, exactly as captured. */
static void mode_pixfmt(DWORD bpp, DDPIXELFORMAT *pf)
{
    memset(pf, 0, sizeof(*pf));
    pf->dwSize        = sizeof(DDPIXELFORMAT);
    pf->dwRGBBitCount = bpp;
    if (bpp == 32) {
        pf->dwFlags    = 0x40;                 /* DDPF_RGB */
        pf->dwRBitMask = 0x00FF0000;
        pf->dwGBitMask = 0x0000FF00;
        pf->dwBBitMask = 0x000000FF;
    } else if (bpp == 16) {
        pf->dwFlags    = 0x40;
        pf->dwRBitMask = 0x0000F800;
        pf->dwGBitMask = 0x000007E0;
        pf->dwBBitMask = 0x0000001F;
    } else {
        pf->dwFlags    = 0x60;                 /* DDPF_RGB|DDPF_PALETTEINDEXED8 */
    }
}

static void rec_to_pixfmt(const PixFmtRec *r, DDPIXELFORMAT *pf)
{
    memset(pf, 0, sizeof(*pf));
    pf->dwSize               = sizeof(DDPIXELFORMAT);
    pf->dwFlags              = r->flags;
    pf->dwFourCC             = r->fourcc;
    pf->dwRGBBitCount        = r->bits;
    pf->dwRBitMask           = r->r;
    pf->dwGBitMask           = r->g;
    pf->dwBBitMask           = r->b;
    pf->dwRGBAlphaBitMask    = r->a;
}

/* ─── Current display mode ─────────────────────────────────────────────────
 * SetDisplayMode does not touch anything real; it just records what the game
 * asked for, because CreateSurface for the primary takes its size and format
 * from here (the game passes no width/height for a primary). */
static DWORD s_cur_w = 1024, s_cur_h = 768, s_cur_bpp = 32;

/* ─── Surfaces ─────────────────────────────────────────────────────────────
 *
 * A static pool: no allocator dependency, and a
 * hard ceiling that shows up as a log line rather than as heap corruption.
 * 512 covers the observed working set (a level tops out around 120 live
 * texture surfaces) with room to spare; exhaustion is logged and fails the
 * CreateSurface, which the game handles.
 */
#define SURF_POOL_SIZE 512
#define MAX_ATTACHED   4

struct NullSurface {
    void          **vtable;      /* must be first — COM ABI */
    LONG            ref;
    DDSURFACEDESC2  desc;        /* what GetSurfaceDesc/Lock report */
    void           *bits;
    HBITMAP         dib;         /* non-NULL when the bits are a DIB section */
    HDC             dc;          /* live only between GetDC and ReleaseDC */
    HGDIOBJ         old_bm;
    NullSurface    *attached[MAX_ATTACHED];
    int             n_attached;
    IDirectDrawPalette *palette;
    DDCOLORKEY      ckey;
    DWORD           ckey_flags;
    void           *tex2;        /* lazily created IDirect3DTexture2, if asked */
    int             in_use;
};

static NullSurface s_surf_pool[SURF_POOL_SIZE];
static void       *s_surf_vtable[45];

static NullSurface *surf_alloc(void)
{
    for (int i = 0; i < SURF_POOL_SIZE; i++) {
        if (!s_surf_pool[i].in_use) {
            NullSurface *s = &s_surf_pool[i];
            memset(s, 0, sizeof(*s));
            s->vtable = s_surf_vtable;
            s->ref    = 1;
            s->in_use = 1;
            return s;
        }
    }
    log_write("nullddraw: surface pool EXHAUSTED (%d in use)\n", SURF_POOL_SIZE);
    return NULL;
}

static DWORD pitch_for(DWORD w, DWORD bpp)
{
    if (bpp == 0) bpp = 32;                    /* FourCC/compressed: not used */
    return ((w * bpp + 31) / 32) * 4;
}

/* Back the surface with real memory.  A DIB section when the format is RGB or
 * palettised, so GetDC works off the same storage as Lock; a plain heap block
 * otherwise (z-buffers). */
static bool surf_alloc_bits(NullSurface *s)
{
    DWORD w   = s->desc.dwWidth;
    DWORD h   = s->desc.dwHeight;
    DWORD bpp = s->desc.ddpfPixelFormat.dwRGBBitCount;
    DWORD pitch = pitch_for(w, bpp);

    s->desc.lPitch  = (LONG)pitch;
    s->desc.dwFlags |= DDSD_PITCH;

    if (w == 0 || h == 0)
        return true;                            /* nothing to allocate */

    bool rgb = (s->desc.ddpfPixelFormat.dwFlags & (0x40 | 0x20)) != 0
               && (bpp == 8 || bpp == 16 || bpp == 32);

    if (rgb) {
        /* BITMAPINFO with room for the three BI_BITFIELDS masks (16bpp) or a
         * 256-entry palette (8bpp), whichever is larger. */
        struct { BITMAPINFOHEADER h; DWORD extra[256]; } bi;
        memset(&bi, 0, sizeof(bi));
        bi.h.biSize        = sizeof(BITMAPINFOHEADER);
        bi.h.biWidth       = (LONG)w;
        bi.h.biHeight      = -(LONG)h;          /* top-down, like DirectDraw */
        bi.h.biPlanes      = 1;
        bi.h.biBitCount    = (WORD)bpp;
        bi.h.biCompression = BI_RGB;
        UINT usage = DIB_RGB_COLORS;

        if (bpp == 16) {
            bi.h.biCompression = BI_BITFIELDS;
            bi.extra[0] = s->desc.ddpfPixelFormat.dwRBitMask;
            bi.extra[1] = s->desc.ddpfPixelFormat.dwGBitMask;
            bi.extra[2] = s->desc.ddpfPixelFormat.dwBBitMask;
        } else if (bpp == 8) {
            /* A greyscale ramp is a placeholder: the game sets a real palette
             * through SetPalette when it has one, and nothing here samples
             * the DIB's own colour table. */
            bi.h.biClrUsed = 256;
            for (int i = 0; i < 256; i++)
                bi.extra[i] = (DWORD)((i << 16) | (i << 8) | i);
        }

        HDC screen = CreateCompatibleDC(NULL);
        void *pv = NULL;
        s->dib = CreateDIBSection(screen, (BITMAPINFO *)&bi, usage, &pv, NULL, 0);
        if (screen) DeleteDC(screen);
        if (s->dib && pv) {
            s->bits = pv;
            memset(pv, 0, pitch * h);
            return true;
        }
        /* CreateDIBSection is not expected to fail; fall through to the heap
         * so the surface is still usable for Lock, and say so. */
        ONCE(dibfail, "nullddraw: CreateDIBSection failed (%lux%lu %lubpp) — "
                      "falling back to heap bits, GetDC will not work\n",
             (unsigned long)w, (unsigned long)h, (unsigned long)bpp);
        s->dib = NULL;
    }

    s->bits = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, pitch * h);
    return s->bits != NULL;
}

static void surf_free_bits(NullSurface *s)
{
    if (s->dc) {
        if (s->old_bm) SelectObject(s->dc, s->old_bm);
        DeleteDC(s->dc);
        s->dc = NULL;
        s->old_bm = NULL;
    }
    if (s->dib) {
        DeleteObject(s->dib);
        s->dib  = NULL;
        s->bits = NULL;
    } else if (s->bits) {
        HeapFree(GetProcessHeap(), 0, s->bits);
        s->bits = NULL;
    }
}

/* ─── IDirect3DTexture2 ────────────────────────────────────────────────────
 * Handles must be non-zero and distinct: the game passes them to SetTexture
 * and to SetLightState, and treats 0 as "none". */
struct NullTexture2 {
    void       **vtable;
    LONG         ref;
    NullSurface *surf;
    DWORD        handle;
    int          in_use;
};

#define TEX_POOL_SIZE 512
static NullTexture2 s_tex_pool[TEX_POOL_SIZE];
static void        *s_tex_vtable[6];

static HRESULT WINAPI NOINLINE nt_QueryInterface(NullTexture2 *s, REFIID r, void **p)
{
    (void)r;
    if (!p) return E_POINTER;
    *p = s;
    InterlockedIncrement(&s->ref);
    return S_OK;
}
static ULONG WINAPI NOINLINE nt_AddRef(NullTexture2 *s)
    { return (ULONG)InterlockedIncrement(&s->ref); }
static ULONG WINAPI NOINLINE nt_Release(NullTexture2 *s)
{
    LONG rc = InterlockedDecrement(&s->ref);
    if (rc <= 0) {
        if (s->surf) s->surf->tex2 = NULL;
        s->in_use = 0;
        return 0;
    }
    return (ULONG)rc;
}
static HRESULT WINAPI NOINLINE nt_GetHandle(NullTexture2 *s, void *dev, DWORD *h)
{
    (void)dev;
    if (!h) return E_POINTER;
    *h = s->handle;
    return S_OK;
}
static HRESULT WINAPI NOINLINE nt_PaletteChanged(NullTexture2 *s, DWORD a, DWORD b)
    { (void)s; (void)a; (void)b; return S_OK; }
static HRESULT WINAPI NOINLINE nt_Load(NullTexture2 *s, NullTexture2 *src)
    { (void)s; (void)src; return S_OK; }

static NullTexture2 *tex_for_surface(NullSurface *surf)
{
    if (surf->tex2) {
        NullTexture2 *t = (NullTexture2 *)surf->tex2;
        InterlockedIncrement(&t->ref);
        return t;
    }
    for (int i = 0; i < TEX_POOL_SIZE; i++) {
        if (!s_tex_pool[i].in_use) {
            NullTexture2 *t = &s_tex_pool[i];
            t->vtable = s_tex_vtable;
            t->ref    = 1;
            t->surf   = surf;
            t->handle = (DWORD)(i + 1);        /* non-zero, distinct */
            t->in_use = 1;
            surf->tex2 = t;
            return t;
        }
    }
    log_write("nullddraw: texture pool EXHAUSTED (%d in use)\n", TEX_POOL_SIZE);
    return NULL;
}

/* ─── IDirectDrawPalette ───────────────────────────────────────────────────
 * Created for <= 8bpp textures by the game's CreatePaletteFromDIB.  The
 * entries are stored so GetEntries round-trips; nothing samples them. */
struct NullPalette {
    void        **vtable;
    LONG          ref;
    PALETTEENTRY  entries[256];
    int           in_use;
};

#define PAL_POOL_SIZE 64
static NullPalette s_pal_pool[PAL_POOL_SIZE];
static void       *s_pal_vtable[7];

static HRESULT WINAPI NOINLINE np_QueryInterface(NullPalette *s, REFIID r, void **p)
    { (void)r; if (!p) return E_POINTER; *p = s; InterlockedIncrement(&s->ref); return S_OK; }
static ULONG WINAPI NOINLINE np_AddRef(NullPalette *s)
    { return (ULONG)InterlockedIncrement(&s->ref); }
static ULONG WINAPI NOINLINE np_Release(NullPalette *s)
{
    LONG rc = InterlockedDecrement(&s->ref);
    if (rc <= 0) { s->in_use = 0; return 0; }
    return (ULONG)rc;
}
static HRESULT WINAPI NOINLINE np_GetCaps(NullPalette *s, DWORD *caps)
    { (void)s; if (caps) *caps = DDPCAPS_8BIT | DDPCAPS_ALLOW256; return S_OK; }
static HRESULT WINAPI NOINLINE np_GetEntries(NullPalette *s, DWORD flags, DWORD start, DWORD count, PALETTEENTRY *pe)
{
    (void)flags;
    if (!pe || start + count > 256) return DDERR_INVALIDPARAMS;
    memcpy(pe, s->entries + start, count * sizeof(PALETTEENTRY));
    return S_OK;
}
static HRESULT WINAPI NOINLINE np_Initialize(NullPalette *s, void *dd, DWORD flags, PALETTEENTRY *pe)
    { (void)s; (void)dd; (void)flags; (void)pe; return DDERR_ALREADYINITIALIZED; }
static HRESULT WINAPI NOINLINE np_SetEntries(NullPalette *s, DWORD flags, DWORD start, DWORD count, PALETTEENTRY *pe)
{
    (void)flags;
    if (!pe || start + count > 256) return DDERR_INVALIDPARAMS;
    memcpy(s->entries + start, pe, count * sizeof(PALETTEENTRY));
    return S_OK;
}

/* ─── IDirectDrawSurface4 (45 slots) ───────────────────────────────────── */

static HRESULT WINAPI NOINLINE ns_QueryInterface(NullSurface *s, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (guid_eq(riid, &IID_IDirect3DTexture2_g)) {
        NullTexture2 *t = tex_for_surface(s);
        if (!t) { *ppv = NULL; return E_OUTOFMEMORY; }
        *ppv = t;
        return S_OK;
    }
    *ppv = s;
    InterlockedIncrement(&s->ref);
    return S_OK;
}
static ULONG WINAPI NOINLINE ns_AddRef(NullSurface *s)
    { return (ULONG)InterlockedIncrement(&s->ref); }
static ULONG WINAPI NOINLINE ns_Release(NullSurface *s)
{
    LONG rc = InterlockedDecrement(&s->ref);
    if (rc <= 0) {
        /* A complex primary owns its back buffer, exactly as DirectDraw does. */
        for (int i = 0; i < s->n_attached; i++)
            if (s->attached[i] && (s->attached[i]->desc.ddsCaps.dwCaps & DDSCAPS_BACKBUFFER))
                ns_Release(s->attached[i]);
        /* Break the link to any IDirect3DTexture2 handed out for this surface:
         * the pool slot is about to be reused, and the texture's `surf` would
         * otherwise point at a different surface's storage. */
        if (s->tex2)
            ((NullTexture2 *)s->tex2)->surf = NULL;
        surf_free_bits(s);
        s->in_use = 0;
        return 0;
    }
    return (ULONG)rc;
}
static HRESULT WINAPI NOINLINE ns_AddAttachedSurface(NullSurface *s, NullSurface *att)
{
    if (!att) return DDERR_INVALIDPARAMS;
    if (s->n_attached >= MAX_ATTACHED) {
        log_write("nullddraw: too many attached surfaces on %p\n", s);
        return DDERR_CANNOTATTACHSURFACE;
    }
    s->attached[s->n_attached++] = att;
    InterlockedIncrement(&att->ref);
    return S_OK;
}
static HRESULT WINAPI NOINLINE ns_AddOverlayDirtyRect(NullSurface *s, RECT *r)
    { (void)s; (void)r; return DDERR_UNSUPPORTED; }

/* Row copy.  Same bpp and same rect size only — see the header comment. */
static void surf_copy_rect(NullSurface *dst, const RECT *dr,
                           NullSurface *src, const RECT *sr)
{
    RECT d = { 0, 0, (LONG)dst->desc.dwWidth, (LONG)dst->desc.dwHeight };
    RECT c = { 0, 0, (LONG)src->desc.dwWidth, (LONG)src->desc.dwHeight };
    if (dr) d = *dr;
    if (sr) c = *sr;

    DWORD bpp = dst->desc.ddpfPixelFormat.dwRGBBitCount;
    if (!dst->bits || !src->bits ||
        bpp == 0 || bpp != src->desc.ddpfPixelFormat.dwRGBBitCount ||
        (d.right - d.left) != (c.right - c.left) ||
        (d.bottom - d.top) != (c.bottom - c.top)) {
        ONCE(bltskip, "nullddraw: Blt not copied — %ldx%ld@%lubpp <- %ldx%ld@%lubpp "
                      "(stretch or format change is not implemented)\n",
             (long)(d.right - d.left), (long)(d.bottom - d.top),
             (unsigned long)bpp,
             (long)(c.right - c.left), (long)(c.bottom - c.top),
             (unsigned long)src->desc.ddpfPixelFormat.dwRGBBitCount);
        return;
    }

    /* Clip both rects into their surfaces. */
    if (d.left < 0 || d.top < 0 || c.left < 0 || c.top < 0) return;
    if (d.right  > (LONG)dst->desc.dwWidth  || d.bottom > (LONG)dst->desc.dwHeight) return;
    if (c.right  > (LONG)src->desc.dwWidth  || c.bottom > (LONG)src->desc.dwHeight) return;

    LONG rows  = d.bottom - d.top;
    LONG bytes = (LONG)(((d.right - d.left) * bpp) / 8);
    for (LONG y = 0; y < rows; y++) {
        BYTE *dp = (BYTE *)dst->bits + (d.top + y) * dst->desc.lPitch
                 + (d.left * bpp) / 8;
        BYTE *sp = (BYTE *)src->bits + (c.top + y) * src->desc.lPitch
                 + (c.left * bpp) / 8;
        memcpy(dp, sp, bytes);
    }
}

static HRESULT WINAPI NOINLINE ns_Blt(NullSurface *s, RECT *dr, NullSurface *src,
                                      RECT *sr, DWORD flags, DDBLTFX *fx)
{
    if (flags & DDBLT_COLORFILL) {
        if (s->bits && fx) {
            DWORD bpp = s->desc.ddpfPixelFormat.dwRGBBitCount;
            RECT d = { 0, 0, (LONG)s->desc.dwWidth, (LONG)s->desc.dwHeight };
            if (dr) d = *dr;
            for (LONG y = d.top; y < d.bottom; y++) {
                BYTE *p = (BYTE *)s->bits + y * s->desc.lPitch;
                for (LONG x = d.left; x < d.right; x++) {
                    if (bpp == 32)      ((DWORD *)p)[x] = fx->dwFillColor;
                    else if (bpp == 16) ((WORD  *)p)[x] = (WORD)fx->dwFillColor;
                    else if (bpp == 8)  ((BYTE  *)p)[x] = (BYTE)fx->dwFillColor;
                }
            }
        }
        return S_OK;
    }
    if (src)
        surf_copy_rect(s, dr, src, sr);
    return S_OK;
}
static HRESULT WINAPI NOINLINE ns_BltBatch(NullSurface *s, void *b, DWORD n, DWORD f)
    { (void)s; (void)b; (void)n; (void)f; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI NOINLINE ns_BltFast(NullSurface *s, DWORD x, DWORD y,
                                          NullSurface *src, RECT *sr, DWORD trans)
{
    (void)trans;
    if (!src) return DDERR_INVALIDPARAMS;
    RECT c = { 0, 0, (LONG)src->desc.dwWidth, (LONG)src->desc.dwHeight };
    if (sr) c = *sr;
    RECT d = { (LONG)x, (LONG)y, (LONG)x + (c.right - c.left),
               (LONG)y + (c.bottom - c.top) };
    surf_copy_rect(s, &d, src, &c);
    return S_OK;
}
static HRESULT WINAPI NOINLINE ns_DeleteAttachedSurface(NullSurface *s, DWORD f, NullSurface *att)
{
    (void)f;
    for (int i = 0; i < s->n_attached; i++) {
        if (s->attached[i] == att || !att) {
            ns_Release(s->attached[i]);
            for (int j = i; j + 1 < s->n_attached; j++)
                s->attached[j] = s->attached[j + 1];
            s->n_attached--;
            if (att) return S_OK;
            i--;
        }
    }
    return S_OK;
}
static HRESULT WINAPI NOINLINE ns_EnumAttachedSurfaces(NullSurface *s, void *ctx, void *cb)
    { (void)s; (void)ctx; (void)cb; return S_OK; }
static HRESULT WINAPI NOINLINE ns_EnumOverlayZOrders(NullSurface *s, DWORD f, void *ctx, void *cb)
    { (void)s; (void)f; (void)ctx; (void)cb; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI NOINLINE ns_Flip(NullSurface *s, NullSurface *over, DWORD flags)
{
    /* Nothing is presented, so a flip has nothing to do.  The bits are not
     * even swapped: the game never reads the primary back. */
    (void)s; (void)over; (void)flags;
    return S_OK;
}
static HRESULT WINAPI NOINLINE ns_GetAttachedSurface(NullSurface *s, DDSCAPS2 *caps,
                                                     NullSurface **att)
{
    if (!att || !caps) return DDERR_INVALIDPARAMS;
    for (int i = 0; i < s->n_attached; i++) {
        if ((s->attached[i]->desc.ddsCaps.dwCaps & caps->dwCaps) == caps->dwCaps) {
            *att = s->attached[i];
            InterlockedIncrement(&(*att)->ref);
            return S_OK;
        }
    }
    *att = NULL;
    return DDERR_NOTFOUND;
}
static HRESULT WINAPI NOINLINE ns_GetBltStatus(NullSurface *s, DWORD f)
    { (void)s; (void)f; return S_OK; }
static HRESULT WINAPI NOINLINE ns_GetCaps(NullSurface *s, DDSCAPS2 *caps)
    { if (!caps) return DDERR_INVALIDPARAMS; *caps = s->desc.ddsCaps; return S_OK; }
static HRESULT WINAPI NOINLINE ns_GetClipper(NullSurface *s, void **pp)
    { (void)s; if (pp) *pp = NULL; return DDERR_NOCLIPPERATTACHED; }
static HRESULT WINAPI NOINLINE ns_GetColorKey(NullSurface *s, DWORD flags, DDCOLORKEY *k)
{
    (void)flags;
    if (!k) return DDERR_INVALIDPARAMS;
    if (!s->ckey_flags) return DDERR_NOCOLORKEY;
    *k = s->ckey;
    return S_OK;
}
static HRESULT WINAPI NOINLINE ns_GetDC(NullSurface *s, HDC *phdc)
{
    if (!phdc) return E_POINTER;
    *phdc = NULL;
    if (!s->dib) {
        ONCE(nodc, "nullddraw: GetDC on a surface with no DIB backing "
                   "(%lux%lu %lubpp caps=%08lX)\n",
             (unsigned long)s->desc.dwWidth, (unsigned long)s->desc.dwHeight,
             (unsigned long)s->desc.ddpfPixelFormat.dwRGBBitCount,
             (unsigned long)s->desc.ddsCaps.dwCaps);
        return DDERR_CANTCREATEDC;
    }
    if (s->dc) return DDERR_DCALREADYCREATED;
    s->dc = CreateCompatibleDC(NULL);
    if (!s->dc) return DDERR_CANTCREATEDC;
    s->old_bm = SelectObject(s->dc, s->dib);
    *phdc = s->dc;
    return S_OK;
}
static HRESULT WINAPI NOINLINE ns_GetFlipStatus(NullSurface *s, DWORD f)
    { (void)s; (void)f; return S_OK; }
static HRESULT WINAPI NOINLINE ns_GetOverlayPosition(NullSurface *s, LONG *x, LONG *y)
    { (void)s; (void)x; (void)y; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI NOINLINE ns_GetPalette(NullSurface *s, void **pp)
{
    if (!pp) return E_POINTER;
    *pp = s->palette;
    if (!s->palette) return DDERR_NOPALETTEATTACHED;
    ((NullPalette *)s->palette)->ref++;
    return S_OK;
}
static HRESULT WINAPI NOINLINE ns_GetPixelFormat(NullSurface *s, DDPIXELFORMAT *pf)
    { if (!pf) return DDERR_INVALIDPARAMS; *pf = s->desc.ddpfPixelFormat; return S_OK; }
static HRESULT WINAPI NOINLINE ns_GetSurfaceDesc(NullSurface *s, DDSURFACEDESC2 *d)
{
    if (!d) return DDERR_INVALIDPARAMS;
    DWORD size = d->dwSize ? d->dwSize : sizeof(DDSURFACEDESC2);
    if (size > sizeof(DDSURFACEDESC2)) size = sizeof(DDSURFACEDESC2);
    memcpy(d, &s->desc, size);
    d->dwSize = size;
    return S_OK;
}
static HRESULT WINAPI NOINLINE ns_Initialize(NullSurface *s, void *dd, DDSURFACEDESC2 *d)
    { (void)s; (void)dd; (void)d; return DDERR_ALREADYINITIALIZED; }
static HRESULT WINAPI NOINLINE ns_IsLost(NullSurface *s)
    { (void)s; return S_OK; }             /* never lost: there is no device */
static HRESULT WINAPI NOINLINE ns_Lock(NullSurface *s, RECT *r, DDSURFACEDESC2 *d,
                                       DWORD flags, HANDLE ev)
{
    (void)flags; (void)ev;
    if (!d) return DDERR_INVALIDPARAMS;
    DWORD size = d->dwSize ? d->dwSize : sizeof(DDSURFACEDESC2);
    if (size > sizeof(DDSURFACEDESC2)) size = sizeof(DDSURFACEDESC2);
    memcpy(d, &s->desc, size);
    d->dwSize   = size;
    d->dwFlags |= DDSD_LPSURFACE;

    BYTE *p = (BYTE *)s->bits;
    if (r && p) {
        DWORD bpp = s->desc.ddpfPixelFormat.dwRGBBitCount;
        p += r->top * s->desc.lPitch + (r->left * bpp) / 8;
        d->dwWidth  = (DWORD)(r->right  - r->left);
        d->dwHeight = (DWORD)(r->bottom - r->top);
    }
    d->lpSurface = p;
    return p ? S_OK : DDERR_GENERIC;
}
static HRESULT WINAPI NOINLINE ns_ReleaseDC(NullSurface *s, HDC hdc)
{
    if (!s->dc || hdc != s->dc) return DDERR_INVALIDPARAMS;
    if (s->old_bm) SelectObject(s->dc, s->old_bm);
    DeleteDC(s->dc);
    s->dc = NULL;
    s->old_bm = NULL;
    return S_OK;
}
static HRESULT WINAPI NOINLINE ns_Restore(NullSurface *s)
    { (void)s; return S_OK; }
static HRESULT WINAPI NOINLINE ns_SetClipper(NullSurface *s, void *cl)
    { (void)s; (void)cl; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI NOINLINE ns_SetColorKey(NullSurface *s, DWORD flags, DDCOLORKEY *k)
{
    if (k) { s->ckey = *k; s->ckey_flags = flags; }
    else     s->ckey_flags = 0;
    return S_OK;
}
static HRESULT WINAPI NOINLINE ns_SetOverlayPosition(NullSurface *s, LONG x, LONG y)
    { (void)s; (void)x; (void)y; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI NOINLINE ns_SetPalette(NullSurface *s, void *pal)
    { s->palette = (IDirectDrawPalette *)pal; return S_OK; }
static HRESULT WINAPI NOINLINE ns_Unlock(NullSurface *s, RECT *r)
    { (void)s; (void)r; return S_OK; }
static HRESULT WINAPI NOINLINE ns_UpdateOverlay(NullSurface *s, RECT *sr, NullSurface *d, RECT *dr, DWORD f, void *fx)
    { (void)s; (void)sr; (void)d; (void)dr; (void)f; (void)fx; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI NOINLINE ns_UpdateOverlayDisplay(NullSurface *s, DWORD f)
    { (void)s; (void)f; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI NOINLINE ns_UpdateOverlayZOrder(NullSurface *s, DWORD f, NullSurface *ref)
    { (void)s; (void)f; (void)ref; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI NOINLINE ns_GetDDInterface(NullSurface *s, void **pp);
static HRESULT WINAPI NOINLINE ns_PageLock(NullSurface *s, DWORD f)
    { (void)s; (void)f; return S_OK; }
static HRESULT WINAPI NOINLINE ns_PageUnlock(NullSurface *s, DWORD f)
    { (void)s; (void)f; return S_OK; }
static HRESULT WINAPI NOINLINE ns_SetSurfaceDesc(NullSurface *s, DDSURFACEDESC2 *d, DWORD f)
    { (void)s; (void)d; (void)f; return DDERR_UNSUPPORTED; }
static HRESULT WINAPI NOINLINE ns_SetPrivateData(NullSurface *s, REFGUID g, void *p, DWORD n, DWORD f)
    { (void)s; (void)g; (void)p; (void)n; (void)f; return S_OK; }
static HRESULT WINAPI NOINLINE ns_GetPrivateData(NullSurface *s, REFGUID g, void *p, DWORD *n)
    { (void)s; (void)g; (void)p; (void)n; return DDERR_NOTFOUND; }
static HRESULT WINAPI NOINLINE ns_FreePrivateData(NullSurface *s, REFGUID g)
    { (void)s; (void)g; return S_OK; }
static HRESULT WINAPI NOINLINE ns_GetUniquenessValue(NullSurface *s, DWORD *v)
    { (void)s; if (v) *v = 1; return S_OK; }
static HRESULT WINAPI NOINLINE ns_ChangeUniquenessValue(NullSurface *s)
    { (void)s; return S_OK; }

/* ─── IDirect3D3 / IDirect3DDevice3 / viewport / material / light ────────
 *
 * These are singletons: the game creates exactly one of each (createdevice.cpp
 * and RenderDevice::SetMaterial / SetDirectionalLight), so there is nothing to pool.  Every
 * render entry point returns D3D_OK without recording anything — the point of
 * headless mode is that no drawing happens.
 */

struct NullObj { void **vtable; LONG ref; };

static void       *s_d3d3_vtable[12];
static void       *s_dev3_vtable[42];
static void       *s_vp3_vtable[21];
static void       *s_mat3_vtable[6];
static void       *s_light_vtable[6];

static NullObj s_d3d3  = { NULL, 1 };
static NullObj s_dev3  = { NULL, 1 };
static NullObj s_vp3   = { NULL, 1 };
static NullObj s_light = { NULL, 1 };

/* The material keeps its own handle, for the same reason textures do. */
struct NullMaterial { void **vtable; LONG ref; D3DMATERIAL mat; DWORD handle; };
static NullMaterial s_mat3 = { NULL, 1, {}, 0x1000 };

static ULONG WINAPI NOINLINE no_AddRef(NullObj *s)
    { return (ULONG)InterlockedIncrement(&s->ref); }
static ULONG WINAPI NOINLINE no_Release(NullObj *s)
{
    LONG rc = InterlockedDecrement(&s->ref);
    return rc > 0 ? (ULONG)rc : 0;
}
static HRESULT WINAPI NOINLINE no_QueryInterface(NullObj *s, REFIID r, void **p)
    { (void)r; if (!p) return E_POINTER; *p = s; InterlockedIncrement(&s->ref); return S_OK; }

/* ─── Arity-correct filler stubs ───────────────────────────────────────────
 *
 * A slot that must exist but does nothing.  These MUST take the same number of
 * arguments as the method they stand in for: COM vtable methods are __stdcall,
 * so the CALLEE pops the arguments.  A single zero-argument `no_ok()` used for
 * every such slot pops nothing while the caller has pushed several — the stack
 * pointer walks up by 4 bytes per unpopped argument, and execution eventually
 * returns into stack garbage.
 *
 * That is not hypothetical: the first headless run crashed exactly this way,
 * immediately after IDirect3DViewport3::AddLight —
 *
 *   Unhandled page fault on write access to 00000001 at address 007EFD82
 *   eip=007efd82 esp=007efd00     <- eip is INSIDE the stack
 *   esi=7a5ee348                  <- the light object we had just handed out
 *
 * So there is no catch-all here.  okN/failN are generated per argument count
 * (N counts `this`), and every slot below names the one matching its own
 * signature, taken from the COM interface declaration in d3d.h / ddraw.h.
 */
#define OK_STUB(n, params) \
    static HRESULT WINAPI NOINLINE ok##n params { return S_OK; }
#define FAIL_STUB(n, params) \
    static HRESULT WINAPI NOINLINE fail##n params { return DDERR_UNSUPPORTED; }

#define A1 (void *a)
#define A2 (void *a, DWORD b)
#define A3 (void *a, DWORD b, DWORD c)
#define A4 (void *a, DWORD b, DWORD c, DWORD d)
#define A5 (void *a, DWORD b, DWORD c, DWORD d, DWORD e)
#define A6 (void *a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f)
#define A7 (void *a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f, DWORD g)
#define A8 (void *a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f, DWORD g, DWORD h)

/* The parameters exist only to give each stub the right stdcall stack
 * cleanup; no stub reads them. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
OK_STUB(1, A1) OK_STUB(2, A2) OK_STUB(3, A3) OK_STUB(4, A4)
OK_STUB(5, A5) OK_STUB(6, A6) OK_STUB(7, A7) OK_STUB(8, A8)

FAIL_STUB(1, A1) FAIL_STUB(2, A2) FAIL_STUB(3, A3) FAIL_STUB(4, A4)
FAIL_STUB(5, A5)
#pragma GCC diagnostic pop

/* --- IDirect3DDevice3 (42 slots) --- */

static HRESULT WINAPI NOINLINE nd_GetCaps(NullObj *s, void *hal, void *hel)
{
    (void)s;
    if (hal) memcpy(hal, s_devdesc, sizeof(s_devdesc));
    if (hel) memcpy(hel, s_devdesc, sizeof(s_devdesc));
    return S_OK;
}
static HRESULT WINAPI NOINLINE nd_EnumTextureFormats(NullObj *s,
        LPD3DENUMPIXELFORMATSCALLBACK cb, void *ctx)
{
    (void)s;
    if (!cb) return DDERR_INVALIDPARAMS;
    for (unsigned i = 0; i < sizeof(s_texfmt) / sizeof(s_texfmt[0]); i++) {
        DDPIXELFORMAT pf;
        rec_to_pixfmt(&s_texfmt[i], &pf);
        if (cb(&pf, ctx) == D3DENUMRET_CANCEL)
            break;
    }
    return S_OK;
}
static HRESULT WINAPI NOINLINE nd_GetDirect3D(NullObj *s, void **pp)
{
    (void)s;
    if (!pp) return E_POINTER;
    *pp = &s_d3d3;
    InterlockedIncrement(&s_d3d3.ref);
    return S_OK;
}
static HRESULT WINAPI NOINLINE nd_GetCurrentViewport(NullObj *s, void **pp)
{
    (void)s;
    if (!pp) return E_POINTER;
    *pp = &s_vp3;
    InterlockedIncrement(&s_vp3.ref);
    return S_OK;
}
static HRESULT WINAPI NOINLINE nd_GetRenderState(NullObj *s, DWORD state, DWORD *value)
    { (void)s; (void)state; if (value) *value = 0; return S_OK; }
static HRESULT WINAPI NOINLINE nd_GetLightState(NullObj *s, DWORD state, DWORD *value)
    { (void)s; (void)state; if (value) *value = 0; return S_OK; }
static HRESULT WINAPI NOINLINE nd_GetTransform(NullObj *s, DWORD which, D3DMATRIX *m)
{
    (void)s; (void)which;
    if (m) { memset(m, 0, sizeof(*m)); m->_11 = m->_22 = m->_33 = m->_44 = 1.0f; }
    return S_OK;
}
static HRESULT WINAPI NOINLINE nd_GetTexture(NullObj *s, DWORD stage, void **tex)
    { (void)s; (void)stage; if (tex) *tex = NULL; return S_OK; }
static HRESULT WINAPI NOINLINE nd_GetTextureStageState(NullObj *s, DWORD stage, DWORD type, DWORD *v)
    { (void)s; (void)stage; (void)type; if (v) *v = 0; return S_OK; }
static HRESULT WINAPI NOINLINE nd_ValidateDevice(NullObj *s, DWORD *passes)
    { (void)s; if (passes) *passes = 1; return S_OK; }
static HRESULT WINAPI NOINLINE nd_GetStats(NullObj *s, void *stats)
    { (void)s; (void)stats; return S_OK; }
static HRESULT WINAPI NOINLINE nd_GetRenderTarget(NullObj *s, void **pp)
    { (void)s; if (pp) *pp = NULL; return S_OK; }
static HRESULT WINAPI NOINLINE nd_GetClipStatus(NullObj *s, void *cs)
    { (void)s; (void)cs; return S_OK; }
static HRESULT WINAPI NOINLINE nd_ComputeSphereVisibility(NullObj *s, void *c, void *r, DWORD n, DWORD f, DWORD *ret)
{
    (void)s; (void)c; (void)r; (void)f;
    if (ret) for (DWORD i = 0; i < n; i++) ret[i] = 0;   /* fully visible */
    return S_OK;
}
static HRESULT WINAPI NOINLINE nd_NextViewport(NullObj *s, void *ref, void **next, DWORD flags)
    { (void)s; (void)ref; (void)flags; if (next) *next = NULL; return S_OK; }

/* --- IDirect3D3 (12 slots) --- */

static HRESULT WINAPI NOINLINE n3_EnumDevices(NullObj *s, void *cb, void *ctx)
{
    (void)s; (void)cb; (void)ctx;
    ONCE(enumdev, "nullddraw: IDirect3D3::EnumDevices called — not implemented, "
                  "returning an empty enumeration\n");
    return S_OK;
}
static HRESULT WINAPI NOINLINE n3_CreateLight(NullObj *s, void **light, IUnknown *outer)
{
    (void)s; (void)outer;
    if (!light) return E_POINTER;
    s_light.ref++;
    *light = &s_light;
    return S_OK;
}
static HRESULT WINAPI NOINLINE n3_CreateMaterial(NullObj *s, void **mat, IUnknown *outer)
{
    (void)s; (void)outer;
    if (!mat) return E_POINTER;
    s_mat3.ref++;
    *mat = &s_mat3;
    return S_OK;
}
static HRESULT WINAPI NOINLINE n3_CreateViewport(NullObj *s, void **vp, IUnknown *outer)
{
    (void)s; (void)outer;
    if (!vp) return E_POINTER;
    s_vp3.ref++;
    *vp = &s_vp3;
    return S_OK;
}
static HRESULT WINAPI NOINLINE n3_FindDevice(NullObj *s, void *search, void *result)
{
    (void)s; (void)search;
    if (!result) return DDERR_INVALIDPARAMS;
    memcpy(result, s_finddev, sizeof(s_finddev));
    return S_OK;
}
static HRESULT WINAPI NOINLINE n3_CreateDevice(NullObj *s, REFCLSID clsid,
        NullSurface *surf, void **dev, IUnknown *outer)
{
    (void)s; (void)clsid; (void)surf; (void)outer;
    if (!dev) return E_POINTER;
    s_dev3.ref++;
    *dev = &s_dev3;
    return S_OK;
}
static HRESULT WINAPI NOINLINE n3_CreateVertexBuffer(NullObj *s, void *desc, void **buf, DWORD f, IUnknown *o)
{
    (void)s; (void)desc; (void)f; (void)o;
    if (buf) *buf = NULL;
    ONCE(cvb, "nullddraw: CreateVertexBuffer called — not implemented\n");
    return DDERR_UNSUPPORTED;
}
static HRESULT WINAPI NOINLINE n3_EnumZBufferFormats(NullObj *s, REFCLSID dev,
        LPD3DENUMPIXELFORMATSCALLBACK cb, void *ctx)
{
    (void)s; (void)dev;
    if (!cb) return DDERR_INVALIDPARAMS;
    for (unsigned i = 0; i < sizeof(s_zfmt) / sizeof(s_zfmt[0]); i++) {
        DDPIXELFORMAT pf;
        rec_to_pixfmt(&s_zfmt[i], &pf);
        if (cb(&pf, ctx) == D3DENUMRET_CANCEL)
            break;
    }
    return S_OK;
}

/* --- IDirect3DMaterial3 (6 slots) --- */

static HRESULT WINAPI NOINLINE nm_SetMaterial(NullMaterial *s, D3DMATERIAL *m)
    { if (m) s->mat = *m; return S_OK; }
static HRESULT WINAPI NOINLINE nm_GetMaterial(NullMaterial *s, D3DMATERIAL *m)
    { if (m) *m = s->mat; return S_OK; }
static HRESULT WINAPI NOINLINE nm_GetHandle(NullMaterial *s, void *dev, DWORD *h)
    { (void)dev; if (!h) return E_POINTER; *h = s->handle; return S_OK; }

/* --- IDirect3DLight (6 slots) --- */

static D3DLIGHT s_light_state;
static HRESULT WINAPI NOINLINE nl_SetLight(NullObj *s, D3DLIGHT *l)
    { (void)s; if (l) memcpy(&s_light_state, l, sizeof(s_light_state)); return S_OK; }
static HRESULT WINAPI NOINLINE nl_GetLight(NullObj *s, D3DLIGHT *l)
    { (void)s; if (l) memcpy(l, &s_light_state, sizeof(s_light_state)); return S_OK; }

/* --- IDirect3DViewport3 (21 slots) --- */

static D3DVIEWPORT2 s_vp_state;
static HRESULT WINAPI NOINLINE nv_GetViewport(NullObj *s, D3DVIEWPORT *vp)
    { (void)s; (void)vp; return S_OK; }
static HRESULT WINAPI NOINLINE nv_GetViewport2(NullObj *s, D3DVIEWPORT2 *vp)
    { (void)s; if (vp) *vp = s_vp_state; return S_OK; }
static HRESULT WINAPI NOINLINE nv_SetViewport2(NullObj *s, D3DVIEWPORT2 *vp)
    { (void)s; if (vp) s_vp_state = *vp; return S_OK; }
static HRESULT WINAPI NOINLINE nv_GetBackground(NullObj *s, DWORD *h, BOOL *valid)
    { (void)s; if (h) *h = 0; if (valid) *valid = FALSE; return S_OK; }
static HRESULT WINAPI NOINLINE nv_GetBackgroundDepth(NullObj *s, void **surf, BOOL *valid)
    { (void)s; if (surf) *surf = NULL; if (valid) *valid = FALSE; return S_OK; }
static HRESULT WINAPI NOINLINE nv_NextLight(NullObj *s, void *ref, void **next, DWORD f)
    { (void)s; (void)ref; (void)f; if (next) *next = NULL; return S_OK; }

/* ─── IDirectDraw4 (28 slots) ──────────────────────────────────────────── */

static NullObj s_dd4 = { NULL, 1 };
static NullObj s_dd1 = { NULL, 1 };
static void   *s_dd4_vtable[28];
static void   *s_dd1_vtable[23];

static HRESULT WINAPI NOINLINE ns_GetDDInterface(NullSurface *s, void **pp)
{
    (void)s;
    if (!pp) return E_POINTER;
    *pp = &s_dd4;
    InterlockedIncrement(&s_dd4.ref);
    return S_OK;
}

static HRESULT WINAPI NOINLINE n4_QueryInterface(NullObj *s, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    if (guid_eq(riid, &IID_IDirect3D3_g)) {
        *ppv = &s_d3d3;
        InterlockedIncrement(&s_d3d3.ref);
        return S_OK;
    }
    if (guid_eq(riid, &IID_IDirectDraw_g)) {
        *ppv = &s_dd1;
        InterlockedIncrement(&s_dd1.ref);
        return S_OK;
    }
    if (guid_eq(riid, &IID_IDirectDraw4_g)) {
        *ppv = &s_dd4;
        InterlockedIncrement(&s_dd4.ref);
        return S_OK;
    }
    *ppv = s;
    InterlockedIncrement(&s->ref);
    return S_OK;
}

static HRESULT WINAPI NOINLINE n4_CreateSurface(NullObj *s, DDSURFACEDESC2 *d,
                                                NullSurface **pp, IUnknown *outer)
{
    (void)s; (void)outer;
    if (!d || !pp) return DDERR_INVALIDPARAMS;
    *pp = NULL;

    NullSurface *surf = surf_alloc();
    if (!surf) return DDERR_OUTOFMEMORY;

    surf->desc.dwSize    = sizeof(DDSURFACEDESC2);
    surf->desc.dwFlags   = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    surf->desc.ddsCaps   = d->ddsCaps;
    surf->desc.dwWidth   = (d->dwFlags & DDSD_WIDTH)  ? d->dwWidth  : s_cur_w;
    surf->desc.dwHeight  = (d->dwFlags & DDSD_HEIGHT) ? d->dwHeight : s_cur_h;
    if (d->dwFlags & DDSD_TEXTURESTAGE) {
        surf->desc.dwTextureStage = d->dwTextureStage;
        surf->desc.dwFlags |= DDSD_TEXTURESTAGE;
    }

    if (d->dwFlags & DDSD_PIXELFORMAT)
        surf->desc.ddpfPixelFormat = d->ddpfPixelFormat;
    else
        mode_pixfmt(s_cur_bpp, &surf->desc.ddpfPixelFormat);

    /* Report the caps DirectDraw reports, not merely the ones asked for: a
     * texture requested as DDSCAPS_TEXTURE comes back TEXTURE|VIDEOMEMORY|
     * LOCALVIDMEM (0x10005000 in the capture) unless system memory was asked
     * for explicitly.  st_texture_caps and the game's own checks read these. */
    if (!(surf->desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY))
        surf->desc.ddsCaps.dwCaps |= DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM;

    if (!surf_alloc_bits(surf)) {
        surf->in_use = 0;
        return DDERR_OUTOFVIDEOMEMORY;
    }

    /* A complex flipping primary owns an attached back buffer, which is what
     * CreateD3DDevice then fetches with GetAttachedSurface(DDSCAPS_BACKBUFFER)
     * and uses as the render target. */
    if ((d->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) &&
        (d->dwFlags & DDSD_BACKBUFFERCOUNT) && d->dwBackBufferCount > 0) {
        NullSurface *back = surf_alloc();
        if (!back) { ns_Release(surf); return DDERR_OUTOFMEMORY; }
        back->desc = surf->desc;
        back->desc.ddsCaps.dwCaps =
            (surf->desc.ddsCaps.dwCaps & ~(DWORD)DDSCAPS_PRIMARYSURFACE)
            | DDSCAPS_BACKBUFFER;
        back->dib = NULL; back->bits = NULL;
        if (!surf_alloc_bits(back)) { ns_Release(back); ns_Release(surf); return DDERR_OUTOFVIDEOMEMORY; }
        surf->attached[surf->n_attached++] = back;   /* the primary's own ref */
        surf->desc.dwBackBufferCount = d->dwBackBufferCount;
        surf->desc.dwFlags |= DDSD_BACKBUFFERCOUNT;
    }

    *pp = surf;
    return S_OK;
}

static HRESULT WINAPI NOINLINE n4_CreatePalette(NullObj *s, DWORD flags,
        PALETTEENTRY *pe, void **pp, IUnknown *outer)
{
    (void)s; (void)flags; (void)outer;
    if (!pp) return E_POINTER;
    *pp = NULL;
    for (int i = 0; i < PAL_POOL_SIZE; i++) {
        if (!s_pal_pool[i].in_use) {
            NullPalette *p = &s_pal_pool[i];
            memset(p, 0, sizeof(*p));
            p->vtable = s_pal_vtable;
            p->ref    = 1;
            p->in_use = 1;
            if (pe) memcpy(p->entries, pe, sizeof(p->entries));
            *pp = p;
            return S_OK;
        }
    }
    log_write("nullddraw: palette pool EXHAUSTED\n");
    return DDERR_OUTOFMEMORY;
}

static HRESULT WINAPI NOINLINE n4_EnumDisplayModes(NullObj *s, DWORD flags,
        DDSURFACEDESC2 *filter, void *ctx, LPDDENUMMODESCALLBACK2 cb)
{
    (void)s; (void)flags; (void)filter;
    if (!cb) return DDERR_INVALIDPARAMS;
    for (unsigned di = 0; di < 3; di++) {
        for (unsigned mi = 0; mi < sizeof(s_modes) / sizeof(s_modes[0]); mi++) {
            DDSURFACEDESC2 d;
            memset(&d, 0, sizeof(d));
            d.dwSize        = sizeof(d);
            d.dwFlags       = 0x0004100E;   /* HEIGHT|WIDTH|PITCH|PIXELFORMAT|REFRESHRATE */
            d.dwWidth       = s_modes[mi].w;
            d.dwHeight      = s_modes[mi].h;
            d.dwRefreshRate = 0;
            mode_pixfmt(s_mode_depths[di], &d.ddpfPixelFormat);
            d.lPitch = (LONG)(s_modes[mi].w * s_mode_depths[di] / 8);
            if (cb(&d, ctx) == DDENUMRET_CANCEL)
                return S_OK;
        }
    }
    return S_OK;
}

static HRESULT WINAPI NOINLINE n4_GetDisplayMode(NullObj *s, DDSURFACEDESC2 *d)
{
    (void)s;
    if (!d) return DDERR_INVALIDPARAMS;
    DWORD size = d->dwSize ? d->dwSize : sizeof(DDSURFACEDESC2);
    memset(d, 0, size);
    d->dwSize   = size;
    d->dwFlags  = 0x0000100E;
    d->dwWidth  = s_cur_w;
    d->dwHeight = s_cur_h;
    d->lPitch   = (LONG)pitch_for(s_cur_w, s_cur_bpp);
    mode_pixfmt(s_cur_bpp, &d->ddpfPixelFormat);
    return S_OK;
}

static HRESULT WINAPI NOINLINE n4_SetDisplayMode(NullObj *s, DWORD w, DWORD h,
        DWORD bpp, DWORD refresh, DWORD flags)
{
    (void)s; (void)refresh; (void)flags;
    /* Records the mode; changes nothing.  This is the call that would resize
     * the desktop and steal focus on a real driver. */
    s_cur_w = w; s_cur_h = h; s_cur_bpp = bpp;
    log_write("nullddraw: SetDisplayMode %lux%lux%lu (recorded, not applied)\n",
              (unsigned long)w, (unsigned long)h, (unsigned long)bpp);
    return S_OK;
}

static HRESULT WINAPI NOINLINE n4_SetCooperativeLevel(NullObj *s, HWND hwnd, DWORD flags)
{
    (void)s;
    log_write("nullddraw: SetCooperativeLevel hwnd=%p flags=%08lX (ignored)\n",
              hwnd, (unsigned long)flags);
    return S_OK;
}

static HRESULT WINAPI NOINLINE n4_GetCaps(NullObj *s, DDCAPS *drv, DDCAPS *hel)
{
    (void)s;
    /* UNVERIFIED: the capture recorded no call to this, so there is nothing to
     * replay.  Zeroing keeps the caller's dwSize and returns success. */
    ONCE(ddcaps, "nullddraw: IDirectDraw4::GetCaps called — no recorded answer "
                 "to replay, returning zeroed caps\n");
    if (drv) { DWORD n = drv->dwSize; memset(drv, 0, n); drv->dwSize = n; }
    if (hel) { DWORD n = hel->dwSize; memset(hel, 0, n); hel->dwSize = n; }
    return S_OK;
}

static HRESULT WINAPI NOINLINE n4_GetAvailableVidMem(NullObj *s, DDSCAPS2 *caps,
        DWORD *total, DWORD *free_)
{
    (void)s; (void)caps;
    if (total) *total = 256u * 1024 * 1024;
    if (free_) *free_ = 256u * 1024 * 1024;
    return S_OK;
}
static HRESULT WINAPI NOINLINE n4_GetMonitorFrequency(NullObj *s, DWORD *p)
    { (void)s; if (p) *p = 60; return S_OK; }
static HRESULT WINAPI NOINLINE n4_GetScanLine(NullObj *s, DWORD *p)
    { (void)s; if (p) *p = 0; return S_OK; }
static HRESULT WINAPI NOINLINE n4_GetVerticalBlankStatus(NullObj *s, BOOL *p)
    { (void)s; if (p) *p = FALSE; return S_OK; }
static HRESULT WINAPI NOINLINE n4_WaitForVerticalBlank(NullObj *s, DWORD f, HANDLE e)
    { (void)s; (void)f; (void)e; return S_OK; }   /* never blocks: no refresh */
static HRESULT WINAPI NOINLINE n4_CreateClipper(NullObj *s, DWORD f, void **pp, IUnknown *o)
{
    (void)s; (void)f; (void)o;
    if (pp) *pp = NULL;
    ONCE(clipper, "nullddraw: CreateClipper called — not implemented\n");
    return DDERR_UNSUPPORTED;
}
static HRESULT WINAPI NOINLINE n4_GetGDISurface(NullObj *s, void **pp)
    { (void)s; if (pp) *pp = NULL; return DDERR_NOTFOUND; }
static HRESULT WINAPI NOINLINE n4_GetSurfaceFromDC(NullObj *s, HDC dc, void **pp)
    { (void)s; (void)dc; if (pp) *pp = NULL; return DDERR_NOTFOUND; }
static HRESULT WINAPI NOINLINE n4_GetFourCCCodes(NullObj *s, DWORD *n, DWORD *codes)
    { (void)s; (void)codes; if (n) *n = 0; return S_OK; }
static HRESULT WINAPI NOINLINE n4_GetDeviceIdentifier(NullObj *s, void *p, DWORD f)
    { (void)s; (void)f; if (p) memset(p, 0, sizeof(DDDEVICEIDENTIFIER)); return S_OK; }
static HRESULT WINAPI NOINLINE n4_DuplicateSurface(NullObj *s, void *src, void **pp)
{
    (void)s; (void)src;
    if (pp) *pp = NULL;
    ONCE(dupsurf, "nullddraw: DuplicateSurface called — not implemented\n");
    return DDERR_UNSUPPORTED;
}

/* IDirectDraw (v1): the game only ever uses it to QueryInterface up to
 * IDirectDraw4, which is what createdevice.cpp does immediately after
 * DirectDrawCreate.  Every other v1 slot shares the DD4 implementation where
 * the signatures agree, and is a no-op where they do not. */
static HRESULT WINAPI NOINLINE n1_QueryInterface(NullObj *s, REFIID riid, void **ppv)
    { return n4_QueryInterface(s, riid, ppv); }
static HRESULT WINAPI NOINLINE n1_SetDisplayMode(NullObj *s, DWORD w, DWORD h, DWORD bpp)
    { return n4_SetDisplayMode(s, w, h, bpp, 0, 0); }
/* Every other v1 slot is a failN of the right arity (see OK_STUB): the game
 * only ever uses v1 to reach DD4, but a wrongly-sized stub would corrupt the
 * stack if anything did call one. */

/* ─── Vtable assembly ───────────────────────────────────────────────────── */

static void nulldd_build_vtables(void)
{
    static bool done = false;
    if (done) return;
    done = true;

    void **v = s_surf_vtable;
    v[0]  = (void*)ns_QueryInterface;      v[1]  = (void*)ns_AddRef;
    v[2]  = (void*)ns_Release;             v[3]  = (void*)ns_AddAttachedSurface;
    v[4]  = (void*)ns_AddOverlayDirtyRect; v[5]  = (void*)ns_Blt;
    v[6]  = (void*)ns_BltBatch;            v[7]  = (void*)ns_BltFast;
    v[8]  = (void*)ns_DeleteAttachedSurface; v[9] = (void*)ns_EnumAttachedSurfaces;
    v[10] = (void*)ns_EnumOverlayZOrders;  v[11] = (void*)ns_Flip;
    v[12] = (void*)ns_GetAttachedSurface;  v[13] = (void*)ns_GetBltStatus;
    v[14] = (void*)ns_GetCaps;             v[15] = (void*)ns_GetClipper;
    v[16] = (void*)ns_GetColorKey;         v[17] = (void*)ns_GetDC;
    v[18] = (void*)ns_GetFlipStatus;       v[19] = (void*)ns_GetOverlayPosition;
    v[20] = (void*)ns_GetPalette;          v[21] = (void*)ns_GetPixelFormat;
    v[22] = (void*)ns_GetSurfaceDesc;      v[23] = (void*)ns_Initialize;
    v[24] = (void*)ns_IsLost;              v[25] = (void*)ns_Lock;
    v[26] = (void*)ns_ReleaseDC;           v[27] = (void*)ns_Restore;
    v[28] = (void*)ns_SetClipper;          v[29] = (void*)ns_SetColorKey;
    v[30] = (void*)ns_SetOverlayPosition;  v[31] = (void*)ns_SetPalette;
    v[32] = (void*)ns_Unlock;              v[33] = (void*)ns_UpdateOverlay;
    v[34] = (void*)ns_UpdateOverlayDisplay; v[35] = (void*)ns_UpdateOverlayZOrder;
    v[36] = (void*)ns_GetDDInterface;      v[37] = (void*)ns_PageLock;
    v[38] = (void*)ns_PageUnlock;          v[39] = (void*)ns_SetSurfaceDesc;
    v[40] = (void*)ns_SetPrivateData;      v[41] = (void*)ns_GetPrivateData;
    v[42] = (void*)ns_FreePrivateData;     v[43] = (void*)ns_GetUniquenessValue;
    v[44] = (void*)ns_ChangeUniquenessValue;

    v = s_tex_vtable;
    v[0] = (void*)nt_QueryInterface; v[1] = (void*)nt_AddRef;
    v[2] = (void*)nt_Release;        v[3] = (void*)nt_GetHandle;
    v[4] = (void*)nt_PaletteChanged; v[5] = (void*)nt_Load;

    v = s_pal_vtable;
    v[0] = (void*)np_QueryInterface; v[1] = (void*)np_AddRef;
    v[2] = (void*)np_Release;        v[3] = (void*)np_GetCaps;
    v[4] = (void*)np_GetEntries;     v[5] = (void*)np_Initialize;
    v[6] = (void*)np_SetEntries;

    /* IDirect3DDevice3, 42 slots.  Everything not named here is a draw or a
     * state setter and returns D3D_OK. */
    /* IDirect3DDevice3, 42 slots.  The okN on each filler line is the
     * argument count INCLUDING `this` — see the OK_STUB comment. */
    v = s_dev3_vtable;
    v[0]  = (void*)no_QueryInterface;          /* 3 */
    v[1]  = (void*)no_AddRef;                  /* 1 */
    v[2]  = (void*)no_Release;                 /* 1 */
    v[3]  = (void*)nd_GetCaps;                 /* 3 */
    v[4]  = (void*)nd_GetStats;                /* 2 */
    v[5]  = (void*)ok2;                        /* AddViewport */
    v[6]  = (void*)ok2;                        /* DeleteViewport */
    v[7]  = (void*)nd_NextViewport;            /* 4 */
    v[8]  = (void*)nd_EnumTextureFormats;      /* 3 */
    v[9]  = (void*)ok1;                        /* BeginScene */
    v[10] = (void*)ok1;                        /* EndScene */
    v[11] = (void*)nd_GetDirect3D;             /* 2 */
    v[12] = (void*)ok2;                        /* SetCurrentViewport */
    v[13] = (void*)nd_GetCurrentViewport;      /* 2 */
    v[14] = (void*)ok3;                        /* SetRenderTarget */
    v[15] = (void*)nd_GetRenderTarget;         /* 2 */
    v[16] = (void*)ok4;                        /* Begin */
    v[17] = (void*)ok6;                        /* BeginIndexed */
    v[18] = (void*)ok2;                        /* Vertex */
    v[19] = (void*)ok2;                        /* Index */
    v[20] = (void*)ok2;                        /* End */
    v[21] = (void*)nd_GetRenderState;          /* 3 */
    v[22] = (void*)ok3;                        /* SetRenderState */
    v[23] = (void*)nd_GetLightState;           /* 3 */
    v[24] = (void*)ok3;                        /* SetLightState */
    v[25] = (void*)ok3;                        /* SetTransform */
    v[26] = (void*)nd_GetTransform;            /* 3 */
    v[27] = (void*)ok3;                        /* MultiplyTransform */
    v[28] = (void*)ok6;                        /* DrawPrimitive */
    v[29] = (void*)ok8;                        /* DrawIndexedPrimitive */
    v[30] = (void*)ok2;                        /* SetClipStatus */
    v[31] = (void*)nd_GetClipStatus;           /* 2 */
    v[32] = (void*)ok6;                        /* DrawPrimitiveStrided */
    v[33] = (void*)ok8;                        /* DrawIndexedPrimitiveStrided */
    v[34] = (void*)ok6;                        /* DrawPrimitiveVB */
    v[35] = (void*)ok6;                        /* DrawIndexedPrimitiveVB */
    v[36] = (void*)nd_ComputeSphereVisibility; /* 6 */
    v[37] = (void*)nd_GetTexture;              /* 3 */
    v[38] = (void*)ok3;                        /* SetTexture */
    v[39] = (void*)nd_GetTextureStageState;    /* 4 */
    v[40] = (void*)ok4;                        /* SetTextureStageState */
    v[41] = (void*)nd_ValidateDevice;          /* 2 */

    v = s_d3d3_vtable;
    v[0]  = (void*)no_QueryInterface; v[1]  = (void*)no_AddRef;
    v[2]  = (void*)no_Release;        v[3]  = (void*)n3_EnumDevices;
    v[4]  = (void*)n3_CreateLight;    v[5]  = (void*)n3_CreateMaterial;
    v[6]  = (void*)n3_CreateViewport; v[7]  = (void*)n3_FindDevice;
    v[8]  = (void*)n3_CreateDevice;   v[9]  = (void*)n3_CreateVertexBuffer;
    v[10] = (void*)n3_EnumZBufferFormats;
    v[11] = (void*)ok1;               /* EvictManagedTextures */

    /* IDirect3DViewport3, 21 slots.  AddLight (slot 13) is what crashed the
     * first headless run when it was filled with a zero-argument stub. */
    v = s_vp3_vtable;
    v[0]  = (void*)no_QueryInterface;      /* 3 */
    v[1]  = (void*)no_AddRef;              /* 1 */
    v[2]  = (void*)no_Release;             /* 1 */
    v[3]  = (void*)ok2;                    /* Initialize */
    v[4]  = (void*)nv_GetViewport;         /* 2 */
    v[5]  = (void*)ok2;                    /* SetViewport */
    v[6]  = (void*)ok5;                    /* TransformVertices */
    v[7]  = (void*)ok3;                    /* LightElements */
    v[8]  = (void*)ok2;                    /* SetBackground */
    v[9]  = (void*)nv_GetBackground;       /* 3 */
    v[10] = (void*)ok2;                    /* SetBackgroundDepth */
    v[11] = (void*)nv_GetBackgroundDepth;  /* 3 */
    v[12] = (void*)ok4;                    /* Clear */
    v[13] = (void*)ok2;                    /* AddLight */
    v[14] = (void*)ok2;                    /* DeleteLight */
    v[15] = (void*)nv_NextLight;           /* 4 */
    v[16] = (void*)nv_GetViewport2;        /* 2 */
    v[17] = (void*)nv_SetViewport2;        /* 2 */
    v[18] = (void*)ok2;                    /* SetBackgroundDepth2 */
    v[19] = (void*)ok3;                    /* GetBackgroundDepth2 */
    v[20] = (void*)ok7;                    /* Clear2 */

    v = s_mat3_vtable;
    v[0] = (void*)no_QueryInterface; v[1] = (void*)no_AddRef;
    v[2] = (void*)no_Release;        v[3] = (void*)nm_SetMaterial;
    v[4] = (void*)nm_GetMaterial;    v[5] = (void*)nm_GetHandle;

    v = s_light_vtable;
    v[0] = (void*)no_QueryInterface; v[1] = (void*)no_AddRef;
    v[2] = (void*)no_Release;        v[3] = (void*)ok2;     /* Initialize */
    v[4] = (void*)nl_SetLight;       v[5] = (void*)nl_GetLight;

    v = s_dd4_vtable;
    v[0]  = (void*)n4_QueryInterface;   v[1]  = (void*)no_AddRef;
    v[2]  = (void*)no_Release;          v[3]  = (void*)ok1;     /* Compact */
    v[4]  = (void*)n4_CreateClipper;    v[5]  = (void*)n4_CreatePalette;
    v[6]  = (void*)n4_CreateSurface;    v[7]  = (void*)n4_DuplicateSurface;
    v[8]  = (void*)n4_EnumDisplayModes; v[9]  = (void*)ok5;     /* EnumSurfaces */
    v[10] = (void*)ok1;                 /* FlipToGDISurface */
    v[11] = (void*)n4_GetCaps;          v[12] = (void*)n4_GetDisplayMode;
    v[13] = (void*)n4_GetFourCCCodes;   v[14] = (void*)n4_GetGDISurface;
    v[15] = (void*)n4_GetMonitorFrequency; v[16] = (void*)n4_GetScanLine;
    v[17] = (void*)n4_GetVerticalBlankStatus; v[18] = (void*)ok2;   /* Initialize */
    v[19] = (void*)ok1;                 /* RestoreDisplayMode */
    v[20] = (void*)n4_SetCooperativeLevel; v[21] = (void*)n4_SetDisplayMode;
    v[22] = (void*)n4_WaitForVerticalBlank; v[23] = (void*)n4_GetAvailableVidMem;
    v[24] = (void*)n4_GetSurfaceFromDC; v[25] = (void*)ok1;   /* RestoreAllSurfaces */
    v[26] = (void*)ok1;                 /* TestCooperativeLevel */
    v[27] = (void*)n4_GetDeviceIdentifier;

    v = s_dd1_vtable;
    v[0]  = (void*)n1_QueryInterface;      /* 3 */
    v[1]  = (void*)no_AddRef;              /* 1 */
    v[2]  = (void*)no_Release;             /* 1 */
    v[3]  = (void*)fail1;                  /* Compact */
    v[4]  = (void*)fail4;                  /* CreateClipper */
    v[5]  = (void*)fail5;                  /* CreatePalette */
    v[6]  = (void*)fail4;                  /* CreateSurface */
    v[7]  = (void*)fail3;                  /* DuplicateSurface */
    v[8]  = (void*)fail5;                  /* EnumDisplayModes */
    v[9]  = (void*)fail5;                  /* EnumSurfaces */
    v[10] = (void*)fail1;                  /* FlipToGDISurface */
    v[11] = (void*)fail3;                  /* GetCaps */
    v[12] = (void*)fail2;                  /* GetDisplayMode */
    v[13] = (void*)fail3;                  /* GetFourCCCodes */
    v[14] = (void*)fail2;                  /* GetGDISurface */
    v[15] = (void*)fail2;                  /* GetMonitorFrequency */
    v[16] = (void*)fail2;                  /* GetScanLine */
    v[17] = (void*)fail2;                  /* GetVerticalBlankStatus */
    v[18] = (void*)fail2;                  /* Initialize */
    v[19] = (void*)fail1;                  /* RestoreDisplayMode */
    v[20] = (void*)n4_SetCooperativeLevel; /* 3 */
    v[21] = (void*)n1_SetDisplayMode;      /* 4 */
    v[22] = (void*)fail3;                  /* WaitForVerticalBlank */

    s_dd4.vtable   = s_dd4_vtable;
    s_dd1.vtable   = s_dd1_vtable;
    s_d3d3.vtable  = s_d3d3_vtable;
    s_dev3.vtable  = s_dev3_vtable;
    s_vp3.vtable   = s_vp3_vtable;
    s_light.vtable = s_light_vtable;
    s_mat3.vtable  = s_mat3_vtable;
}

/* ─── The window ───────────────────────────────────────────────────────────
 *
 * Replacing DirectDraw is necessary but NOT sufficient.  Wine cannot create a
 * top-level window without a display driver at all: with no X server,
 * user32 falls back to the null driver and `CreateWindowEx` fails outright —
 *
 *   err:winediag:nodrv_CreateWindow Application tried to create a window,
 *                                   but no driver could be loaded.
 *   err:winediag:nodrv_CreateWindow "The explorer process failed to start."
 *
 * — and the game dies in WinMain long before it reaches DirectDraw.  Under
 * Proton with DISPLAY unset, the two kinds of window give
 *
 *   message-only HWND=00050042 err=0        <- works
 *   top-level    HWND=00000000 err=0        <- fails
 *   CreateCompatibleDC=25410040             <- GDI works, so DIB surfaces do
 *
 * So the fix is to make the game's one window message-only.  A window
 * parented to HWND_MESSAGE never goes near the display driver: it has no
 * frame, no position and no visibility, but it has a valid HWND, a window
 * procedure, and a message queue — everything the game actually uses it for
 * once DirectDraw is not real.  The null device ignores the HWND it is
 * handed, so nothing else notices.
 *
 * WinMain's is the only window.  Its ShowWindow and UpdateWindow calls are
 * left alone: both are harmless no-ops on a message-only window.
 *
 * Outside headless mode this is a pure passthrough.
 */
  HWND WINAPI hooks_CreateWindowExA(
        DWORD exStyle, LPCSTR className, LPCSTR windowName, DWORD style,
        int x, int y, int w, int h, HWND parent, HMENU menu,
        HINSTANCE inst, LPVOID param)
{
    if (nulldd_enabled() && parent == NULL) {
        HWND hwnd = CreateWindowExA(0, className, windowName,
                                    style & ~(DWORD)WS_VISIBLE,
                                    x, y, w, h, HWND_MESSAGE, menu, inst, param);
        log_write("nullddraw: CreateWindowExA class=%s -> message-only hwnd=%p "
                  "(style %08lX -> %08lX, exstyle %08lX dropped)\n",
                  className ? className : "(atom)", hwnd,
                  (unsigned long)style,
                  (unsigned long)(style & ~(DWORD)WS_VISIBLE),
                  (unsigned long)exStyle);
        return hwnd;
    }
    return CreateWindowExA(exStyle, className, windowName, style,
                           x, y, w, h, parent, menu, inst, param);
}

IDirectDraw *nulldd_create(void)
{
    nulldd_build_vtables();
    log_write("nullddraw: null DirectDraw installed — no display is touched "
              "(%u modes, %u texture formats, %u z formats)\n",
              (unsigned)(sizeof(s_modes) / sizeof(s_modes[0]) * 3),
              (unsigned)(sizeof(s_texfmt) / sizeof(s_texfmt[0])),
              (unsigned)(sizeof(s_zfmt) / sizeof(s_zfmt[0])));
    return (IDirectDraw *)&s_dd1;
}
