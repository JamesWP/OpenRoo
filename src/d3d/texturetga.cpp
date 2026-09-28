/* LoadedImage's TGA loader: read the file, convert each pixel to the texture
 * surface's own format in a system-memory scratch surface, and Blt it across.
 *
 * Only image type 0x0a (RLE true-colour) takes the RLE path; every other type,
 * including 1/3/9/11, is read as one uncompressed block.
 *
 * The 16-bit destination path truncates each channel with __ftol semantics,
 * and the channels are not rounded alike: green and alpha go through float32
 * first, blue and red are truncated at extended precision.  Near an integer
 * boundary that is a difference of one in the output, so the code keeps it
 * channel by channel.
 *
 * The success path does not set loadedState or ImageName -- the caller does --
 * and loadStatus is never touched, unlike the DIB loader. */

#include "scenetexture.h"
#include "d3dnative.h"
#include "ddrawdiag.h"
#include "tga.h"
#include "log.h"
#include "gamestr.h"
#include "gameglobals.h"
#include <stdio.h>
#include <new>

/* The length for the two log calls. */
static unsigned int tga_strlen(const char *s)
{
    const char *p = s;
    while (*p != '\0')
        ++p;
    return (unsigned int)(p - s);
}

/* ─── The three mask helpers ───────────────────────────────────────────────
 */
static unsigned int mask_popcount(unsigned int m)
{
    unsigned int n = 0;
    while (m != 0) { m &= m - 1; ++n; }
    return n;
}

static unsigned int mask_shift(unsigned int m)
{
    unsigned int n = 0;
    if (m == 0)
        return 0;
    while ((m & 1) == 0) { m >>= 1; ++n; }
    return n;
}

static unsigned int mask_max(unsigned int bits)
{
    return (1u << (unsigned char)bits) - 1u;  // the shift count is taken mod 32
}

/* The three float32 constants, by bit pattern: 255.0f, 255/31 and 1/255. */
static float f32_from_bits(unsigned int bits)
{
    union { unsigned int u; float f; } c;
    c.u = bits;
    return c.f;
}
#define K_255      f32_from_bits(0x437f0000u)
#define K_255_D31  f32_from_bits(0x41039ce7u)
#define K_INV_255  f32_from_bits(0x3b808180u)

/* __ftol as the callers use it: truncate into a 64-bit temporary and keep the
 * low byte. */
static unsigned int ftol8(long double v)
{
    return (unsigned int)(long long)v & 0xffu;
}

extern "C" {

__declspec(dllexport) unsigned int  
TextureTGA_Parse(LoadedImage *self, LPCSTR path)
{
    DDSURFACEDESC2 ddsd;
    ddsd.dwSize = sizeof(DDSURFACEDESC2);  // written before the open

    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;

    // Header: twelve reads, no error checking.
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

    // A system-memory scratch surface with the destination's pixel format, as
    // BlitToSurface builds it: the descriptor is inherited, not zeroed.
    ddiag_surface_desc(self->textureSurface()->GetSurfaceDesc(&ddsd), &ddsd);
    ddsd.dwFlags        = 0x1007;  // CAPS | HEIGHT | WIDTH | PIXELFORMAT
    ddsd.ddsCaps.dwCaps = 0x1800;  // TEXTURE | SYSTEMMEMORY

    IDirectDraw4 *dd = NULL;
    self->textureSurface()->GetDDInterface((void **)&dd);

    IDirectDrawSurface4 *tmp = NULL;
    HRESULT hr = dd->CreateSurface(&ddsd, &tmp, NULL);
    ddiag_create_surface(hr, &ddsd);
    if (hr < 0) {
        fwrite(GS_TEX_CREATESURFACE_FAILED,
                       (int)tga_strlen(GS_TEX_CREATESURFACE_FAILED),
                       1, stderr);
        if (fp != NULL) fclose(fp);
        return 0;
    }

    // dwFlags is 0, not DDLOCK_WAIT.  Lock overwrites ddsd with the scratch
    // surface's real geometry, which the conversion loop reads.
    hr = tmp->Lock(NULL, &ddsd, 0, NULL);
    ddiag_lock(hr, 0, &ddsd);
    if (hr < 0) {
        fwrite(GS_TEX_LOCK_FAILED, (int)tga_strlen(GS_TEX_LOCK_FAILED),
                       1, stderr);
        if (tmp != NULL)
            tmp->Release();
        if (fp != NULL) fclose(fp);
        return 0;
    }

    // PRESERVED: colourMapLength is added as a byte count, not as length *
    // depth/8.  Every shipped TGA has no colour map.
    fseek(fp, (long)(h.colourMapLength + (unsigned int)h.idLength), SEEK_CUR);

    // bpp * height * width / 8 as a signed divide.
    int bits = (int)((unsigned int)h.bpp * (unsigned int)h.height
                                         * (unsigned int)h.width);
    // nothrow: a failed allocation leaves buf NULL, which the free below
    // guards against.
    char *buf = new (std::nothrow)
                    char[(unsigned int)((bits + ((bits >> 31) & 7)) >> 3)];

    if (h.imageType == 0x0a) {
        // RLE true-colour.  The loop compares the running pixel index unsigned
        // against the header's pixel count, so a zero-sized image skips it.
        unsigned int npix = (unsigned int)h.width * (unsigned int)h.height;
        unsigned int i = 0;
        if (npix != 0) {
            do {
                unsigned char pkt;
                fread(&pkt, 1, 1, fp);
                if ((pkt & 0x80) != 0) {
                    // Run packet: one pixel, repeated (pkt & 0x7f) + 1 times.
                    // It is read into a 4-byte slot and only the low bpp/8
                    // bytes are filled.
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
                    // Raw packet: (pkt & 0x7f) + 1 pixels straight from the
                    // file, at buf + bpp*i/8 (an unsigned shift here, unlike
                    // the allocation's divide).
                    unsigned int count = (unsigned int)(pkt & 0x7f) + 1u;
                    fread(buf + (((unsigned int)h.bpp * i) >> 3),
                               (unsigned int)h.bpp >> 3, count, fp);
                }
                i += 1u + (pkt & 0x7f);
            } while (i < npix);
        }
    } else {
        int n = (int)((unsigned int)h.bpp * (unsigned int)h.height
                                          * (unsigned int)h.width);
        fread(buf, 1, (unsigned int)((n + ((n >> 31) & 7)) >> 3), fp);
    }

    fclose(fp);
    fp = NULL;

    // ─── Conversion ─────────────────────────────────────────────────────────
    //
    // Per destination channel: its maximum value and where it sits, from the
    // Lock'd descriptor's masks.
    unsigned int alphaScale = mask_max(mask_popcount(ddsd.ddpfPixelFormat.dwRGBAlphaBitMask));
    unsigned int redScale   = mask_max(mask_popcount(ddsd.ddpfPixelFormat.dwRBitMask));
    unsigned int greenScale = mask_max(mask_popcount(ddsd.ddpfPixelFormat.dwGBitMask));
    unsigned int blueScale  = mask_max(mask_popcount(ddsd.ddpfPixelFormat.dwBBitMask));

    unsigned int alphaShift = mask_shift(ddsd.ddpfPixelFormat.dwRGBAlphaBitMask);
    unsigned int redShift   = mask_shift(ddsd.ddpfPixelFormat.dwRBitMask);
    unsigned int greenShift = mask_shift(ddsd.ddpfPixelFormat.dwGBitMask);
    unsigned int blueShift  = mask_shift(ddsd.ddpfPixelFormat.dwBBitMask);

    // Red stays at extended precision; the other three are float32.  All four
    // start at zero.
    float alpha = 0.0f, green = 0.0f, blue = 0.0f;
    long double red = 0.0L;

    const int   H     = (int)ddsd.dwHeight;
    const int   W     = (int)ddsd.dwWidth;
    const int   srcBpp = (int)h.bpp;
    const int   dstBits = (int)ddsd.ddpfPixelFormat.dwRGBBitCount;
    unsigned char *row = (unsigned char *)ddsd.lpSurface;

    for (int y = 0; y < H; ++y) {
        unsigned char *p16 = row;
        unsigned char *p32 = row;
        for (int x = 0; x < W; ++x) {
            // PRESERVED: the source row stride is the height, not the width.
            // Every shipped texture is square; a non-square TGA would come out
            // sheared or read past the buffer.
            int idx = (H - y - 1) * H + x;

            // PRESERVED: a 24-bit source never writes alpha, so the previous
            // pixel's alpha (zero for the first) is used; any other source
            // depth decodes nothing and the previous pixel's colours are
            // converted again.
            if (srcBpp == 0x10) {
                unsigned short px = *(unsigned short *)(buf + idx * 2);
                alpha = (float)((long double)(int)(px >> 15) * K_255);  // 1-bit alpha
                red   = (long double)(int)((px >> 10) & 0x1f) * K_255_D31;
                green = (float)((long double)(int)((px >> 5) & 0x1f) * K_255_D31);
                blue  = (float)((long double)(int)(px & 0x1f) * K_255_D31);
            } else if (srcBpp == 0x18) {
                const unsigned char *q = (const unsigned char *)(buf + idx * 3);
                blue  = (float)(int)q[0];
                green = (float)(int)q[1];
                red   = (long double)(int)q[2];
            } else if (srcBpp == 0x20) {
                const unsigned char *q = (const unsigned char *)(buf + idx * 4);
                blue  = (float)(int)q[0];
                green = (float)(int)q[1];
                red   = (long double)(int)q[2];
                alpha = (float)(int)q[3];
            }

            if (dstBits == 0x20) {
                // No scaling: the channels are already 0..255 and the
                // destination's are 8 bits wide.
                unsigned int v = ftol8((long double)blue)  << blueShift;
                v += ftol8((long double)green) << greenShift;
                v += ftol8((long double)alpha) << alphaShift;
                v += ftol8(red)                << redShift;
                *(unsigned int *)p32 = v;
            } else if (dstBits == 0x10) {
                // Rescale 0..255 to the destination channel's own maximum.
                // The scales are masked to 16 bits.
                long double la = (long double)(int)(alphaScale & 0xffff)
                                 * alpha * K_INV_255;
                float alpha16 = (float)la;

                red = red * (long double)(int)(redScale & 0xffff) * K_INV_255;

                float green16 = (float)((long double)(int)(greenScale & 0xffff)
                                        * green * K_INV_255);

                long double lb = (long double)(int)(blueScale & 0xffff)
                                 * blue * K_INV_255;
                blue = (float)lb;  // stored and kept at extended precision

                unsigned int v = ftol8(lb) << blueShift;         // extended
                v += ftol8((long double)green16) << greenShift;  // float32
                v += ftol8((long double)alpha16) << alphaShift;  // float32
                v += ftol8(red) << redShift;                     // extended
                *(unsigned short *)p16 = (unsigned short)v;
            }
            // Any other destination depth writes nothing.

            p32 += 4;
            p16 += 2;
        }
        row += ddsd.lPitch;
    }

    if (buf != NULL)
        delete[] buf;

    if (tmp->Unlock(NULL) < 0) {
        if (tmp != NULL)
            tmp->Release();
        if (fp != NULL) fclose(fp);
        return 0;
    }

    self->textureSurface()->Blt(NULL, tmp, NULL, DDBLT_WAIT, NULL);

    if (tmp != NULL)
        tmp->Release();

    if (fp != NULL) fclose(fp);
    return 1;
}

}  // extern "C"
