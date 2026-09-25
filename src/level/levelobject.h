#pragma once
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stddef.h>
#include "texture.h"    /* SceneTexture */

/* SceneSubObject — per-draw-call blend/texture state, 60 bytes, stored inline
 * in LevelObject at +0x3c5.  +0x0c..+0x14 read off DrawQuadBatch (0x408710);
 * the rest from what ThemeFileLoader's depth-2/3 keywords write (theme.h).
 * The loader never writes +0x08 or +0x28..+0x3b. */
enum SceneSubObjectEffect : DWORD {
    SUBOBJ_EFFECT_NONE        = 0,
    SUBOBJ_EFFECT_FLASH       = 1,   /* 3 params */
    SUBOBJ_EFFECT_PULSE       = 2,   /* 2 params */
    SUBOBJ_EFFECT_TURN        = 3,   /* 1 param  */
    SUBOBJ_EFFECT_WOBBLE      = 4,   /* 3 params */
    SUBOBJ_EFFECT_ENVIRONMENT = 5,   /* none     */
    SUBOBJ_EFFECT_SCROLL      = 6,   /* 2 params */
};

struct SceneSubObject {
    DWORD  dwVisibilityGate; // +0x00  `condition`: active 1 inactive 2 dead 3
                             //        alive 4 paraglide 5 protection 6
    SceneTexture *pTexture;  // +0x04  SetTexture(0, pTexture->pTexture2)
    DWORD  unknown08;        // +0x08
    DWORD  dwBlendSrc;       // +0x0c  D3DRENDERSTATE_SRCBLEND value
    DWORD  dwBlendDst;       // +0x10  D3DRENDERSTATE_DESTBLEND value
    DWORD  dwTexAddress;     // +0x14  TEXTUREADDRESSU/V; 0 means "use 3"
    SceneSubObjectEffect effect; // +0x18
    float  flEffectParams[3];    // +0x1c
    BYTE   pad28[0x14];      // +0x28..+0x3b
};

static_assert(offsetof(SceneSubObject, pTexture)     == 0x04, "SceneSubObject layout");
static_assert(offsetof(SceneSubObject, dwBlendSrc)   == 0x0c, "SceneSubObject layout");
static_assert(offsetof(SceneSubObject, dwBlendDst)   == 0x10, "SceneSubObject layout");
static_assert(offsetof(SceneSubObject, dwTexAddress) == 0x14, "SceneSubObject layout");
static_assert(offsetof(SceneSubObject, effect)       == 0x18, "SceneSubObject layout");
static_assert(offsetof(SceneSubObject, flEffectParams) == 0x1c, "SceneSubObject layout");
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
