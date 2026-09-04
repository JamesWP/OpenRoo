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
#include "log.h"

/* ─── Originals left live in the binary ────────────────────────────────────
 *
 * The FILE * belongs to the game's statically linked CRT, so its fread/fseek/
 * fclose must be the game's too — mingw's would be operating on a foreign
 * FILE.  All of these are shared helpers with many other callers. */

/* FileWrapper — an 8-byte { vtable, FILE * } at 0x45d3a4.
 *   0x413430  ctor(this)                       vtable + fp = NULL
 *   0x413480  Open(this, path, mode) -> FILE*  closes any open file first
 *   0x4134b0  CloseFile(this) -> int           fclose; fp = NULL iff it
 *                                              returned 0
 *   0x413460  Close(this) -> int               resets the vtable, then
 *                                              CloseFile if fp is non-NULL,
 *                                              else returns 0
 * ParseTGAFile calls CloseFile as soon as the pixel data is read and Close
 * again at every exit; the second call therefore sees fp == NULL and returns
 * 0, which is where the "upper three bytes" of the result come from. */
struct FileWrapper {
    void *pVtable;   /* +0x00, always 0x0045d3a4 */
    void *fp;        /* +0x04, the game CRT's FILE * */
};

typedef void  (__attribute__((thiscall)) *fw_ctor_fn)(FileWrapper *);
typedef void *(__attribute__((thiscall)) *fw_open_fn)(FileWrapper *, LPCSTR, LPCSTR);
typedef int   (__attribute__((thiscall)) *fw_close_fn)(FileWrapper *);
#define ORIG_FW_CTOR      ((fw_ctor_fn)0x00413430)
#define ORIG_FW_OPEN      ((fw_open_fn)0x00413480)
#define ORIG_FW_CLOSEFILE ((fw_close_fn)0x004134b0)
#define ORIG_FW_CLOSE     ((fw_close_fn)0x00413460)
#define STR_MODE_RB       ((LPCSTR)0x00465188)   /* "rb" */

typedef unsigned int (__cdecl *fread_fn)(void *, unsigned int, unsigned int, void *);
typedef int          (__cdecl *fseek_fn)(void *, long, int);
#define ORIG_FREAD ((fread_fn)0x0045158a)
#define ORIG_FSEEK ((fseek_fn)0x004517b9)

typedef char *(__cdecl *opnew_fn)(unsigned int);
typedef void  (__cdecl *free2_fn)(void *);
#define ORIG_OPERATOR_NEW ((opnew_fn)0x00450e9d)
#define ORIG_FACT_FREE2   ((free2_fn)0x004504c0)

/* ImageLogger::Log — __cdecl(const char *, int len, int, int *sink). */
typedef unsigned int (__cdecl *fwrite_fn)(const char *, int, int, int *);
#define ORIG_FWRITE ((fwrite_fn)0x004513c7)
#define GAME_LOG_FILE ((int *)0x00469cf8)

#define STR_CREATESURFACE_FAILED ((const char *)0x0046718c)
#define STR_LOCK_FAILED          ((const char *)0x004671a4)

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

/* The 18-byte TGA header, read field by field in exactly the original's
 * order and sizes (twelve separate freads, not one block read). */
struct TgaHeader {
    unsigned char  idLength;         /* +0x00 */
    unsigned char  colourMapType;    /* +0x01 */
    unsigned char  imageType;        /* +0x02 */
    unsigned short colourMapOrigin;  /* +0x03 */
    unsigned short colourMapLength;  /* +0x05 */
    unsigned char  colourMapDepth;   /* +0x07 */
    unsigned short xOrigin;          /* +0x08 */
    unsigned short yOrigin;          /* +0x0a */
    unsigned short width;            /* +0x0c */
    unsigned short height;           /* +0x0e */
    unsigned char  bpp;              /* +0x10 */
    unsigned char  descriptor;       /* +0x11 */
} __attribute__((packed));

static_assert(sizeof(TgaHeader) == 18, "TGA header must be 18 bytes");

#if KAROO_VERIFY_ORIGINAL
/* Filled by the impl with the Lock'd scratch surface's real format, which is
 * what the conversion actually reads — not the destination's. */
struct TgaDbg { unsigned long r, g, b, a, bits, w, h, pitch; int srcBpp, type; };
TgaDbg g_tgaDbg;
#endif

extern "C" {

/* The reimplementation proper.  TextureTGA_Parse below is either a direct
 * call to this or, in a verification build, the comparing wrapper. */
unsigned int __attribute__((thiscall))
TextureTGA_ParseImpl(LoadedImage *self, LPCSTR path)
{
    FileWrapper file;
    ORIG_FW_CTOR(&file);

    DDSURFACEDESC2 ddsd;
    ddsd.dwSize = sizeof(DDSURFACEDESC2);   /* 0x7c, written before the open */

    if (ORIG_FW_OPEN(&file, path, STR_MODE_RB) == NULL)
        return ORIG_FW_CLOSE(&file) & 0xffffff00u;

    /* Header: twelve reads, no error checking whatsoever. */
    TgaHeader h;
    void *fp = file.fp;
    ORIG_FREAD(&h.idLength,        1, 1, fp);
    ORIG_FREAD(&h.colourMapType,   1, 1, fp);
    ORIG_FREAD(&h.imageType,       1, 1, fp);
    ORIG_FREAD(&h.colourMapOrigin, 2, 1, fp);
    ORIG_FREAD(&h.colourMapLength, 2, 1, fp);
    ORIG_FREAD(&h.colourMapDepth,  1, 1, fp);
    ORIG_FREAD(&h.xOrigin,         2, 1, fp);
    ORIG_FREAD(&h.yOrigin,         2, 1, fp);
    ORIG_FREAD(&h.width,           2, 1, fp);
    ORIG_FREAD(&h.height,          2, 1, fp);
    ORIG_FREAD(&h.bpp,             1, 1, fp);
    ORIG_FREAD(&h.descriptor,      1, 1, fp);

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
        ORIG_FWRITE(STR_CREATESURFACE_FAILED,
                       (int)tga_strlen(STR_CREATESURFACE_FAILED),
                       1, GAME_LOG_FILE);
        return ORIG_FW_CLOSE(&file) & 0xffffff00u;
    }

    /* dwFlags is 0 — not DDLOCK_WAIT.  Lock overwrites ddsd with the scratch
     * surface's real geometry, which is what the conversion loop below reads:
     * dwWidth, dwHeight, lPitch, lpSurface and the four channel masks. */
    if (tmp->Lock(NULL, &ddsd, 0, NULL) < 0) {
        ORIG_FWRITE(STR_LOCK_FAILED, (int)tga_strlen(STR_LOCK_FAILED),
                       1, GAME_LOG_FILE);
        if (tmp != NULL)
            tmp->Release();
        return ORIG_FW_CLOSE(&file) & 0xffffff00u;
    }

    /* Bug 2: colourMapLength is added as a byte count. */
    ORIG_FSEEK(fp, (long)(h.colourMapLength + (unsigned int)h.idLength), SEEK_CUR);

    /* Buffer size = bpp * height * width / 8, as a signed divide: the
     * `cdq; and edx,7; add; sar 3` idiom, not a shift. */
    int bits = (int)((unsigned int)h.bpp * (unsigned int)h.height
                                         * (unsigned int)h.width);
    char *buf = ORIG_OPERATOR_NEW((unsigned int)((bits + ((bits >> 31) & 7)) >> 3));

    if (h.imageType == 0x0a) {
        /* RLE true-colour.  `i` is the running pixel index; the loop
         * condition is an unsigned compare against the header's pixel count,
         * so a zero-sized image skips the loop entirely. */
        unsigned int npix = (unsigned int)h.width * (unsigned int)h.height;
        unsigned int i = 0;
        if (npix != 0) {
            do {
                unsigned char pkt;
                ORIG_FREAD(&pkt, 1, 1, fp);
                if ((pkt & 0x80) != 0) {
                    /* Run packet: one pixel, repeated (pkt & 0x7f) + 1 times.
                     * The run pixel is read into a 4-byte slot regardless of
                     * depth, and only the low bpp/8 bytes are filled. */
                    unsigned int run = 0;
                    ORIG_FREAD(&run, (unsigned int)(h.bpp >> 3), 1, fp);
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
                    ORIG_FREAD(buf + (((unsigned int)h.bpp * i) >> 3),
                               (unsigned int)h.bpp >> 3, count, fp);
                }
                i += 1u + (pkt & 0x7f);
            } while (i < npix);
        }
    } else {
        /* Bug 7: every non-0x0a type, compressed or not, lands here. */
        int n = (int)((unsigned int)h.bpp * (unsigned int)h.height
                                          * (unsigned int)h.width);
        ORIG_FREAD(buf, 1, (unsigned int)((n + ((n >> 31) & 7)) >> 3), fp);
    }

    ORIG_FW_CLOSEFILE(&file);

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

#if KAROO_VERIFY_ORIGINAL
    g_tgaDbg.r = ddsd.ddpfPixelFormat.dwRBitMask;
    g_tgaDbg.g = ddsd.ddpfPixelFormat.dwGBitMask;
    g_tgaDbg.b = ddsd.ddpfPixelFormat.dwBBitMask;
    g_tgaDbg.a = ddsd.ddpfPixelFormat.dwRGBAlphaBitMask;
    g_tgaDbg.bits = ddsd.ddpfPixelFormat.dwRGBBitCount;
    g_tgaDbg.w = ddsd.dwWidth; g_tgaDbg.h = ddsd.dwHeight;
    g_tgaDbg.pitch = (unsigned long)ddsd.lPitch;
    g_tgaDbg.srcBpp = h.bpp; g_tgaDbg.type = h.imageType;
#endif

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
        ORIG_FACT_FREE2(buf);

    if (tmp->Unlock(NULL) < 0) {
        if (tmp != NULL)
            tmp->Release();
        return ORIG_FW_CLOSE(&file) & 0xffffff00u;
    }

    self->pTextureSurface->Blt(NULL, tmp, NULL, DDBLT_WAIT, NULL);

    if (tmp != NULL)
        tmp->Release();

    return (ORIG_FW_CLOSE(&file) & 0xffffff00u) | 1u;
}

} // extern "C"

/* ─── Acceptance test: KAROO_TGA_VERIFY=1 (verification builds only) ───────
 *
 * There is no synthetic-fixture oracle for this one the way dsogolden.cpp is
 * for DrawSceneObjects, because the function's whole output is a DirectDraw
 * surface: it needs a live device, and the destination's pixel format is an
 * *input* to the conversion.  So the oracle is the original itself, run over
 * the game's own textures, in the game, against the same destination surface.
 *
 * For each load: run the ORIGINAL, snapshot the destination surface, run
 * OURS into the same surface, snapshot again, and compare every byte.  Both
 * fill the surface completely via the closing Blt, so a difference in any
 * pixel — or in the return value — is a real divergence.
 *
 *   make VERIFY=1
 *   comment out the (0x3e190, _UD2) SAFETY_STUBS entry in patch.py
 *   KAROO_TGA_VERIFY=1 bash launch.sh --skip-launcher --auto-exit 60
 *
 * The CALL_PATCHES rewrite stays in place — it is what gets us entered — so
 * only the stub has to go, exactly as for DrawSceneObjects.
 */
#if KAROO_VERIFY_ORIGINAL

#include <stdlib.h>

typedef unsigned int (__attribute__((thiscall)) *parsetga_fn)(LoadedImage *, LPCSTR);
#define ORIG_PARSE_TGA ((parsetga_fn)0x0043e190)

/* Copy the whole locked surface out.  Returns NULL if it cannot be locked. */
static unsigned char *tga_snapshot(IDirectDrawSurface4 *surf, unsigned int *len)
{
    DDSURFACEDESC2 d;
    memset(&d, 0, sizeof(d));
    d.dwSize = sizeof(d);
    if (surf->Lock(NULL, &d, DDLOCK_WAIT, NULL) < 0)
        return NULL;
    unsigned int n = (unsigned int)d.lPitch * d.dwHeight;
    unsigned char *p = (unsigned char *)malloc(n);
    if (p != NULL)
        memcpy(p, d.lpSurface, n);
    surf->Unlock(NULL);
    *len = n;
    return p;
}

/* launch.sh forwards every KAROO_* variable through its `env -i` block, so an
 * unset one still reaches the game as an EMPTY string.  A bare `getenv(...) !=
 * NULL` test is therefore always true here, which silently turned the whole
 * acceptance test into original-vs-original for three runs.  Every flag in
 * this file goes through this one predicate. */
static int tga_flag(const char *name)
{
    const char *e = getenv(name);
    return (e != NULL && *e != '\0' && *e != '0') ? 1 : 0;
}

static int tga_verify_enabled(void)
{
    static int state = -1;
    if (state < 0)
        state = tga_flag("KAROO_TGA_VERIFY");
    return state;
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
TextureTGA_Parse(LoadedImage *self, LPCSTR path)
{
    if (!tga_verify_enabled())
        return TextureTGA_ParseImpl(self, path);

    static LONG files = 0, bad = 0;

    /* KAROO_TGA_VERIFY_SWAP runs ours first, the original second.
     * KAROO_TGA_VERIFY_CONTROL runs the ORIGINAL for both halves — a null
     * experiment: anything it still reports is an artefact of this harness
     * and not a difference between the two implementations. */
    const int swap    = tga_flag("KAROO_TGA_VERIFY_SWAP");
    const int control = tga_flag("KAROO_TGA_VERIFY_CONTROL");

    unsigned int rOrig = (swap && !control) ? TextureTGA_ParseImpl(self, path)
                                            : ORIG_PARSE_TGA(self, path);
    unsigned int nOrig = 0;
    unsigned char *sOrig = tga_snapshot(self->pTextureSurface, &nOrig);

    unsigned int rOurs = (control || swap) ? ORIG_PARSE_TGA(self, path)
                                            : TextureTGA_ParseImpl(self, path);
    unsigned int nOurs = 0;
    unsigned char *sOurs = tga_snapshot(self->pTextureSurface, &nOurs);

    InterlockedIncrement(&files);
    const char *verdict = "MATCH";
    unsigned int diff = 0;
    extern struct TgaDbg g_tgaDbg;
    if (sOrig == NULL || sOurs == NULL) {
        verdict = "SKIP (lock failed)";
    } else if (nOrig != nOurs) {
        verdict = "SIZE MISMATCH";
        InterlockedIncrement(&bad);
    } else {
        /* Compare only the bits the destination's pixel format actually
         * defines.  The unused byte of an X8R8G8B8 surface is outside every
         * mask, is written by neither implementation's arithmetic, and comes
         * back 0xFF from the first load of a surface and 0x00 from the
         * second.  KAROO_TGA_VERIFY_CONTROL proves that: with the ORIGINAL
         * on both sides of the comparison it reports exactly the same 29
         * files differing in exactly that lane.  So masking it out removes an
         * artefact of loading twice into one surface, not a real difference. */
        unsigned int valid = 0xffffffffu;
        unsigned int bpp   = 4;
        {
            DDSURFACEDESC2 d;
            memset(&d, 0, sizeof(d));
            d.dwSize = sizeof(d);
            if (self->pTextureSurface->GetSurfaceDesc(&d) >= 0
                && d.ddpfPixelFormat.dwRGBBitCount != 0) {
                valid = d.ddpfPixelFormat.dwRBitMask
                      | d.ddpfPixelFormat.dwGBitMask
                      | d.ddpfPixelFormat.dwBBitMask
                      | d.ddpfPixelFormat.dwRGBAlphaBitMask;
                bpp = d.ddpfPixelFormat.dwRGBBitCount / 8;
            }
        }
        unsigned char vmask[4];
        for (unsigned int k = 0; k < 4; ++k)
            vmask[k] = (k < bpp) ? (unsigned char)(valid >> (8 * k)) : 0;

        unsigned int lane[4] = {0,0,0,0};
        char sample[160]; sample[0] = '\0';
        int nsample = 0;
        for (unsigned int i = 0; i < nOrig; ++i)
            if (((sOrig[i] ^ sOurs[i]) & vmask[i % bpp]) != 0) {
                ++diff;
                ++lane[i & 3];
                if (nsample < 3) {
                    char one[48];
                    wsprintfA(one, " [%u]%02X/%02X", i, sOrig[i], sOurs[i]);
                    lstrcatA(sample, one);
                    ++nsample;
                }
            }
        if (diff != 0 || rOrig != rOurs) {
            verdict = "DIFF";
            InterlockedIncrement(&bad);
            log_write("tgaverify:   lanes b=%u g=%u r=%u a=%u  masks "
                      "R=%08lX G=%08lX B=%08lX A=%08lX bits=%lu "
                      "%lux%lu pitch=%lu src=%d type=%d %s\n",
                      lane[0], lane[1], lane[2], lane[3],
                      g_tgaDbg.r, g_tgaDbg.g, g_tgaDbg.b, g_tgaDbg.a,
                      g_tgaDbg.bits, g_tgaDbg.w, g_tgaDbg.h, g_tgaDbg.pitch,
                      g_tgaDbg.srcBpp, g_tgaDbg.type, sample);
        }
    }
    log_write("tgaverify: %-18s %6u bytes  %6u differ  ret %08X/%08X  %s  "
              "[%ld files, %ld bad]\n",
              verdict, nOrig, diff, rOrig, rOurs, path ? path : "(null)",
              files, bad);

    free(sOrig);
    free(sOurs);
    return rOurs;
}

#else

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
TextureTGA_Parse(LoadedImage *self, LPCSTR path)
{
    return TextureTGA_ParseImpl(self, path);
}

#endif
