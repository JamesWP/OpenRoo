#pragma once
#include <stddef.h>

/* The 18-byte TGA header, in the original's field order and sizes.  Both TGA
 * paths read it field by field (twelve separate freads, not one block read):
 * TextureTGA_Parse (texturetga.cpp) and ImportSceneTextures (scenetexture.cpp,
 * frame slot B+0x1c). */
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
