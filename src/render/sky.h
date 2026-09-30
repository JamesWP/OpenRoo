/* SkyBackground: the six-faced cube of sky textures drawn behind a level,
 * yaw-rotated and re-centred on the viewer every frame.  ThemeAssetBlock owns
 * the one instance per theme (sky.cpp has its lifecycle and the draw). */

#pragma once
#include <windows.h>
#include <stddef.h>
#include "scenetexture.h"
class RenderDevice;

/* One sky-cube vertex, FVF 0x1e2 = XYZ | RESERVED1 | DIFFUSE | SPECULAR |
 * TEX1. */
struct SkyVertex {
    float x, y, z;
    DWORD reserved;  // never written
    DWORD diffuse;
    DWORD specular;
    float u, v;
};

/* One cube of six faces, four vertices each, plus the world matrix
 * DrawSkyBackground rebuilds from flYawAngle and the viewer position every
 * call. */
class SkyBackground {
public:
    /* Fills the geometry and matrix, then loads the six faces (UP, DN, FR, BK,
     * LF, RT) through the texture loader.  Stops at the first face that fails
     * to load; the low byte of the result is 0 on failure, 1 on success. */
    unsigned int buildFromFaceNames(RenderDevice *dev, const char *up,
                                    const char *dn, const char *fr,
                                    const char *bk, const char *lf,
                                    const char *rt, UINT bpp);

    /* Builds the six face textures, then fills the geometry; the destructor
     * releases the faces last to first. */
    SkyBackground();
    ~SkyBackground();
    SkyBackground(const SkyBackground &) = delete;
    SkyBackground &operator=(const SkyBackground &) = delete;

    /* Rebuilds the world matrix from flYawAngle and the given centre, submits
     * the six faces, and returns the matrix (self->WorldMatrix). */
    float *draw(RenderDevice *dev, float flCentreX, float flCentreY,
                float flCentreZ);

 
 
    SceneTexture       *textures()       { return Textures_; }
    const SceneTexture *textures() const { return Textures_; }
 

private:
    void skyFillGeometry();

 

    float           flYawAngle_;       // +0x004 radians, the Y rotation
    SceneTexture    Textures_[6];      // +0x008 one SceneTexture per face
    SkyVertex       QuadVerts_[6][4];  // +0x0b0 one triangle-strip quad per face
    float           WorldMatrix_[16];  // +0x3b0 rebuilt every draw call
};

