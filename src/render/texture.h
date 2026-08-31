#pragma once
#include "direct3d.h"   /* LoadedImage (the 24-byte base variant) */

/* SceneTexture — the 28-byte extended LoadedImage variant: the base struct
 * plus an IDirect3DTexture2 at +0x18.  See HOOKS.md § LoadedImage struct —
 * two sizes.  The two variants really are distinct objects: CreateSurfaceDIB
 * and friends operate on 24-byte instances that have no +0x18 slot at all, so
 * anything touching pTexture2 must be handed the extended type. */
struct SceneTexture {
    LoadedImage        base;       // +0x00 (24 bytes)
    IDirect3DTexture2 *pTexture2;  // +0x18
};

static_assert(offsetof(SceneTexture, pTexture2) == 0x18, "SceneTexture layout");
static_assert(sizeof(SceneTexture) == 0x1c, "SceneTexture stride mismatch");
