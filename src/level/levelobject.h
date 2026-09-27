#pragma once
#include <windows.h>
#include <stddef.h>
#include "texture.h"

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

static_assert(offsetof(SceneSubObject, pTexture)     == 0x04, "SceneSubObject layout");
static_assert(offsetof(SceneSubObject, dwBlendSrc)   == 0x0c, "SceneSubObject layout");
static_assert(offsetof(SceneSubObject, dwBlendDst)   == 0x10, "SceneSubObject layout");
static_assert(offsetof(SceneSubObject, dwTexAddress) == 0x14, "SceneSubObject layout");
static_assert(offsetof(SceneSubObject, effect)       == 0x18, "SceneSubObject layout");
static_assert(offsetof(SceneSubObject, flEffectParams) == 0x1c, "SceneSubObject layout");
static_assert(sizeof(SceneSubObject) == 0x3c, "SceneSubObject stride mismatch");

