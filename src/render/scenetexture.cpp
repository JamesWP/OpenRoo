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
 * 0x43ea50 stays live in the binary and is called through.  0x43f720 does
 * NOT any more: it and its enumeration callback 0x43f590 are reimplemented
 * below (2026-09-19, ENDGAME_PLAN E1), which is why this file's only
 * remaining ORIG_ is the palette helper.
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
#include "scenetexture.h"   /* our own owner header; brings in texture.h */
#include "tga.h"
#include "log.h"
#include "alloc.h"
#include "gamestr.h"
#include "gameglobals.h"
#include "gamelog.h"

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



/* See the header note: __stdcall, not __thiscall.  Both helpers are gone as
 * originals now — 0x43f720 and its callback are reimplemented below, and
 * 0x43ea50 is Texture_CreatePaletteFromDIB in texture.cpp (it belongs to the
 * LoadedImage TU, and it was the last function in it).  This file calls no
 * game address any more. */

/* Strings at their original addresses, so the pointer handed to the logger is
 * identical to the original's. */

/* Already-replaced neighbours; their originals are UD2-stubbed. */
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
    fwrite(s, (int)st_strlen(s), 1, stderr);
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
    sprintf(copy, GS_FMT_S, name);
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
enum TextureFx { TEXFX_OFF = 0, TEXFX_BPP16, TEXFX_SOLID, TEXFX_DEEPFMT };

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
            else if (lstrcmpiA(buf, "deepfmt") == 0)
                cached = TEXFX_DEEPFMT;
        }
        log_write("scenetexture: FX mode = %s\n",
                  cached == TEXFX_BPP16 ? "bpp16" :
                  cached == TEXFX_SOLID ? "solid" :
                  cached == TEXFX_DEEPFMT ? "deepfmt" : "off");
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

/* ─── The texture-format picker: 0x43f720 and its callback 0x43f590 ────────
 *
 * PickTextureFormatForDepth enumerates the device's texture formats and keeps
 * one; EnumTextureFormatsPickerCallback is the scoring function that decides
 * which.  They are replaced as a pair for the reason ENDGAME_PLAN records for
 * DrawBigText: 0x43f590's ONLY reference in the whole binary is the
 * `PUSH 0x43f590` at 0x43f74e inside 0x43f720 (xref.py), so replacing the
 * outer function alone would have moved the callback rather than retired it.
 *
 * 0x43f720's two call sites, 0x43faaa and 0x43fd55, are inside
 * ImportSceneTextures and BindTextureResource — both already ours — so no
 * CALL_PATCHES entry is needed and none is added.  Both get a SAFETY_STUB
 * anyway, so a site nobody found faults as c000001d instead of quietly
 * running game code.
 *
 * ─── The context, and its one deliberate oddity ───────────────────────────
 *
 * 0x43f720 builds a 0x25-byte context on its stack: a DWORD request, a BYTE
 * alpha flag, and an UNALIGNED DDPIXELFORMAT at +5.  It zeroes it with
 * `mov ecx,9; rep stosd` — 0x24 bytes, one short — and then `stosb`s the
 * alpha flag byte into the 0x25th, which is the TOP BYTE of the kept format's
 * dwRGBAlphaBitMask.  The `mov [ctx+4],cl` two instructions later is the one
 * the callback actually reads; the stosb is a stray.  It is observable only
 * when the enumeration keeps nothing at all, in which case the caller's
 * DDPIXELFORMAT comes back all zero except for alphaFlag << 24 in the alpha
 * mask.  Reproduced, per CLAUDE.md's "preserve bugs".
 *
 * ─── What the callback accepts, and then what it prefers ──────────────────
 *
 * Two stages, and they are separate: an eligibility gate, then a preference.
 *
 * Gate: DDPF_ALPHA (0x2) formats are always skipped.  At 8 bits or fewer the
 * format must be palettised (DDPF_PALETTEINDEXED4|8, 0x28) AND exactly 8 bits
 * wide — the `cmp edi,8 / jbe` then `jnc` pair means a 4-bit palettised format
 * reaches the second test and fails it.  Above 8 bits it must be DDPF_RGB.
 *
 * Preference, with nothing kept yet: take it.  Otherwise, when no alpha is
 * asked for, the format must be at least as deep as the request and strictly
 * CLOSER to it than the kept one — so the smallest depth at or above the
 * request wins and the first of a tie keeps its place.  When alpha IS asked
 * for and the candidate is not strictly closer, it gets a second chance as an
 * equal-depth alternative: same bit count, strictly more alpha bits than the
 * kept one, and no more alpha bits than a quarter of its own depth.
 *
 * All the arithmetic is unsigned, including `bits - request` where the kept
 * format is shallower than the request and the subtraction wraps.  That is a
 * semantic, not a rounding artefact, so it is preserved as written.
 *
 * The log line goes through the same stderr path as the rest of this file
 * (CRT_PLAN Stage B/C): sprintf into the original's 100-byte stack buffer,
 * then fwrite of strlen bytes.  Both branches of the original log the same
 * six fields from the CANDIDATE, before it becomes the kept one.
 */

/* 0x43e140, __cdecl(DWORD) -> int — Kernighan's popcount, `lea edx,[ecx-1];
 * and ecx,edx` per bit.  texturetga.cpp has the same helper as mask_popcount;
 * this copy is scenetexture.cpp's because the original's other four call
 * sites are inside the already-stubbed ParseTGAFile, which makes 0x43e140 a
 * DEAD_STUBS candidate once these two callers are ours.  The callers truncate
 * the result to AX, and that truncation is reproduced. */
static unsigned int st_mask_popcount(DWORD mask)
{
    unsigned int n = 0;
    while (mask != 0) {
        mask &= mask - 1;
        ++n;
    }
    return n;
}

#pragma pack(push, 1)
struct PickFormatCtx {
    DWORD         dwRequestedBpp;   /* +0x00 */
    BYTE          bWantAlpha;       /* +0x04 */
    DDPIXELFORMAT kept;             /* +0x05 .. +0x24, deliberately unaligned */
};
#pragma pack(pop)
static_assert(sizeof(DDPIXELFORMAT) == 0x20, "DDPIXELFORMAT must be 0x20 bytes");
static_assert(sizeof(PickFormatCtx) == 0x25, "the picker context is 0x25 bytes");
static_assert(__builtin_offsetof(PickFormatCtx, kept) == 5, "kept format at +5");

/* KAROO_TEXTURE_FX=deepfmt reverses the preference: among the formats at or
 * above the request, keep the one FURTHEST above it instead of the closest.
 * A direction change rather than a value perturbation, per CLAUDE.md, and one
 * only this callback can make — nothing else in the binary chooses a texture
 * format.  Its blast radius is a pixel format, so it moves no geometry and
 * cannot reach the bridge/slide spawn scans that crash levelreport.py.  The
 * eligibility gate is untouched, so the chosen format is still an RGB format
 * the device offered. */
static bool st_fmt_preferred(DWORD cand, DWORD kept)
{
    return texture_fx_mode() == TEXFX_DEEPFMT ? (cand > kept) : (cand < kept);
}

/* 0x43f590 — LPD3DENUMPIXELFORMATSCALLBACK.  Always returns D3DENUMRET_OK;
 * the original has no early-out and enumerates every format every time. */
static HRESULT WINAPI st_enum_texture_formats_picker(LPDDPIXELFORMAT pf,
                                                     LPVOID param)
{
    DWORD flags = pf->dwFlags;
    if (flags & 0x02)                       /* DDPF_ALPHA: test dl,0x2 */
        return D3DENUMRET_OK;

    DWORD bits = pf->dwRGBBitCount;
    if (bits <= 8) {
        if (!(flags & 0x28))                /* DDPF_PALETTEINDEXED4|8 */
            return D3DENUMRET_OK;
        if (bits != 8)                      /* 4-bit palettised is rejected */
            return D3DENUMRET_OK;
    } else {
        if (!(flags & 0x40))                /* DDPF_RGB */
            return D3DENUMRET_OK;
    }

    PickFormatCtx *ctx = (PickFormatCtx *)param;
    DWORD req  = ctx->dwRequestedBpp;
    DWORD kept = ctx->kept.dwRGBBitCount;   /* 0 until something is kept */

    if (ctx->bWantAlpha == 0) {
        if (kept != 0) {
            if (bits < req)
                return D3DENUMRET_OK;
            if (!st_fmt_preferred(bits - req, kept - req))
                return D3DENUMRET_OK;
        }
    } else if (kept != 0) {
        bool closer = (bits >= req) && st_fmt_preferred(bits - req, kept - req);
        if (!closer) {
            if (bits != kept)
                return D3DENUMRET_OK;
            /* Equal depth: strictly more alpha bits wins, but only up to a
             * quarter of the depth.  Both comparisons are on the 16-bit
             * truncation of the popcount, as the original's `mov di,ax` /
             * `cmp ax,di` and `and eax,0xffff` do it. */
            unsigned short keptAlpha =
                (unsigned short)st_mask_popcount(ctx->kept.dwRGBAlphaBitMask);
            unsigned short candAlpha =
                (unsigned short)st_mask_popcount(pf->dwRGBAlphaBitMask);
            if (candAlpha <= keptAlpha)
                return D3DENUMRET_OK;
            if ((DWORD)candAlpha > (bits >> 2))
                return D3DENUMRET_OK;
        }
    }

    char msg[100];                          /* the original's 0x64 frame */
    sprintf(msg, GS_TEX_FMT_PIXELFORMAT, (int)flags, (int)bits,
            (unsigned int)pf->dwRBitMask, (unsigned int)pf->dwGBitMask,
            (unsigned int)pf->dwBBitMask, (unsigned int)pf->dwRGBAlphaBitMask);
    st_log_str(msg);

    memcpy(&ctx->kept, pf, 0x20);           /* rep movsd, 8 dwords */
    return D3DENUMRET_OK;
}

/* 0x43f720 — __stdcall, not __thiscall; see the header note. */
static void __stdcall st_pick_texture_format(IDirect3DDevice3 *dev, DWORD bpp,
                                             DWORD alphaFlag,
                                             DDPIXELFORMAT *out)
{
    PickFormatCtx ctx;
    memset(&ctx, 0, 0x24);                          /* mov ecx,9; rep stosd */
    ((BYTE *)&ctx)[0x24] = (BYTE)alphaFlag;         /* the stray stosb */
    ctx.bWantAlpha     = (BYTE)alphaFlag;
    ctx.dwRequestedBpp = bpp;

    dev->EnumTextureFormats(st_enum_texture_formats_picker, &ctx);

    memcpy(out, &ctx.kept, 0x20);                   /* rep movsd, 8 dwords */
}

extern "C" {

/* ─── The SceneTexture ctor/dtor family — the TU's last three ─────────────
 *
 *   0x43f540 SceneTexture::Constructor          __thiscall(this) -> this
 *   0x43f580 SceneTexture::DtorBody             __thiscall(this), ret 0
 *   0x43f560 SceneTexture::ScalarDeletingDtor   __thiscall(this, flags), ret 4
 *
 * Replaced together with LoadedImage's three (texture.cpp) — Constructor
 * CALLs 0x43dde0 and DtorBody tail-JMPs to 0x43de20, so the derived class on
 * its own would have created two callbacks instead of retiring any.  With
 * these the SceneTexture TU is at 100%.
 *
 * ─── The reference audit, and why this one needed all three lists ─────────
 *
 * This is the first group in this file where CALL_PATCHES alone was not
 * enough, so the counts are worth recording (xref.py over Karoo.exe.orig):
 *
 *   0x43f540  14 refs — 9 CALL, 4 JMP, and a PUSH at 0x43c56d
 *   0x43f580  15 refs — 8 CALL, 5 JMP, and PUSHes at 0x43c568 and 0x43c856
 *   0x43f560   0 refs — vtable slot 0 only
 *
 * With LoadedImage's 4 and 5 that is 38 references for the six functions, and
 * patch.py reports 38 rewrites — 19 CALL, 16 JMP, 3 PUSH.  The two counts
 * agreeing is the check; they did not on the first pass, and the split above
 * is the corrected one.
 *
 * The JMPs are compiler thunks in the 0x425c90-0x425fb5 and 0x42d850-0x42d953
 * blocks — the array-of-member ctor/dtor loops the LinkedList audit named as
 * the static-initialiser region.  JMP_PATCHES handles them exactly as it does
 * Sim_DestroyMovableEntityBase's five.
 *
 * The three PUSHes are the case CLAUDE.md says to read by USE rather than by
 * type, and here the use is unambiguous: 0x43c568/0x43c56d are consecutive
 * `push 0x43f580; push 0x43f540; push 6; push 0x1c; push ptr` feeding
 * 0x00451db5, MSVC's vector-constructor iterator.  They are genuine function
 * pointers for an array of SIX SceneTextures at +8 of the object whose vtable
 * is 0x45d6fc, with stride 0x1c — which independently confirms this file's
 * `sizeof(SceneTexture) == 0x1c`.  0x43c856 is the matching dtor-only push.
 * PUSH_PATCHES, not a mis-read RGB constant.
 *
 * And 0x43f560 is the LinkedList::ScalarDestructor case again: no CALL, no
 * JMP, reachable only through the vtable.  An E8/E9 scan would have missed it.
 *
 * ─── The vtable is ours ───────────────────────────────────────────────────
 *
 * 0x0045d71c is exactly one slot — its only writers are this class's
 * Constructor and DtorBody (Ghidra xrefs), and the neighbouring dwords belong
 * to BridgeObject's table below it and to another class's above it, not to
 * this one.  That slot is ours, so ENDGAME_PLAN's licence applies: we install
 * g_SceneTextureVtable and leave the game's slot pointing at the UD2.
 *
 * Preserved deliberately: DtorBody writes the SceneTexture vtable and then
 * tail-calls the base dtor body, which immediately overwrites it with the
 * LoadedImage one.  The first store is dead in every reachable path.  It is
 * MSVC's standard codegen for a derived dtor and it is reproduced rather than
 * elided — the net state of the object is the base vtable, either way.
 */

/* Forward declaration: the vtable below needs its address. */
__declspec(dllexport) SceneTexture *__attribute__((thiscall))
Texture_SceneScalarDtor(SceneTexture *self, unsigned int flags);

static void *const g_SceneTextureVtable[1] = { (void *)&Texture_SceneScalarDtor };

/* Same question as texture.cpp's, answered the same way: ours normally, the
 * game's UD2-backed 0x0045d71c under KAROO_IMAGE_FX=gamevtbl. */
static void *scene_vtable(void)
{
    return Texture_ImageFxGameVtable() ? (void *)0x0045d71c
                                       : (void *)g_SceneTextureVtable;
}

__declspec(dllexport) SceneTexture *__attribute__((thiscall))
Texture_SceneCtor(SceneTexture *self)
{
    static unsigned long seen; Texture_ImageFirstCall("SceneTexture::Constructor", &seen);
    Texture_ImageCtor(&self->base);                       /* CALL 0x43dde0 */
    self->base.unknown00 = scene_vtable();
    self->pTexture2      = NULL;
    return self;
}

__declspec(dllexport) void __attribute__((thiscall))
Texture_SceneDtorBody(SceneTexture *self)
{
    static unsigned long seen; Texture_ImageFirstCall("SceneTexture::DtorBody", &seen);
    self->base.unknown00 = scene_vtable();                /* dead store, kept */
    Texture_ImageDtorBody(&self->base);                   /* JMP 0x43de20 */
}

__declspec(dllexport) SceneTexture *__attribute__((thiscall))
Texture_SceneScalarDtor(SceneTexture *self, unsigned int flags)
{
    static unsigned long seen; Texture_ImageFirstCall("SceneTexture::ScalarDeletingDtor", &seen);
    Texture_SceneDtorBody(self);
    if ((flags & 1) != 0)
        game_free2(self);     /* the object is the game's, not ours */
    return self;
}

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

    st_pick_texture_format(dev, bpp, 0, &ddsd.ddpfPixelFormat);
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
        IDirectDrawPalette *pal = Texture_CreatePaletteFromDIB(dd, (HBITMAP)hbmp);
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
    st_log_str(GS_FMT_NEWLINE);

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

    st_log_str(GS_TEX_TYPE_OK);

    char msg[256];
    unsigned int w = h.width;
    sprintf(msg, GS_TEX_FMT_X_SIZE, w, st_size_report_value(w));
    st_log_str(msg);
    unsigned int ht = h.height;
    sprintf(msg, GS_TEX_FMT_Y_SIZE, ht, st_size_report_value(ht));
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

    st_pick_texture_format(dev, bpp, alphaFlag, &ddsd.ddpfPixelFormat);
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
        st_log_str(GS_TEX_NO_TEXTURE_SURFACE);
        if (fp != NULL) fclose(fp);
        return 0;
    }

    if ((TextureTGA_Parse(&self->base, name) & 0xff) == 0) {
        st_log_str(GS_TEX_NO_TGA_COPY);
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
        st_log_str(GS_TEX_NO_TEXTURE_IFACE);
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
    const unsigned char *ext = (const unsigned char *)strrchr(name, '.');

    if (st_strcmp(ext, (const unsigned char *)GS_TEX_DOT_BMP_LOWER) == 0 ||
        st_strcmp(ext, (const unsigned char *)GS_TEX_DOT_BMP_UPPER) == 0)
        return Texture_BindTextureResource(self, dd, dev, name, bpp, 0);

    if (st_strcmp(ext, (const unsigned char *)GS_TEX_DOT_TGA_LOWER) == 0)
        return Texture_ImportSceneTextures(self, dd, dev, name, 0, bpp, 0);

    int cmp = st_strcmp(ext, (const unsigned char *)GS_TEX_DOT_TGA_UPPER);
    if (cmp != 0)
        return (unsigned int)cmp & 0xffffff00u;   /* al cleared, upper: strcmp */

    return Texture_ImportSceneTextures(self, dd, dev, name, 0, bpp, 0);
}

} // extern "C"

/* ─── TextureManager (0x004400d0, 0x00440220) ──────────────────────────────
 *
 * GetOrLoad walks the cache comparing names case-insensitively the way the
 * original does: by lowercasing BOTH strings IN PLACE and then strcmp'ing.
 * So the caller's buffer and every cached ImageName are permanently
 * lowercased -- which is why "TM: %s loaded" logs the lowercased name.  Kept.
 *
 * alphaFlag is a BYTE in the original's push (only AL is meaningful); the
 * upper bytes are passed through to ImportSceneTextures as the original's
 * stack slot carried them, which for the theme loader is always zero. */
static void tm_lower_inplace(char *s)
{
    /* CrtStrLwr's C-locale loop: 'A'..'Z' only. */
    for (; *s; s++)
        if (*s > '@' && *s < '[')
            *s += ' ';
}

typedef void *(__attribute__((thiscall)) *tm_scalar_dtor_fn)(void *self, unsigned int flags);

static void tm_delete(SceneTexture *t)
{
    tm_scalar_dtor_fn dtor = *(tm_scalar_dtor_fn *)t->base.unknown00;
    dtor(t, 1);
}

extern "C" __declspec(dllexport) SceneTexture *__attribute__((thiscall))
TextureManager_GetOrLoad(TextureManager *self, IDirectDraw4 *dd,
                         IDirect3DDevice3 *dev, char *filename,
                         DWORD alphaFlag, UINT bpp, DWORD textureStage)
{
    for (LinkedListNode *node = self->cache.pHead; node != NULL; ) {
        SceneTexture *cached = (SceneTexture *)node->pValue;
        node = node->pNextNode;
        tm_lower_inplace(filename);
        tm_lower_inplace(cached->base.ImageName);
        if (strcmp(cached->base.ImageName, filename) == 0) {
            if (self->pLogger != NULL)
                GameLog_LogMessage(self->pLogger, 1, GS_TM_FOUND, filename);
            return cached;
        }
    }

    void *mem = game_operator_new(sizeof(SceneTexture));
    SceneTexture *tex = (mem != NULL) ? Texture_SceneCtor((SceneTexture *)mem) : NULL;
    unsigned int ok = Texture_ImportSceneTextures(tex, dd, dev, filename,
                                                  alphaFlag, bpp, textureStage);
    if ((ok & 0xff) == 0) {
        if (tex != NULL)
            tm_delete(tex);
        if (self->pLogger != NULL)
            GameLog_LogMessage(self->pLogger, 3, GS_TM_FAILED, filename);
        return NULL;
    }
    if (self->pLogger != NULL)
        GameLog_LogMessage(self->pLogger, 1, GS_TM_LOADED, filename);
    LinkedList_Append(&self->cache, tex);
    return tex;
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
TextureManager_ReleaseAll(TextureManager *self)
{
    for (LinkedListNode *node = self->cache.pHead; node != NULL; ) {
        SceneTexture *tex = (SceneTexture *)node->pValue;
        node = node->pNextNode;
        if (tex != NULL) {
            Texture_ReleaseD3DTexture(tex);
            tm_delete(tex);
        }
    }
    LinkedList_Clear(&self->cache);
}

/* ─── TextureManager lifecycle: 0x440070 ctor, 0x4400b0 dtor body, 0x440090 scalar ──────────────
 *
 * Every instance is static (see scenetexture.h), so nothing ever deletes one and the
 * scalar dtor's free is unreached -- reimplemented, not exercised.  It stays
 * on the game heap, the LinkedList precedent: no allocator of one is ours. */
static void *const g_TextureManagerVtable[1] = { (void *)&TextureManager_ScalarDestructor };

extern "C" __declspec(dllexport) TextureManager *__attribute__((thiscall))
TextureManager_Construct(TextureManager *self)
{
    List_Init(&self->cache);
    self->vtable  = (void *)g_TextureManagerVtable;
    self->pLogger = NULL;
    return self;
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
TextureManager_Destruct(TextureManager *self)
{
    self->vtable = (void *)g_TextureManagerVtable;
    List_Destruct(&self->cache);
}

extern "C" __declspec(dllexport) TextureManager *__attribute__((thiscall))
TextureManager_ScalarDestructor(TextureManager *self, unsigned char flags)
{
    TextureManager_Destruct(self);
    if (flags & 1)
        game_free2(self);
    return self;
}

/* 0x4400c0: pLogger = logger.  Two E8, both in the D3D setup at 0x426072 /
 * 0x426081 (the global manager and the Scene's). */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
TextureManager_SetLogger(TextureManager *self, GameLogger *logger)
{
    self->pLogger = logger;
}

/* 0x440260: Texture_Load every non-NULL cached image, head to tail.  The
 * next pointer is read before the load, as the original does. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
TextureManager_LoadAll(TextureManager *self)
{
    for (LinkedListNode *n = self->cache.pHead; n != NULL; ) {
        LoadedImage *img = (LoadedImage *)n->pValue;
        n = n->pNextNode;
        if (img != NULL)
            Texture_Load(img);
    }
}
