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
struct __attribute__((packed)) SkyBackground {
    const void     *pVtable;          // +0x000 one-slot vtable
    float           flYawAngle;       // +0x004 radians, the Y rotation
    SceneTexture    Textures[6];      // +0x008 one SceneTexture per face
    SkyVertex       QuadVerts[6][4];  // +0x0b0 one triangle-strip quad per face
    float           WorldMatrix[16];  // +0x3b0 rebuilt every draw call
};

static_assert(offsetof(SkyBackground, flYawAngle)  == 0x004, "SkyBackground layout");
static_assert(offsetof(SkyBackground, Textures)    == 0x008, "SkyBackground layout");
static_assert(offsetof(SkyBackground, QuadVerts)   == 0x0b0, "SkyBackground layout");
static_assert(offsetof(SkyBackground, WorldMatrix) == 0x3b0, "SkyBackground layout");
static_assert(sizeof(SkyBackground) == 0x3f0, "SkyBackground size mismatch");

/* Fills the geometry and matrix, then loads the six faces (UP, DN, FR, BK, LF,
 * RT) through the texture loader.  Stops at the first face that fails to load;
 * the low byte of the result is 0 on failure, 1 on success. */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sky_BuildFromFaceNames(SkyBackground *self, RenderDevice *dev, const char *up, const char *dn,
                       const char *fr, const char *bk, const char *lf,
                       const char *rt, UINT bpp);

/* Construct, destroy and destroy-and-free, matching the vtable's one slot. */
extern "C" __declspec(dllexport) SkyBackground *__attribute__((thiscall))
Sky_Construct(SkyBackground *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sky_DtorBody(SkyBackground *self);
extern "C" __declspec(dllexport) SkyBackground *__attribute__((thiscall))
Sky_ScalarDtor(SkyBackground *self, unsigned int flags);

/* Rebuilds the world matrix from flYawAngle and the given centre, submits the
 * six faces, and returns the matrix (self->WorldMatrix). */
extern "C" __declspec(dllexport) float * __attribute__((thiscall))
Sky_DrawSkyBackground(SkyBackground *self, RenderDevice *dev,
                      float flCentreX, float flCentreY, float flCentreZ);
