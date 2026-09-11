/* SceneTexture loader entry points — the last of Band 1's SceneTexture work.
 *
 *   0x43fc70  SceneTexture::BindTextureResource   __thiscall, ret 0x14 (5 args)
 *             — 2 E8 sites (0x43FF0C, 0x00440040), both inside
 *               SelectTextureLoader; no E9/PUSH/DATA refs (xref.py)
 *   0x43f770  SceneTexture::ImportSceneTextures   __thiscall, ret 0x18 (6 args)
 *             — 15 E8 sites, all-CALL refs; no E9/PUSH/DATA refs
 *   0x43feb0  SceneTexture::SelectTextureLoader   __thiscall, ret 0x14 (5 args)
 *             — 6 E8 sites (0x43CB40..0x43CBE3), all-CALL refs
 *
 * Together these are the front door of the texture system: SelectTextureLoader
 * picks a loader by file extension (or by an explicit mode), BindTextureResource
 * is the BMP/DIB path and ImportSceneTextures the TGA path.  Both build the
 * DirectDraw texture surface and then hand the pixels to an already-replaced
 * loader — TextureDIB_BlitToSurface and TextureTGA_Parse respectively.
 *
 * ─── Signatures, from `ret N` and the frame, not from Ghidra ───────────────
 *
 * Ghidra's parameter lists happen to be in the right ORDER here, but the types
 * are only recoverable from use, and BindTextureResource's frame is shifted:
 * it saves ebx/ebp before ecx is read and esi/edi after, so Ghidra's fixed
 * frame base is off by 8 or 0x10 over most of the body and it reports both
 * `param_2` and a phantom `piStack_8` for the same argument.  Laid out against
 * esp the frame is unambiguous, and it tiles exactly:
 *
 *   BindTextureResource   sub esp,0x28c + 4 pushes; base B = entry-0x29c
 *     B+0x10  BITMAP (0x18)          B+0xa4  D3DDEVICEDESC hw  (0xfc)
 *     B+0x28  DDSURFACEDESC2 (0x7c)  B+0x1a0 D3DDEVICEDESC sw  (0xfc)
 *   0x10 + 0x18 == 0x28, 0x28 + 0x7c == 0xa4, and 0x1a0 + 0xfc == 0x29c.
 *
 *   ImportSceneTextures   sub esp,0x39c + 4 pushes + SEH; base B = entry-0x3b8
 *     B+0x10  FileWrapper (8)        B+0xb4  char msg[256]
 *     B+0x1c  TGA header (18)        B+0x1b4 D3DDEVICEDESC hw  (0xfc)
 *     B+0x30  double ln(2) temp      B+0x2b0 D3DDEVICEDESC sw  (0xfc)
 *     B+0x38  DDSURFACEDESC2 (0x7c)  B+0x3ac SEH record (12)
 *   0x38 + 0x7c == 0xb4, 0xb4 + 0x100 == 0x1b4, 0x2b0 + 0xfc == 0x3ac,
 *   0x3ac + 0xc == 0x3b8.  Nothing overlaps and nothing is left over.
 *
 * The one slot that genuinely overlaps is B+0x18 — the FileWrapper's FILE *,
 * reused as the FILD scratch for the size logging once the file is closed.
 * texturetga.cpp records the same idiom in ParseTGAFile.
 *
 *   BindTextureResource(this, IDirectDraw4 *dd, IDirect3DDevice3 *dev,
 *                       LPCSTR name, UINT bpp, DWORD textureStage)
 *   ImportSceneTextures(this, IDirectDraw4 *dd, IDirect3DDevice3 *dev,
 *                       LPCSTR name, DWORD alphaFlag, UINT bpp,
 *                       DWORD textureStage)
 *   SelectTextureLoader(this, IDirectDraw4 *dd, IDirect3DDevice3 *dev,
 *                       LPCSTR name, UINT bpp, int mode)
 *
 * SelectTextureLoader always passes textureStage 0 and alphaFlag 0, so those
 * two arguments are dead in every shipped call — but they are real stack slots
 * and reach the surface descriptor, so they are carried through rather than
 * dropped.
 *
 * ─── Two helpers that are NOT __thiscall, despite their call sites ─────────
 *
 * Both call sites do `mov ecx,esi` (this) immediately before calling
 * 0x43f720 and 0x43ea50, which reads as a member call.  Neither function ever
 * reads ecx: 0x43f720 overwrites it at its second instruction and 0x43ea50 at
 * its fourth.  Both are `ret N` with every operand taken from the stack, i.e.
 * __stdcall.  Declaring them __thiscall here would pass `this` in ecx for no
 * reason; __stdcall is what the callee actually implements.  (Checked, because
 * a stray `mov ecx` is exactly what makes a plain function look like a method.)
 *
 *   0x43f720  __stdcall(IDirect3DDevice3 *dev, DWORD bpp, DWORD alphaFlag,
 *                       DDPIXELFORMAT *out)
 *             Builds a 0x25-byte context { DWORD bpp; BYTE alphaFlag;
 *             DDPIXELFORMAT result; } — deliberately unaligned — runs
 *             IDirect3DDevice3::EnumTextureFormats with the picker at
 *             0x43f590, then copies the chosen format's 0x20 bytes to *out.
 *   0x43ea50  __stdcall(IDirectDraw4 *dd, HBITMAP hbmp) -> IDirectDrawPalette *
 *             GetDIBColorTable + IDirectDraw4::CreatePalette.
 *
 * Both stay live in the binary and are called through; neither makes a draw
 * call, and the format picker in particular is a scoring loop with no bearing
 * on the two functions replaced here.
 *
 * ─── Preserved deliberately ───────────────────────────────────────────────
 *
 *  1. The texture memory pool is chosen from the HARDWARE device description's
 *     dcmColorModel, not from any capability flag:
 *         GetCaps(&hw, &sw);
 *         caps = hw.dcmColorModel ? DDSCAPS_TEXTURE
 *                                 : DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY;
 *     Both functions do this identically (`neg eax; sbb eax,eax; and
 *     eax,0xfffff800; add eax,0x1800`).  The software description is fetched
 *     and then never read at all — it exists only because GetCaps requires
 *     both pointers.
 *
 *  2. The bit-depth argument is a REQUEST, honoured only if it is exactly 16
 *     or 32.  Anything else — including a plausible 24 — is discarded and the
 *     source image's own depth is used instead (BITMAP.bmBitsPixel for a DIB,
 *     the TGA header's byte for a TGA).
 *
 *  3. The name copy is `operator new(strlen + 1)` + sprintf("%s"), not a
 *     strdup, and the length is the original's `not ecx` with no `dec ecx`.
 *     The log calls in the same functions use `not ecx; dec ecx`, i.e. the
 *     true length.  Both idioms as written.
 *
 *  4. ImportSceneTextures' "x-Size:%d(%g) ...ok" line computes the %g value as
 *         pow(2.0, log(w)/log(2.0))
 *     which is just w again, to within rounding.  It reads like the remains of
 *     a power-of-two check whose ceil() was lost; there is no rounding step in
 *     the instruction stream (fldln2 / fyl2x / fdiv / _CIpow and nothing else).
 *     Reproduced as the identity it is, rather than "fixed" into a check.
 *
 *  5. The failure paths return whatever the last call left in the upper three
 *     bytes of eax, as in texturedib.cpp; each return below names its source.
 *     ImportSceneTextures' upper bytes always come from FileWrapper::Close.
 *
 *  6. SelectTextureLoader dereferences strrchr's result without a NULL check.
 *     A name with no '.' faults.  Left as is — every caller passes a real file
 *     name, and the fault would be the original's fault too.
 *
 *  7. Only imageType 2 (uncompressed true-colour) and 0x0a (RLE true-colour)
 *     are accepted by ImportSceneTextures; every other TGA type is rejected
 *     silently, before ParseTGAFile — which itself would happily read types
 *     1/3/9/11 as one uncompressed block.  The gate is here, not there.
 *
 *  8. ImportSceneTextures rejects the file only AFTER calling
 *     ReleaseD3DTexture, so a bad TGA destroys the texture that was there.
 *
 *  9. The DIB path tries the exe's RESOURCES first (LR_CREATEDIBSECTION with
 *     hInst = the module, no LR_LOADFROMFILE) and only then the file system
 *     (hInst = NULL, LR_LOADFROMFILE | LR_CREATEDIBSECTION).  The game ships
 *     no bitmap resources, so the first call always fails and the second is
 *     what actually loads — but the flags are as written, because getting them
 *     wrong silently changes which call succeeds and whether the result is a
 *     DIB section or a device-dependent bitmap.
 *
 * ─── Deliberate departure: the SEH frame ──────────────────────────────────
 *
 * ImportSceneTextures installs an SEH record whose only job is to run
 * FileWrapper::Close if something below it throws.  Nothing below it can:
 * ParseTGAFile is ours and is nothrow, and the rest is COM and CRT calls.
 * The replacement therefore closes the file explicitly on every path — which
 * is what the original's normal paths do anyway — and installs no handler.
 * The observable difference is confined to a throw that cannot happen.
 */
#include <string.h>
#include <stdio.h>
#include "texture.h"
#include "log.h"
#include "alloc.h"

/* ─── Originals left live in the binary ──────────────────────────────────── */

/* THE FILE IS OURS (2026-09-05).  This reader used to open through the game's
 * FileWrapper and read with the game's fread, on the grounds that the FILE *
 * belonged to the game's CRT.  True, but it was the wrong thing to hold fixed:
 * the file is opened HERE, and whoever opens a file decides which CRT owns it.
 * The FileWrapper is a purely local { vtable, FILE * } on the stack that
 * nothing outside this function sees, so a plain FILE * from our own CRT is
 * behaviourally identical -- see the method notes in texturetga.cpp.
 *
 * The original's failure paths return FileWrapper::Close's result with AL
 * forced to 0, so the upper three bytes of EAX carry Close's value; we return
 * a plain 0.  Callers test AL only. */

typedef int   (__cdecl *sprintf_fn)(char *, const char *, ...);
typedef char *(__cdecl *strrchr_fn)(const char *, int);
#define ORIG_MAYBE_SPRINTF ((sprintf_fn)0x00450655)
#define ORIG_STRRCHR       ((strrchr_fn)0x00451a50)
#define FMT_PERCENT_S      ((const char *)0x004641f8)

/* ImageLogger::Log — __cdecl(const char *, int len, int, int *sink). */
typedef unsigned int (__cdecl *fwrite_fn)(const char *, int, int, int *);
#define ORIG_FWRITE ((fwrite_fn)0x004513c7)
#define GAME_LOG_FILE ((int *)0x00469cf8)

/* See the header note: __stdcall, not __thiscall. */
typedef void (__stdcall *enumfmt_fn)(IDirect3DDevice3 *, DWORD, DWORD, DDPIXELFORMAT *);
typedef IDirectDrawPalette *(__stdcall *dibpal_fn)(IDirectDraw4 *, HBITMAP);
#define ORIG_PICK_TEXTURE_FORMAT    ((enumfmt_fn)0x0043f720)
#define ORIG_CREATE_PALETTE_FROMDIB ((dibpal_fn)0x0043ea50)

/* Strings at their original addresses, so the pointer handed to the logger is
 * identical to the original's. */
#define STR_NEWLINE            ((const char *)0x00465160)
#define STR_TYPE_OK            ((const char *)0x00467280)  /* "Type ok\n" */
#define STR_FMT_X_SIZE         ((const char *)0x00467268)  /* "x-Size:%d(%g) ...ok\n" */
#define STR_FMT_Y_SIZE         ((const char *)0x00467250)  /* "y-Size:%d(%g) ...ok\n" */
#define STR_NO_TEXTURE_SURFACE ((const char *)0x0046722c)
#define STR_NO_TGA_COPY        ((const char *)0x00467218)
#define STR_NO_TEXTURE_IFACE   ((const char *)0x00467200)
#define STR_DOT_TGA_UPPER      ((const char *)0x0046728c)  /* ".TGA" */
#define STR_DOT_TGA_LOWER      ((const char *)0x00467294)  /* ".tga" */
#define STR_DOT_BMP_UPPER      ((const char *)0x0046729c)  /* ".BMP" */
#define STR_DOT_BMP_LOWER      ((const char *)0x004672a4)  /* ".bmp" */

/* Already-replaced neighbours; their originals are UD2-stubbed. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Texture_ReleaseD3DTexture(SceneTexture *self);
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
TextureDIB_BlitToSurface(LoadedImage *self, HANDLE hbmp);
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
TextureTGA_Parse(LoadedImage *self, LPCSTR path);

/* D3DDEVICEDESC as the original treats it: 0x3f dwords, zeroed, dwSize at [0].
 * Only dcmColorModel at +8 is ever read.  Modelled as raw dwords rather than
 * as the SDK struct so the 0xfc the game writes is the size that is used, not
 * whatever this mingw's d3d.h happens to define. */
struct DevDescRaw { DWORD dw[0x3f]; };
static_assert(sizeof(DevDescRaw) == 0xfc, "D3DDEVICEDESC must be 0xfc bytes");
#define DEVDESC_COLORMODEL 2

/* The 18-byte TGA header, in the original's field order and sizes.  Duplicated
 * from texturetga.cpp rather than shared: both are reconstructions of the same
 * on-disk layout, and keeping each file self-contained leaves the already
 * byte-verified TGA loader untouched. */
struct TgaHeader {
    unsigned char  idLength;
    unsigned char  colourMapType;
    unsigned char  imageType;
    unsigned short colourMapOrigin;
    unsigned short colourMapLength;
    unsigned char  colourMapDepth;
    unsigned short xOrigin;
    unsigned short yOrigin;
    unsigned short width;
    unsigned short height;
    unsigned char  bpp;
    unsigned char  descriptor;
} __attribute__((packed));
static_assert(sizeof(TgaHeader) == 18, "TGA header must be 18 bytes");

/* The inline REPNE SCASB the originals use.  st_strlen is the true length
 * (`not ecx; dec ecx`), used by the log calls; the name copies allocate
 * st_strlen + 1, which is the originals' bare `not ecx`. */
static unsigned int st_strlen(const char *s)
{
    const char *p = s;
    while (*p != '\0')
        ++p;
    return (unsigned int)(p - s);
}

static void st_log_str(const char *s)
{
    ORIG_FWRITE(s, (int)st_strlen(s), 1, GAME_LOG_FILE);
}

/* The game's inlined strcmp: 0 when equal, otherwise -1 or 1 from the
 * `sbb ecx,ecx; sbb ecx,-1` pair on the first differing byte. */
static int st_strcmp(const unsigned char *a, const unsigned char *b)
{
    for (;;) {
        if (*a != *b)
            return (*a < *b) ? -1 : 1;
        if (*a == 0)
            return 0;
        ++a; ++b;
    }
}

/* Replace the image name: free the old, allocate strlen+1, sprintf("%s"). */
static void st_set_image_name(LoadedImage *self, LPCSTR name)
{
    if (self->ImageName != NULL)
        game_free2(self->ImageName);
    char *copy = (char *)game_operator_new(st_strlen(name) + 1u);
    self->ImageName = copy;
    ORIG_MAYBE_SPRINTF(copy, FMT_PERCENT_S, name);
}

/* DDSCAPS for the texture surface, from the hardware device description.
 * See preserved note 1. */
static DWORD st_texture_caps(const DevDescRaw *hw)
{
    return hw->dw[DEVDESC_COLORMODEL] != 0
             ? (DWORD)(DDSCAPS_TEXTURE)                          /* 0x1000 */
             : (DWORD)(DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY);  /* 0x1800 */
}

/* pow(2.0, log(n)/log(2.0)) exactly as the original computes it: ln(2) is
 * rounded to a double temporary (`fstp qword`) and the numerator is not, so
 * the division runs at extended precision against a double divisor.  See
 * note 4 — the result is n, and it only ever reaches a log line. */
static double st_size_report_value(unsigned int n)
{
    double      ln2 = (double)__builtin_logl(2.0L);   /* fld 2.0; fldln2; fyl2x; fstp qword */
    long double e   = __builtin_logl((long double)(int)n) / (long double)ln2;
    return (double)__builtin_powl(2.0L, e);
}

/* ─── KAROO_TEXTURE_FX — visual proof that these replacements run ──────────
 *
 * "bpp16" forces the resolved bit depth to 16 just before
 * PickTextureFormatForDepth, in BOTH loaders.  Every texture in the game then
 * loads into a 16-bit surface instead of the 32-bit one the source asks for,
 * which is unmistakable on screen: banding on the sky gradient and on every
 * smooth-shaded texture.
 *
 * This is a change only this file can produce.  The depth-request rule (note 2
 * — honour the argument only if it is exactly 16 or 32, else take the source's
 * own depth) lives here and nowhere else; neither the DIB blitter nor the TGA
 * decoder chooses a surface format.  So banding proves that the code deciding
 * the format is ours, not that some texture somewhere changed.
 *
 * Read by VALUE, never by presence — under launch.sh an unset KAROO_* flag can
 * still arrive, and `getenv(...) != NULL` is not a test for anything.  See
 * RENDER_PLAN.md, 2026-09-02, where that trap silently turned the TGA
 * acceptance test into a no-op for three runs. */
/* "solid" is the louder mode: once the decoder has filled the texture surface,
 * lock it and overwrite every pixel with flat magenta.  Unlike bpp16 it does
 * not depend on the format picker honouring anything — it writes the surface
 * this function created, through the pointer this function owns, so if the
 * textures come out flat magenta then this code ran, full stop. */
enum TextureFx { TEXFX_OFF = 0, TEXFX_BPP16, TEXFX_SOLID };

static int texture_fx_mode(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[16];
        cached = TEXFX_OFF;
        if (GetEnvironmentVariableA("KAROO_TEXTURE_FX", buf, sizeof(buf))) {
            if (lstrcmpiA(buf, "bpp16") == 0)
                cached = TEXFX_BPP16;
            else if (lstrcmpiA(buf, "solid") == 0)
                cached = TEXFX_SOLID;
        }
        log_write("scenetexture: FX mode = %s\n",
                  cached == TEXFX_BPP16 ? "bpp16" :
                  cached == TEXFX_SOLID ? "solid" : "off");
    }
    return cached;
}

static bool texture_fx_bpp16(void) { return texture_fx_mode() == TEXFX_BPP16; }

/* What the picker actually chose.  bpp16 looking identical on screen has two
 * possible causes — this code not reaching the choice, or the picker returning
 * the same format whatever it is asked for — and they are told apart by
 * reading the format rather than by looking at pixels. */
static void texture_log_format(const char *who, UINT requested,
                               const DDPIXELFORMAT *pf)
{
    static LONG seen = 0;
    if (InterlockedIncrement(&seen) <= 8)
        log_write("scenetexture: %s req=%u -> chosen %lubpp flags=%08lX "
                  "r=%08lX g=%08lX b=%08lX a=%08lX\n",
                  who, requested, (unsigned long)pf->dwRGBBitCount,
                  (unsigned long)pf->dwFlags,
                  (unsigned long)pf->dwRBitMask, (unsigned long)pf->dwGBitMask,
                  (unsigned long)pf->dwBBitMask,
                  (unsigned long)pf->dwRGBAlphaBitMask);
}

/* Flat-fill the texture surface, for TEXFX_SOLID.  Writes through the
 * destination's own pitch and bit count, so it works for any format the picker
 * chose; anything other than 16 or 32 bits per pixel is left alone rather than
 * guessed at. */
static void texture_fx_fill_solid(IDirectDrawSurface4 *surf)
{
    DDSURFACEDESC2 d;
    memset(&d, 0, sizeof(d));
    d.dwSize = sizeof(d);
    HRESULT hr = surf->Lock(NULL, &d, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, NULL);

    /* Never fail silently: an unlockable surface would look exactly like "the
     * FX did nothing", which is the failure mode that made bpp16 ambiguous. */
    static LONG seen = 0;
    if (InterlockedIncrement(&seen) <= 4)
        log_write("scenetexture: solid fill lock=%08lX %lux%lu %lubpp pitch=%ld\n",
                  (unsigned long)hr, (unsigned long)d.dwWidth,
                  (unsigned long)d.dwHeight,
                  (unsigned long)d.ddpfPixelFormat.dwRGBBitCount,
                  (long)d.lPitch);
    if (hr < 0)
        return;

    BYTE *row = (BYTE *)d.lpSurface;
    for (DWORD y = 0; y < d.dwHeight; ++y, row += d.lPitch) {
        if (d.ddpfPixelFormat.dwRGBBitCount == 32) {
            DWORD *p = (DWORD *)row;
            for (DWORD x = 0; x < d.dwWidth; ++x)
                p[x] = 0xFFFF00FFu;          /* opaque magenta */
        } else if (d.ddpfPixelFormat.dwRGBBitCount == 16) {
            WORD *p = (WORD *)row;
            for (DWORD x = 0; x < d.dwWidth; ++x)
                p[x] = (WORD)(d.ddpfPixelFormat.dwRBitMask |
                              d.ddpfPixelFormat.dwBBitMask |
                              d.ddpfPixelFormat.dwRGBAlphaBitMask);
        }
    }
    surf->Unlock(NULL);
}

extern "C" {

/* ─── SceneTexture::BindTextureResource (0x43fc70) ─────────────────────────
 *
 * The BMP/DIB path.  LoadImageA the file, create a texture surface matching
 * its dimensions and the device's chosen pixel format, attach a palette if the
 * format is 8-bit or less, blit the DIB in and query the IDirect3DTexture2.
 */
__declspec(dllexport) unsigned int __attribute__((thiscall))
Texture_BindTextureResource(SceneTexture *self, IDirectDraw4 *dd,
                            IDirect3DDevice3 *dev, LPCSTR name, UINT bpp,
                            DWORD textureStage)
{
    /* Note 9: resources first (0x2000), then the file system (0x2010). */
    HANDLE hbmp = LoadImageA(GetModuleHandleA(NULL), name, IMAGE_BITMAP,
                             0, 0, LR_CREATEDIBSECTION);
    if (hbmp == NULL) {
        hbmp = LoadImageA(NULL, name, IMAGE_BITMAP, 0, 0,
                          LR_LOADFROMFILE | LR_CREATEDIBSECTION);
        if (hbmp == NULL)
            return 0;               /* eax is exactly 0 (LoadImageA's NULL) */
    }

    static LONG seen_bind = 0;
    if (InterlockedIncrement(&seen_bind) <= 4)
        log_write("scenetexture: Bind this=%p dd=%p dev=%p bpp=%u stage=%lu name=%s\n",
                  self, dd, dev, bpp, (unsigned long)textureStage,
                  name ? name : "(null)");

    Texture_ReleaseD3DTexture(self);

    BITMAP bm;
    GetObjectA(hbmp, sizeof(BITMAP), &bm);

    DDSURFACEDESC2 ddsd;
    memset(&ddsd, 0, sizeof(ddsd));         /* rep stosd, 0x1f dwords */
    ddsd.dwWidth        = (DWORD)bm.bmWidth;
    ddsd.dwTextureStage = textureStage;
    ddsd.dwSize         = sizeof(DDSURFACEDESC2);
    ddsd.dwFlags        = 0x101007;         /* CAPS|HEIGHT|WIDTH|PIXELFORMAT|TEXTURESTAGE */
    ddsd.dwHeight       = (DWORD)bm.bmHeight;

    st_log_str(name);

    /* Note 2: only an exact 16 or 32 is honoured. */
    if (bpp != 16 && bpp != 32)
        bpp = (UINT)((DWORD)bm.bmBitsPixel & 0xffff);

    if (texture_fx_bpp16())
        bpp = 16;

    ORIG_PICK_TEXTURE_FORMAT(dev, bpp, 0, &ddsd.ddpfPixelFormat);
    texture_log_format("Bind", bpp, &ddsd.ddpfPixelFormat);

    DevDescRaw hw, sw;
    memset(&hw, 0, sizeof(hw));
    memset(&sw, 0, sizeof(sw));
    hw.dw[0] = 0xfc;
    sw.dw[0] = 0xfc;
    dev->GetCaps((LPD3DDEVICEDESC)&hw, (LPD3DDEVICEDESC)&sw);

    ddsd.ddsCaps.dwCaps = st_texture_caps(&hw);

    HRESULT hr = dd->CreateSurface(&ddsd, &self->base.pTextureSurface, NULL);
    if (hr < 0) {
        unsigned int d = (unsigned int)DeleteObject((HGDIOBJ)hbmp);
        return d & 0xffffff00u;             /* upper bytes: DeleteObject */
    }

    /* A palettised format gets a palette built from the DIB's colour table.
     * pTexturePalette is typed IDirectDrawSurface4 * in the struct — a
     * misnaming inherited from HOOKS.md; it holds an IDirectDrawPalette *. */
    if (ddsd.ddpfPixelFormat.dwRGBBitCount <= 8) {
        IDirectDrawPalette *pal = ORIG_CREATE_PALETTE_FROMDIB(dd, (HBITMAP)hbmp);
        self->base.pTexturePalette = (IDirectDrawSurface4 *)pal;
        if (pal != NULL)
            self->base.pTextureSurface->SetPalette(pal);
    }

    if ((TextureDIB_BlitToSurface(&self->base, hbmp) & 0xff) != 0) {
        if (texture_fx_mode() == TEXFX_SOLID)
            texture_fx_fill_solid(self->base.pTextureSurface);
        hr = self->base.pTextureSurface->QueryInterface(IID_IDirect3DTexture2,
                                                        (void **)&self->pTexture2);
        if (hr >= 0) {
            st_set_image_name(&self->base, name);
            self->base.loadedState = 1;
            unsigned int last = (unsigned int)DeleteObject((HGDIOBJ)hbmp);
            return (last & 0xffffff00u) | 1u;   /* upper bytes: DeleteObject */
        }
    }

    DeleteObject((HGDIOBJ)hbmp);
    Texture_ReleaseD3DTexture(self);
    /* upper bytes: ReleaseD3DTexture, which returns void — the original reads
     * back whatever it happened to leave in eax.  al is zero either way. */
    return 0;
}

/* ─── SceneTexture::ImportSceneTextures (0x43f770) ─────────────────────────
 *
 * The TGA path.  Reads the 18-byte header itself — twelve separate freads, the
 * same sequence ParseTGAFile repeats moments later — to get the dimensions and
 * to reject anything that is not a true-colour image, then builds the surface
 * and hands the file to ParseTGAFile to decode.
 */
__declspec(dllexport) unsigned int __attribute__((thiscall))
Texture_ImportSceneTextures(SceneTexture *self, IDirectDraw4 *dd,
                            IDirect3DDevice3 *dev, LPCSTR name,
                            DWORD alphaFlag, UINT bpp, DWORD textureStage)
{
    st_log_str(name);
    st_log_str(STR_NEWLINE);

    FILE *fp = fopen(name, "rb");
    if (fp == NULL)
        return 0;

    /* Header: twelve reads, no error checking whatsoever. */
    TgaHeader h;
    fread(&h.idLength,        1, 1, fp);
    fread(&h.colourMapType,   1, 1, fp);
    fread(&h.imageType,       1, 1, fp);
    fread(&h.colourMapOrigin, 2, 1, fp);
    fread(&h.colourMapLength, 2, 1, fp);
    fread(&h.colourMapDepth,  1, 1, fp);
    fread(&h.xOrigin,         2, 1, fp);
    fread(&h.yOrigin,         2, 1, fp);
    fread(&h.width,           2, 1, fp);
    fread(&h.height,          2, 1, fp);
    fread(&h.bpp,             1, 1, fp);
    fread(&h.descriptor,      1, 1, fp);
    fclose(fp);
    fp = NULL;

    static LONG seen_import = 0;
    if (InterlockedIncrement(&seen_import) <= 4)
        log_write("scenetexture: Import this=%p type=%u %ux%u src=%ubpp req=%u name=%s\n",
                  self, (unsigned)h.imageType, (unsigned)h.width,
                  (unsigned)h.height, (unsigned)h.bpp, bpp,
                  name ? name : "(null)");

    /* Note 8: the texture is destroyed before the type is even checked. */
    Texture_ReleaseD3DTexture(self);

    /* Note 7: true-colour only, compressed or not. */
    if (h.imageType != 2 && h.imageType != 0x0a) {
        if (fp != NULL) fclose(fp);
        return 0;
    }

    st_log_str(STR_TYPE_OK);

    char msg[256];
    unsigned int w = h.width;
    ORIG_MAYBE_SPRINTF(msg, STR_FMT_X_SIZE, w, st_size_report_value(w));
    st_log_str(msg);
    unsigned int ht = h.height;
    ORIG_MAYBE_SPRINTF(msg, STR_FMT_Y_SIZE, ht, st_size_report_value(ht));
    st_log_str(msg);

    DDSURFACEDESC2 ddsd;
    memset(&ddsd, 0, sizeof(ddsd));
    ddsd.dwSize         = sizeof(DDSURFACEDESC2);
    ddsd.dwFlags        = 0x101007;
    ddsd.dwTextureStage = textureStage;
    ddsd.dwWidth        = w;
    ddsd.dwHeight       = ht;

    if (bpp != 16 && bpp != 32)
        bpp = (UINT)h.bpp;

    if (texture_fx_bpp16())
        bpp = 16;

    ORIG_PICK_TEXTURE_FORMAT(dev, bpp, alphaFlag, &ddsd.ddpfPixelFormat);
    texture_log_format("Import", bpp, &ddsd.ddpfPixelFormat);

    DevDescRaw hw, sw;
    memset(&hw, 0, sizeof(hw));
    memset(&sw, 0, sizeof(sw));
    hw.dw[0] = 0xfc;
    sw.dw[0] = 0xfc;
    dev->GetCaps((LPD3DDEVICEDESC)&hw, (LPD3DDEVICEDESC)&sw);

    ddsd.ddsCaps.dwCaps = st_texture_caps(&hw);

    HRESULT hr = dd->CreateSurface(&ddsd, &self->base.pTextureSurface, NULL);
    if (hr < 0) {
        st_log_str(STR_NO_TEXTURE_SURFACE);
        if (fp != NULL) fclose(fp);
        return 0;
    }

    if ((TextureTGA_Parse(&self->base, name) & 0xff) == 0) {
        st_log_str(STR_NO_TGA_COPY);
        Texture_ReleaseD3DTexture(self);
        if (fp != NULL) fclose(fp);
        return 0;
    }

    if (texture_fx_mode() == TEXFX_SOLID)
        texture_fx_fill_solid(self->base.pTextureSurface);

    hr = self->base.pTextureSurface->QueryInterface(IID_IDirect3DTexture2,
                                                    (void **)&self->pTexture2);
    if (hr < 0) {
        /* Release first, then log — that order is the original's. */
        Texture_ReleaseD3DTexture(self);
        st_log_str(STR_NO_TEXTURE_IFACE);
        if (fp != NULL) fclose(fp);
        return 0;
    }

    st_set_image_name(&self->base, name);
    self->base.loadedState = 2;
    if (fp != NULL) fclose(fp);
    return 1;
}

/* ─── SceneTexture::SelectTextureLoader (0x43feb0) ─────────────────────────
 *
 * mode 0 picks by extension, 1 forces the DIB loader, 2 forces the TGA loader,
 * anything else is a silent failure.  The extension test is four inlined
 * strcmps against ".bmp", ".BMP", ".tga", ".TGA" in that order — case-exact,
 * so a ".Bmp" is rejected.
 */
__declspec(dllexport) unsigned int __attribute__((thiscall))
Texture_SelectTextureLoader(SceneTexture *self, IDirectDraw4 *dd,
                            IDirect3DDevice3 *dev, LPCSTR name, UINT bpp,
                            int mode)
{
    if (mode != 0) {
        if (mode == 1)
            return Texture_BindTextureResource(self, dd, dev, name, bpp, 0);
        if (mode == 2)
            return Texture_ImportSceneTextures(self, dd, dev, name, 0, bpp, 0);
        /* al cleared, upper bytes left as mode-2 */
        return (unsigned int)(mode - 2) & 0xffffff00u;
    }

    /* Note 6: no NULL check on the result. */
    const unsigned char *ext = (const unsigned char *)ORIG_STRRCHR(name, '.');

    if (st_strcmp(ext, (const unsigned char *)STR_DOT_BMP_LOWER) == 0 ||
        st_strcmp(ext, (const unsigned char *)STR_DOT_BMP_UPPER) == 0)
        return Texture_BindTextureResource(self, dd, dev, name, bpp, 0);

    if (st_strcmp(ext, (const unsigned char *)STR_DOT_TGA_LOWER) == 0)
        return Texture_ImportSceneTextures(self, dd, dev, name, 0, bpp, 0);

    int cmp = st_strcmp(ext, (const unsigned char *)STR_DOT_TGA_UPPER);
    if (cmp != 0)
        return (unsigned int)cmp & 0xffffff00u;   /* al cleared, upper: strcmp */

    return Texture_ImportSceneTextures(self, dd, dev, name, 0, bpp, 0);
}

} // extern "C"
