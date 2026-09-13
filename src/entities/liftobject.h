/* LiftObject -- the rising/falling platform.  Ghidra struct `LiftObject`.
 *
 * Every function that touches a LiftObject field is in liftobject.cpp:
 * spawn (0x417b90), tick (0x411cb0) and purge (0x417d90).  The fields are
 * private, so the compiler enforces that -- code elsewhere reaches a lift
 * only through the public methods.
 *
 * Construction and destruction are ours too: create() allocates with our own
 * `new` (we both create and destroy every lift -- COHESION_PLAN.md template
 * 6) and the constructor installs OUR vtable, whose one slot is an
 * MSVC-shaped scalar deleting destructor.  Nothing depends on the game's
 * vtable 0x45d380 or on patch.py rewriting a slot; the three originals
 * (0x411c50, 0x411c80, 0x411ca0) are UD2-stubbed.
 *
 * The layout is still the game's: the renderer (FUN_00408870, original)
 * reads every lift's +0x25/+0x29/+0x2d directly.  So the class stays packed
 * and every offset is asserted, until that reader is ours too
 * (COHESION_PLAN.md, "When may a class own its layout?").
 */
#pragma once

#include "layout.h"
#include "game.h"

struct CStaticSoundbuffer;

class __attribute__((packed)) LiftObject {
public:
    static const int ORIGIN = 0;

    /* Game::SpawnLiftObject 0x00417b90.  Arguments are dwords masked to
     * bytes, exactly as the original reads them. */
    static void spawn(Game *game, unsigned int uArg, unsigned int vArg,
                      unsigned int baseArg, unsigned int topArg);

    /* Game::PurgeLiftObjects 0x00417d90 -- destroy every lift and zero the
     * count. */
    static void purgeAll(Game *game);

    /* Game::UpdateVerticalLiftObject 0x00411cb0 -- one tick. */
    void tick();

    /* +0x3a, attached by InitLevelBasedSounds (levelsounds.cpp). */
    void setSound(CStaticSoundbuffer *p) { sound_ = p; }

private:

    /* The vtable.  MSVC layout: one slot, the scalar deleting destructor,
     * __thiscall with a flags argument (bit 0 = free the memory). */
    struct Vtbl {
        void *(__attribute__((thiscall)) *scalarDeletingDtor)(LiftObject *self,
                                                              unsigned int flags);
    };
    static const Vtbl VTABLE;

    /* 0x411c50 -- allocate and construct, with our own new: we both create
     * and destroy every lift.  NULL if allocation fails, as the original's
     * operator new returned NULL. */
    static LiftObject *create();
    LiftObject();
    /* 0x411c80 -- vtable slot 0. */
    static void *__attribute__((thiscall)) scalarDeletingDtor(LiftObject *self,
                                                              unsigned int flags);
    /* Destroy through the object's own vtable, flags 1, as the purge did. */
    void destroy();

    KAROO_LAYOUT_REGISTER(LiftObject);

    const Vtbl         *vtable_;       /* +0x00  &VTABLE                     */
    double              now_;          /* +0x04  latched from *clock_        */
    double             *clock_;        /* +0x0c  Game::clock()               */
    Field170a5c        *record_;       /* +0x10  Game::field_170a5c()        */
    unsigned char       field_14;      /* +0x14                              */
    Field170a5c         recordCopy_;   /* +0x15  copied from *record_        */
    unsigned char       field_1d[8];   /* +0x1d                              */
    float               posU_;         /* +0x25  } base-class fields,        */
    float               height_;       /* +0x29  } zeroed by 0x401000;       */
    float               posV_;         /* +0x2d  } live height at +0x29      */
    signed char         cellU_;        /* +0x31                              */
    signed char         cellV_;        /* +0x32                              */
    signed char         heightCell_;   /* +0x33  live height, truncated      */
    unsigned char      *tileBase_;     /* +0x34  Game::tileBase()            */
    signed char         baseHeight_;   /* +0x38                              */
    signed char         topHeight_;    /* +0x39                              */
    CStaticSoundbuffer *sound_;        /* +0x3a  moving loop, may be NULL    */
    unsigned char       slot_;         /* +0x3e  index in Game's lift slots  */
    int                 atTop_;        /* +0x3f  1 = parked at top           */
    signed char         state_;        /* +0x43  0 parked, 1 rising, 2 falling */
    double              phaseStart_;   /* +0x44                              */
};

KAROO_LAYOUT_CHECKS(LiftObject)
{
    KAROO_LAYOUT_AT(now_,        0x04);
    KAROO_LAYOUT_AT(clock_,      0x0c);
    KAROO_LAYOUT_AT(record_,     0x10);
    KAROO_LAYOUT_AT(recordCopy_, 0x15);
    KAROO_LAYOUT_AT(posU_,       0x25);
    KAROO_LAYOUT_AT(height_,     0x29);
    KAROO_LAYOUT_AT(posV_,       0x2d);
    KAROO_LAYOUT_AT(cellU_,      0x31);
    KAROO_LAYOUT_AT(heightCell_, 0x33);
    KAROO_LAYOUT_AT(tileBase_,   0x34);
    KAROO_LAYOUT_AT(baseHeight_, 0x38);
    KAROO_LAYOUT_AT(topHeight_,  0x39);
    KAROO_LAYOUT_AT(sound_,      0x3a);
    KAROO_LAYOUT_AT(slot_,       0x3e);
    KAROO_LAYOUT_AT(atTop_,      0x3f);
    KAROO_LAYOUT_AT(state_,      0x43);
    KAROO_LAYOUT_AT(phaseStart_, 0x44);
    /* No size check: the object is ours to allocate, so nothing relies on
     * it being the original's 0x4c. */
}
