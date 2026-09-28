#pragma once
#include <windows.h>
#include <stddef.h>
#include "scenetexture.h"

/* A theme record's draw state for one sub-object: blend, texture address mode,
 * visibility condition and an animated effect, as the theme file's depth-2 and
 * depth-3 keywords set them. */
enum SceneSubObjectEffect : DWORD {
    SUBOBJ_EFFECT_NONE        = 0,
    SUBOBJ_EFFECT_FLASH       = 1,  // 3 params
    SUBOBJ_EFFECT_PULSE       = 2,  // 2 params
    SUBOBJ_EFFECT_TURN        = 3,  // 1 param
    SUBOBJ_EFFECT_WOBBLE      = 4,  // 3 params
    SUBOBJ_EFFECT_ENVIRONMENT = 5,  // none
    SUBOBJ_EFFECT_SCROLL      = 6,  // 2 params
};

/* dwVisibilityGate is the theme's `condition`: active 1, inactive 2, dead 3,
 * alive 4, paraglide 5, protection 6. */
struct SceneSubObject {
    DWORD  dwVisibilityGate;
    SceneTexture *pTexture;
    DWORD  unknown08;        // never written
    DWORD  dwBlendSrc;       // the SRCBLEND value
    DWORD  dwBlendDst;       // the DESTBLEND value
    DWORD  dwTexAddress;     // TEXTUREADDRESSU/V; 0 means 3
    SceneSubObjectEffect effect;
    float  flEffectParams[3];
    BYTE   pad28[0x14];  // never written
};

/* Raw access to theme records (ThemeLevelObject, theme.h) for the quad batch
 * path. */
#define LOBJ_STRIDE            0x5dd
#define LOBJ_OFF_DRAWKIND      0x000  // == 2 selects the quad-batch draw
#define LOBJ_OFF_SUBOBJCOUNT   0x3c1
#define LOBJ_OFF_SUBOBJECTS    0x3c5

/* The quad batch draws the theme block's SIDE slot: its record count, then its
 * records. */
#define GAME_OFF_QUAD_COUNT    0x11aa8
#define GAME_OFF_QUAD_OBJECTS  0x11aac

static inline BYTE *lobj_at(void *game, unsigned i)
{
    return (BYTE *)game + GAME_OFF_QUAD_OBJECTS + i * LOBJ_STRIDE;
}
