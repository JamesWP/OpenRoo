#pragma once
#define DIRECTDRAW_VERSION 0x0100
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stddef.h>

/* SceneSubObject — per-draw-call blend/texture state, 60 bytes, stored inline
 * in LevelObject at +0x3c5.  Layout from HOOKS.md § SceneSubObject, with
 * dwTexAddress (+0x14) added here: DrawQuadBatch (0x408710) feeds it to
 * D3DRENDERSTATE_TEXTUREADDRESSU/V, substituting 3 (D3DTADDRESS_CLAMP) when
 * it is zero.  HOOKS.md had +0x14 inside an unknown pad. */
struct SceneSubObject {
    DWORD  dwVisibilityGate; // +0x00
    void  *pTexture;         // +0x04  SetTexture(0, *(pTexture+0x18))
    DWORD  unknown08;        // +0x08
    DWORD  dwBlendSrc;       // +0x0c  D3DRENDERSTATE_SRCBLEND value
    DWORD  dwBlendDst;       // +0x10  D3DRENDERSTATE_DESTBLEND value
    DWORD  dwTexAddress;     // +0x14  TEXTUREADDRESSU/V; 0 means "use 3"
    BYTE   pad18[0x24];      // +0x18..+0x3b
};

static_assert(offsetof(SceneSubObject, pTexture)     == 0x04, "SceneSubObject layout");
static_assert(offsetof(SceneSubObject, dwBlendSrc)   == 0x0c, "SceneSubObject layout");
static_assert(offsetof(SceneSubObject, dwBlendDst)   == 0x10, "SceneSubObject layout");
static_assert(offsetof(SceneSubObject, dwTexAddress) == 0x14, "SceneSubObject layout");
static_assert(sizeof(SceneSubObject) == 0x3c, "SceneSubObject stride mismatch");

/* LevelObject — 0x5dd-byte entry in the flat array loaded by ThemeFileLoader.
 * Only the fields the quad-batch path touches are named; the struct is far
 * from fully decoded, so it is accessed through byte offsets rather than a
 * declared 1501-byte type. */
#define LOBJ_STRIDE            0x5dd
#define LOBJ_OFF_DRAWKIND      0x000  /* dwType; == 2 selects the quad-batch draw.
                                        * The decompile reads this as
                                        * *(puVar4 - 0x3c1) where puVar4 is
                                        * &obj->dwSubObjectCount (obj+0x3c1),
                                        * i.e. obj+0x00 — not obj+0x04. */
#define LOBJ_OFF_SUBOBJCOUNT   0x3c1
#define LOBJ_OFF_SUBOBJECTS    0x3c5

/* The quad-batch array lives in the Game object: a DWORD count immediately
 * followed by the objects.  Both offsets are read straight out of the
 * DrawQuadBatch decompile (count at Game+0x11aa8; the inner cursor starts at
 * Game+0x11e6d, which is LevelObject[0]+0x3c1, putting object 0 at +0x11aac). */
#define GAME_OFF_QUAD_COUNT    0x11aa8
#define GAME_OFF_QUAD_OBJECTS  0x11aac

static inline BYTE *lobj_at(void *game, unsigned i)
{
    return (BYTE *)game + GAME_OFF_QUAD_OBJECTS + i * LOBJ_STRIDE;
}
