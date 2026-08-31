#pragma once
#define DIRECTDRAW_VERSION 0x0100
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stddef.h>
#include "texture.h"   /* SceneTexture (28-byte extended LoadedImage) */

/* SkyBackground — the object DrawSkyBackground (0x43cc00) is called on.
 * Derived from the EBP-relative offsets in the original (0x20 textures,
 * 0xb0 vertices, 0x3b0 matrix); the fields tile exactly with no gaps, which
 * is what confirms the layout.  See sky.cpp for the EBP == this argument. */
struct SkyBackground {
    DWORD           field0;         // +0x000
    float           flYawAngle;     // +0x004 radians; drives the Y rotation
    SceneTexture    Textures[6];    // +0x008 .. +0x0b0 (stride 0x1c)
    BYTE            QuadVerts[6 * 0x80]; // +0x0b0 .. +0x3b0  6 quads, 4 verts, FVF 0x1e2
    float           WorldMatrix[16];     // +0x3b0 .. +0x3f0
};

static_assert(offsetof(SkyBackground, flYawAngle)  == 0x004, "SkyBackground layout");
static_assert(offsetof(SkyBackground, Textures)    == 0x008, "SkyBackground layout");
static_assert(offsetof(SkyBackground, QuadVerts)   == 0x0b0, "SkyBackground layout");
static_assert(offsetof(SkyBackground, WorldMatrix) == 0x3b0, "SkyBackground layout");
static_assert(sizeof(SkyBackground) == 0x3f0, "SkyBackground size mismatch");
