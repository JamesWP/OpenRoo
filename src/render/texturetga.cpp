/* LoadedImage::ParseTGAFile reimplementation.
 *
 *   0x43e190  __thiscall(this, LPCSTR path), `ret 0x4`
 *             — 2 E8 call sites (0x43EB31 inside the original Load, 0x43FB52
 *               inside ImportSceneTextures), no E9/PUSH/DATA refs, and a raw
 *               DWORD scan of the whole image finds zero occurrences of
 *               0x0043E190, so no vtable slot holds it either.
 *
 * Reconstructed from the *disassembly*.  The decompile is unusable here: the
 * frame is 0xe8 bytes with four callee-saved registers pushed at different
 * points (esi/edi in the prologue, ebp before the pixel buffer is allocated,
 * ebx before the conversion loop), so Ghidra's fixed frame base is off by 4
 * or 8 over most of the body and it aliases the file object onto the three
 * float temporaries that reuse its storage after the file is closed.
 *
 * Laid out by hand against esp, the frame is unambiguous:
 *
 *   +0x0f  RLE packet header byte          +0x50  greenShift
 *   +0x10  alpha (float32)                 +0x54  redShift
 *   +0x14  FileWrapper { vtbl, FILE* }     +0x58  blueShift
 *   +0x1c  green (float32)                 +0x5c  blueScale
 *   +0x20  blue  (float32)                 +0x60  this
 *   +0x24  temp IDirectDrawSurface4*       +0x64  redScale
 *   +0x28  TGA header, 18 bytes            +0x68  alphaScale
 *   +0x3c  RLE run pixel (up to 4 bytes)   +0x6c  IDirectDraw4* (GetDDInterface)
 *   +0x40  32-bit dest pixel pointer       +0x70  greenScale
 *   +0x44  x                               +0x74  DDSURFACEDESC2 (0x7c bytes)
 *   +0x48  y
 *   +0x4c  row start pointer
 *
 * +0x28 + 18 == +0x3a and +0x74 + 0x7c == +0xf0, which is the SEH record;
 * nothing overlaps and nothing is left over.
 *
 * Red never gets a stack slot.  Every source branch leaves it on the x87
 * stack in st(0) and the destination branch consumes a `fld st(0)` duplicate,
 * so it is the one channel that is never rounded to float32.  That is why the
 * decompile shows only three colour temporaries.  It is modelled here as a
 * `long double`, the same idiom d3dmath.cpp uses.
 *
 * ─── Bugs and asymmetries preserved deliberately ──────────────────────────
 *
 *  1. THE SOURCE INDEX USES THE HEIGHT AS ITS ROW STRIDE.
 *
 *         idx = (dwHeight - y - 1) * dwHeight + x
 *
 *     It should be `* dwWidth`.  Every texture the game ships is square, so
 *     the bug is invisible; a non-square TGA would come out sheared or would
 *     read past the buffer.  Left exactly as written — "correcting" it would
 *     change nothing for real content and would hide the defect.
 *
 *  2. The colour-map skip treats colourMapLength as a BYTE count:
 *         fseek(fp, idLength + colourMapLength, SEEK_CUR)
 *     A real colour map is `length * (depth/8)` bytes.  The game's TGAs are
 *     all colourMapType 0, so the term is zero.
 *
 *  3. A 24-bit source NEVER WRITES ALPHA.  The alpha slot keeps the previous
 *     pixel's value — zero for the first pixel of the image, and then
 *     whatever the last 16- or 32-bit image left if the bit depth changes
 *     mid-run.  Only reachable when the destination is 16-bit with an alpha
 *     channel; a 32-bit destination still folds the stale value in.
 *
 *  4. A source depth other than 16/24/32 decodes NOTHING and the destination
 *     branch converts the previous pixel's colours again.  In the original
 *     this also underflows the x87 stack by one register per pixel, because
 *     the branch pops the carried red and pushes no replacement.  The pop is
 *     not modelled; the stale-colour behaviour is.
 *
 *  5. Rounding is asymmetric between the four channels on the 16-bit
 *     destination path, and this is NOT something any reading of the
 *     decompile suggests:
 *       - green and alpha are stored to float32 (`fstp`) and reloaded before
 *         __ftol, so they are rounded to single precision first;
 *       - blue is stored with `fst` — store *and keep* — and __ftol then runs
 *         on the value still in the register, at x87 precision;
 *       - red is never stored at all.
 *     So two channels truncate a float32 and two truncate an extended value.
 *     Near an integer boundary that is a difference of one in the output.
 *     Written below the way the original computes it, channel by channel.
 *
 *  6. The 16-bit source's alpha is `(px >> 15) * 255.0f` — the one-bit alpha
 *     scaled by 255 rather than by the destination's own alpha maximum, which
 *     the destination stage then rescales.  Not a bug, but it is why 255.0f
 *     appears here and 255/31 appears for the colour channels.
 *
 *  7. Only image type 0x0a (RLE true-colour) takes the RLE path.  Everything
 *     else, including types 1/3/9/11, is read as one uncompressed block.
 *
 * ─── Return convention ────────────────────────────────────────────────────
 *
 * Every exit is `call FileWrapper::Close` followed by `xor al,al` or
 * `mov al,1`, so the result is `(close_result & 0xffffff00) | ok`.  In
 * practice fclose returns 0 and the value is exactly 0 or 1, but the whole
 * dword is reproduced rather than assumed, as in texturedib.cpp.
 *
 * The success path does NOT set loadedState or ImageName — the caller
 * (SelectTextureLoader / ImportSceneTextures) does that.  loadStatus is never
 * touched at all, unlike the DIB loader, which sets it on every failure.
 */
#include "texture.h"
#include "tga.h"
#include "log.h"
#include "alloc.h"
#include "gamestr.h"
#include "gameglobals.h"
#include <stdio.h>

/* ─── Originals left live in the binary ────────────────────────────────────
 *
 * ─── The file is OURS now (2026-09-05) ────────────────────────────────────
 *
 * This reader used to open through the game's FileWrapper and then read with
 * the game's fread, because the FILE * was the game CRT's and mingw's fread
 * would have been operating on a foreign FILE.  That reasoning was correct but
 * it treated the wrong thing as fixed: the file is opened HERE, so nothing
 * forced it to be the game's.  Whoever opens a file decides which CRT owns it.
 *
 * The FileWrapper the original uses is a purely local 8-byte
 * { vtable, FILE * } on the stack -- nothing outside this function ever sees
 * it -- so replacing it with a plain FILE * from our own CRT changes no
 * observable behaviour:
 *
 *   0x413430  ctor(this)                       vtable + fp = NULL
 *   0x413480  Open(this, path, mode) -> FILE*  closes any open file, fopens
 *   0x4134b0  CloseFile(this)                  fclose; fp = NULL iff it
 *                                              returned 0
 *   0x413460  Close(this)                      resets the vtable, then
 *                                              CloseFile if fp is non-NULL
 *
 * The original calls CloseFile once the pixel data is read and Close again at
 * every exit; the second call therefore sees fp == NULL and does nothing.  The
 * replacement closes once and NULLs, which is the same sequence of fcloses.
 *
 * One unobservable difference: the original's failure paths return the Close
 * result with AL forced to 0, so the upper three bytes of EAX carry Close's
 * return value.  We return a plain 0.  Callers test AL only.
 *
 * (The one case where the game's CRT is still required is a FILE * the game
 * it(self) opened and handed us -- see karoo-hooks/reportwriter.cpp for how that
 * one was resolved, by moving the open rather than reaching across.) */


typedef unsigned int (__cdecl *fwrite_fn)(const char *, int, int, FILE *);
#define ORIG_FWRITE ((fwrite_fn)0x004513c7)   /* the static CRT fwrite; the
                                               * stream is GG_LOG_STREAM --
                                               * see gameglobals.h */


/* The inline REPNE SCASB the original uses for the two log calls: `not ecx`
 * followed by `dec ecx`, i.e. the true length. */
static unsigned int tga_strlen(const char *s)
{
    const char *p = s;
    while (*p != '\0')
        ++p;
    return (unsigned int)(p - s);
}

/* ─── The three mask helpers, 0x43e140 / 0x43e160 / 0x43e180 ───────────────
 *
 * Tiny leaf __cdecl functions, inlined here rather than called: they take no
 * arguments the caller could get wrong and reproducing them is three lines.
 * 0x43e160 has a harmless quirk — its zero-mask exit is `xor ax,ax` (16-bit)
 * rather than `xor eax,eax`, which only works because eax was already zeroed
 * on the line above.  The value is 0 either way. */
static unsigned int mask_popcount(unsigned int m)   /* 0x43e140 */
{
    unsigned int n = 0;
    while (m != 0) { m &= m - 1; ++n; }
    return n;
}

static unsigned int mask_shift(unsigned int m)      /* 0x43e160 */
{
    unsigned int n = 0;
    if (m == 0)
        return 0;
    while ((m & 1) == 0) { m >>= 1; ++n; }
    return n;
}

static unsigned int mask_max(unsigned int bits)     /* 0x43e180 */
{
    return (1u << (unsigned char)bits) - 1u;        /* shl uses cl, so & 31 */
}

/* The three float32 constants, by bit pattern rather than by expression, so
 * the values are the originals' and not the compiler's rounding of a
 * quotient.  0x45d328 = 255.0f, 0x45d710 = 255/31, 0x45d70c = 1/255. */
static float f32_from_bits(unsigned int bits)
{
    union { unsigned int u; float f; } c;
    c.u = bits;
    return c.f;
}
#define K_255      f32_from_bits(0x437f0000u)   /* 0x0045d328 */
#define K_255_D31  f32_from_bits(0x41039ce7u)   /* 0x0045d710 */
#define K_INV_255  f32_from_bits(0x3b808180u)   /* 0x0045d70c */

/* __ftol (0x451134) as the callers use it: round-toward-zero into a 64-bit
 * temporary, low dword returned, and every call site then takes `al`. */
static unsigned int ftol8(long double v)
{
    return (unsigned int)(long long)v & 0xffu;
}


extern "C" {

__declspec(dllexport) unsigned int __attribute__((thiscall))
TextureTGA_Parse(LoadedImage *self, LPCSTR path)
{
    DDSURFACEDESC2 ddsd;
    ddsd.dwSize = sizeof(DDSURFACEDESC2);   /* 0x7c, written before the open */

    FILE *fp = fopen(path, "rb");
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

    static LONG seen = 0;
    if (InterlockedIncrement(&seen) <= 4)
        log_write("texturetga: Parse this=%p %ux%u bpp=%u type=%u name=%s\n",
                  self, h.width, h.height, h.bpp, h.imageType,
                  path ? path : "(null)");

    /* A system-memory scratch surface with the destination's pixel format,
     * exactly as BlitDIBToSurface does: inherit the description, override
     * only dwFlags and dwCaps, keep the inherited format.  The descriptor is
     * never zeroed here either. */
    self->pTextureSurface->GetSurfaceDesc(&ddsd);
    ddsd.dwFlags        = 0x1007;   /* CAPS | HEIGHT | WIDTH | PIXELFORMAT */
    ddsd.ddsCaps.dwCaps = 0x1800;   /* TEXTURE | SYSTEMMEMORY */

    IDirectDraw4 *dd = NULL;
    self->pTextureSurface->GetDDInterface((void **)&dd);

    IDirectDrawSurface4 *tmp = NULL;
    if (dd->CreateSurface(&ddsd, &tmp, NULL) < 0) {
        ORIG_FWRITE(GS_TEX_CREATESURFACE_FAILED,
                       (int)tga_strlen(GS_TEX_CREATESURFACE_FAILED),
                       1, GG_LOG_STREAM);
        if (fp != NULL) fclose(fp);
        return 0;
    }

    /* dwFlags is 0 — not DDLOCK_WAIT.  Lock overwrites ddsd with the scratch
     * surface's real geometry, which is what the conversion loop below reads:
     * dwWidth, dwHeight, lPitch, lpSurface and the four channel masks. */
    if (tmp->Lock(NULL, &ddsd, 0, NULL) < 0) {
        ORIG_FWRITE(GS_TEX_LOCK_FAILED, (int)tga_strlen(GS_TEX_LOCK_FAILED),
                       1, GG_LOG_STREAM);
        if (tmp != NULL)
            tmp->Release();
        if (fp != NULL) fclose(fp);
        return 0;
    }

    /* Bug 2: colourMapLength is added as a byte count. */
    fseek(fp, (long)(h.colourMapLength + (unsigned int)h.idLength), SEEK_CUR);

    /* Buffer size = bpp * height * width / 8, as a signed divide: the
     * `cdq; and edx,7; add; sar 3` idiom, not a shift. */
    int bits = (int)((unsigned int)h.bpp * (unsigned int)h.height
                                         * (unsigned int)h.width);
    char *buf = (char *)game_operator_new((unsigned int)((bits + ((bits >> 31) & 7)) >> 3));

    if (h.imageType == 0x0a) {
        /* RLE true-colour.  `i` is the running pixel index; the loop
         * condition is an unsigned compare against the header's pixel count,
         * so a zero-sized image skips the loop entirely. */
        unsigned int npix = (unsigned int)h.width * (unsigned int)h.height;
        unsigned int i = 0;
        if (npix != 0) {
            do {
                unsigned char pkt;
                fread(&pkt, 1, 1, fp);
                if ((pkt & 0x80) != 0) {
                    /* Run packet: one pixel, repeated (pkt & 0x7f) + 1 times.
                     * The run pixel is read into a 4-byte slot regardless of
                     * depth, and only the low bpp/8 bytes are filled. */
                    unsigned int run = 0;
                    fread(&run, (unsigned int)(h.bpp >> 3), 1, fp);
                    int count = (int)((pkt & 0x7f) + 1);
                    for (int k = 0; k < count; ++k) {
                        if (h.bpp == 0x10)
                            *(unsigned short *)(buf + ((i << 4) >> 3) + k * 2)
                                = (unsigned short)run;
                        else if (h.bpp == 0x18) {
                            char *q = buf + (((i * 3) << 3) >> 3) + k * 3;
                            q[0] = (char)run;
                            q[1] = (char)(run >> 8);
                            q[2] = (char)(run >> 16);
                        } else if (h.bpp == 0x20)
                            *(unsigned int *)(buf + ((i << 5) >> 3) + k * 4) = run;
                    }
                } else {
                    /* Raw packet: (pkt & 0x7f) + 1 pixels straight from the
                     * file, at buf + bpp*i/8 (an unsigned shift here, unlike
                     * the signed divide used for the allocation). */
                    unsigned int count = (unsigned int)(pkt & 0x7f) + 1u;
                    fread(buf + (((unsigned int)h.bpp * i) >> 3),
                               (unsigned int)h.bpp >> 3, count, fp);
                }
                i += 1u + (pkt & 0x7f);
            } while (i < npix);
        }
    } else {
        /* Bug 7: every non-0x0a type, compressed or not, lands here. */
        int n = (int)((unsigned int)h.bpp * (unsigned int)h.height
                                          * (unsigned int)h.width);
        fread(buf, 1, (unsigned int)((n + ((n >> 31) & 7)) >> 3), fp);
    }

    fclose(fp);
    fp = NULL;

    /* ─── Conversion ───────────────────────────────────────────────────────
     *
     * Per destination channel: how many bits it has (as a maximum value) and
     * where they sit.  Both derived from the Lock'd descriptor's masks. */
    unsigned int alphaScale = mask_max(mask_popcount(ddsd.ddpfPixelFormat.dwRGBAlphaBitMask));
    unsigned int redScale   = mask_max(mask_popcount(ddsd.ddpfPixelFormat.dwRBitMask));
    unsigned int greenScale = mask_max(mask_popcount(ddsd.ddpfPixelFormat.dwGBitMask));
    unsigned int blueScale  = mask_max(mask_popcount(ddsd.ddpfPixelFormat.dwBBitMask));

    unsigned int alphaShift = mask_shift(ddsd.ddpfPixelFormat.dwRGBAlphaBitMask);
    unsigned int redShift   = mask_shift(ddsd.ddpfPixelFormat.dwRBitMask);
    unsigned int greenShift = mask_shift(ddsd.ddpfPixelFormat.dwGBitMask);
    unsigned int blueShift  = mask_shift(ddsd.ddpfPixelFormat.dwBBitMask);

    /* alpha/green/blue live in float32 stack slots; red lives in st(0) and is
     * therefore never rounded to single precision.  All four start at zero,
     * which is what the 24-bit source path's missing alpha write (bug 3)
     * leaves in place for the first pixel. */
    float alpha = 0.0f, green = 0.0f, blue = 0.0f;
    long double red = 0.0L;


    const int   H     = (int)ddsd.dwHeight;
    const int   W     = (int)ddsd.dwWidth;
    const int   srcBpp = (int)h.bpp;
    const int   dstBits = (int)ddsd.ddpfPixelFormat.dwRGBBitCount;
    unsigned char *row = (unsigned char *)ddsd.lpSurface;

    for (int y = 0; y < H; ++y) {
        unsigned char *p16 = row;     /* the 16-bit write pointer (esi) */
        unsigned char *p32 = row;     /* the 32-bit write pointer (+0x40) */
        for (int x = 0; x < W; ++x) {
            /* Bug 1: the row stride is dwHeight. */
            int idx = (H - y - 1) * H + x;

            if (srcBpp == 0x10) {
                unsigned short px = *(unsigned short *)(buf + idx * 2);
                alpha = (float)((long double)(int)(px >> 15) * K_255);
                red   = (long double)(int)((px >> 10) & 0x1f) * K_255_D31;
                green = (float)((long double)(int)((px >> 5) & 0x1f) * K_255_D31);
                blue  = (float)((long double)(int)(px & 0x1f) * K_255_D31);
            } else if (srcBpp == 0x18) {
                const unsigned char *q = (const unsigned char *)(buf + idx * 3);
                blue  = (float)(int)q[0];
                green = (float)(int)q[1];
                red   = (long double)(int)q[2];
                /* bug 3: no alpha write */
            } else if (srcBpp == 0x20) {
                const unsigned char *q = (const unsigned char *)(buf + idx * 4);
                blue  = (float)(int)q[0];
                green = (float)(int)q[1];
                red   = (long double)(int)q[2];
                alpha = (float)(int)q[3];
            }
            /* bug 4: any other depth reuses the previous pixel's values */

            if (dstBits == 0x20) {
                /* No scaling: the channels are already 0..255 and the
                 * destination's are 8 bits wide. */
                unsigned int v = ftol8((long double)blue)  << blueShift;
                v += ftol8((long double)green) << greenShift;
                v += ftol8((long double)alpha) << alphaShift;
                v += ftol8(red)                << redShift;
                *(unsigned int *)p32 = v;
            } else if (dstBits == 0x10) {
                /* Rescale 0..255 to the destination channel's own maximum.
                 * The scales are masked to 16 bits before the fild.  See
                 * bug 5 for why green and alpha round through float32 and
                 * blue and red do not. */
                long double la = (long double)(int)(alphaScale & 0xffff)
                                 * alpha * K_INV_255;
                float alpha16 = (float)la;

                red = red * (long double)(int)(redScale & 0xffff) * K_INV_255;

                float green16 = (float)((long double)(int)(greenScale & 0xffff)
                                        * green * K_INV_255);

                long double lb = (long double)(int)(blueScale & 0xffff)
                                 * blue * K_INV_255;
                blue = (float)lb;              /* `fst`: stored AND kept */

                unsigned int v = ftol8(lb) << blueShift;          /* extended */
                v += ftol8((long double)green16) << greenShift;   /* float32  */
                v += ftol8((long double)alpha16) << alphaShift;   /* float32  */
                v += ftol8(red) << redShift;                      /* extended */
                *(unsigned short *)p16 = (unsigned short)v;
            }
            /* any other destination depth writes nothing at all */

            p32 += 4;
            p16 += 2;
        }
        row += ddsd.lPitch;
    }

    if (buf != NULL)
        game_free2(buf);

    if (tmp->Unlock(NULL) < 0) {
        if (tmp != NULL)
            tmp->Release();
        if (fp != NULL) fclose(fp);
        return 0;
    }

    self->pTextureSurface->Blt(NULL, tmp, NULL, DDBLT_WAIT, NULL);

    if (tmp != NULL)
        tmp->Release();

    if (fp != NULL) fclose(fp);
    return 1;
}

} // extern "C"
