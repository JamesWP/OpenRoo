#pragma once

/* The 18-byte TGA header, in file order.  Both TGA paths read it field by
 * field (twelve separate freads, not one block read):
 * TextureTGA_Parse (texturetga.cpp) and ImportSceneTextures
 * (scenetexture.cpp). */
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
};
