#pragma once
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stddef.h>
#include "texture.h"   /* SceneTexture (28-byte extended LoadedImage) */

/* SkyBackground — the object DrawSkyBackground (0x43cc00) is called on.
 * Derived from the EBP-relative offsets in the original (0x20 textures,
 * 0xb0 vertices, 0x3b0 matrix); the fields tile exactly with no gaps, which
 * is what confirms the layout.  See sky.cpp for the EBP == this argument. */
/* One sky vertex, FVF 0x1e2 = XYZ | RESERVED1 | DIFFUSE | SPECULAR | TEX1.
 * RESERVED1 is the dword after the position; nothing writes it. */
struct SkyVertex {
    float x, y, z;
    DWORD reserved;
    DWORD diffuse;
    DWORD specular;
    float u, v;
};
static_assert(sizeof(SkyVertex) == 0x20, "SkyVertex stride");

struct SkyBackground {
    const void     *pVtable;        // +0x000 our one-slot table (game's 0x45d6fc)
    float           flYawAngle;     // +0x004 radians; drives the Y rotation
    SceneTexture    Textures[6];    // +0x008 .. +0x0b0 (stride 0x1c)
    SkyVertex       QuadVerts[6][4];     // +0x0b0 .. +0x3b0  one strip per face
    float           WorldMatrix[16];     // +0x3b0 .. +0x3f0
};

static_assert(offsetof(SkyBackground, flYawAngle)  == 0x004, "SkyBackground layout");
static_assert(offsetof(SkyBackground, Textures)    == 0x008, "SkyBackground layout");
static_assert(offsetof(SkyBackground, QuadVerts)   == 0x0b0, "SkyBackground layout");
static_assert(offsetof(SkyBackground, WorldMatrix) == 0x3b0, "SkyBackground layout");
static_assert(sizeof(SkyBackground) == 0x3f0, "SkyBackground size mismatch");

/* 0x0043c870 -- fill the vertices and matrix, then load the six faces
 * (UP, DN, FR, BK, LF, RT) through SelectTextureLoader in mode 0.  Stops at
 * the first face that fails; the low byte of the result is the flag.  Its
 * one caller is the theme loader's `sky` keyword. */
struct IDirectDraw4;
struct IDirect3DDevice3;
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sky_BuildFromFaceNames(SkyBackground *self, IDirectDraw4 *dd,
                       IDirect3DDevice3 *dev, const char *up, const char *dn,
                       const char *fr, const char *bk, const char *lf,
                       const char *rt, UINT bpp);

/* The lifecycle of the one instance, ThemeAssetBlock::sky (sky.cpp). */
extern "C" __declspec(dllexport) SkyBackground *__attribute__((thiscall))
Sky_Construct(SkyBackground *self);                          /* 0x0043c560 */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sky_DtorBody(SkyBackground *self);                           /* 0x0043c850 */
extern "C" __declspec(dllexport) SkyBackground *__attribute__((thiscall))
Sky_ScalarDtor(SkyBackground *self, unsigned int flags);     /* 0x0043c830 */
