#pragma once
#include <stdint.h>
#include <stddef.h>
#include "scenetexture.h"

/* A theme record's draw state for one sub-object: blend, texture address mode,
 * visibility condition and an animated effect, as the theme file's depth-2 and
 * depth-3 keywords set them. */
enum SceneSubObjectEffect : uint32_t {
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
    uint32_t  dwVisibilityGate;
    SceneTexture *pTexture;
    uint32_t  dwBlendSrc;       // the SRCBLEND value
    uint32_t  dwBlendDst;       // the DESTBLEND value
    uint32_t  dwTexAddress;     // TEXTUREADDRESSU/V; 0 means 3
    SceneSubObjectEffect effect;
    float  flEffectParams[3];
};
