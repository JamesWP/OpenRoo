/* The animation codes: the animation an entity is in, which MovableEntity
 * carries in anim_ (movableentity.h) and the renderer looks up in a model's
 * animation table (gfx/ani.h, LookupAnimDescriptor).  They live here, below
 * both, because the simulation sets them and the drawing reads them. */
#pragma once

enum AnimCode {
    ANIM_ICE               = 0x03,
    ANIM_SLIDE             = 0x04,
    ANIM_PARAGLIDE         = 0x05,
    ANIM_FALL              = 0x08,
    ANIM_GLUE              = 0x09,
    ANIM_GHOST             = 0x0a,
    ANIM_JUMP              = 0x0b,
    ANIM_WALK_FORWARD      = 0x14,
    ANIM_WALK_BACKWARD     = 0x15,
    ANIM_FIELD_STAIR_UP    = 0x16,
    ANIM_FIELD_STAIR_DOWN  = 0x17,
    ANIM_STAIR_STAIR_UP    = 0x18,
    ANIM_STAIR_STAIR_DOWN  = 0x19,
    ANIM_STAIR_FIELD_UP    = 0x1a,
    ANIM_STAIR_FIELD_DOWN  = 0x1b,
    ANIM_TURN_RIGHT        = 0x1e,
    ANIM_TURN_LEFT         = 0x1f,
    ANIM_IDLE1             = 0xfa,
    ANIM_IDLE2             = 0xfb
};
