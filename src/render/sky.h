/* SkyBackground: the six-faced cube of sky textures drawn behind a level,
 * yaw-rotated and re-centred on the viewer every frame.  ThemeAssetBlock owns
 * the one instance per theme (sky.cpp has its lifecycle and the draw).  The
 * vertex and matrix layouts below are load-bearing: the struct must tile
 * exactly to the sizes the static_asserts check. */

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
static_assert(sizeof(SkyVertex) == 0x20, "SkyVertex stride");

/* One cube of six faces, four vertices each, plus the world matrix
 * DrawSkyBackground rebuilds from flYawAngle and the viewer position every
 * call. */
class __attribute__((packed)) SkyBackground {
public:
    /* Fills the geometry and matrix, then loads the six faces (UP, DN, FR, BK,
     * LF, RT) through the texture loader.  Stops at the first face that fails
     * to load; the low byte of the result is 0 on failure, 1 on success. */
    unsigned int buildFromFaceNames(RenderDevice *dev, const char *up,
                                    const char *dn, const char *fr,
                                    const char *bk, const char *lf,
                                    const char *rt, UINT bpp);

    /* Construct, destroy and destroy-and-free, matching the vtable's one slot.
     */
    SkyBackground *construct();

    void dtorBody();
    static SkyBackground * 
    scalarDtor(SkyBackground *self, unsigned int flags);

    /* Rebuilds the world matrix from flYawAngle and the given centre, submits
     * the six faces, and returns the matrix (self->WorldMatrix). */
    float *draw(RenderDevice *dev, float flCentreX, float flCentreY,
                float flCentreZ);

    const void     *vtable() const { return pVtable_; }

    /* The six faces.  4-aligned within the packed sky. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    SceneTexture       *textures()       { return Textures_; }
    const SceneTexture *textures() const { return Textures_; }
#pragma GCC diagnostic pop

private:
    void skyFillGeometry();

    static void checkLayout();

    const void     *pVtable_;          // +0x000 one-slot vtable
    float           flYawAngle_;       // +0x004 radians, the Y rotation
    SceneTexture    Textures_[6];      // +0x008 one SceneTexture per face
    SkyVertex       QuadVerts_[6][4];  // +0x0b0 one triangle-strip quad per face
    float           WorldMatrix_[16];  // +0x3b0 rebuilt every draw call
};

inline void SkyBackground::checkLayout()
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(SkyBackground, flYawAngle_)  == 0x004, "SkyBackground layout");
    static_assert(offsetof(SkyBackground, Textures_)    == 0x008, "SkyBackground layout");
    static_assert(offsetof(SkyBackground, QuadVerts_)   == 0x0b0, "SkyBackground layout");
    static_assert(offsetof(SkyBackground, WorldMatrix_) == 0x3b0, "SkyBackground layout");
#pragma GCC diagnostic pop
}

static_assert(sizeof(SkyBackground) == 0x3f0, "SkyBackground size mismatch");

