/* LiftObject: the rising and falling platform.  Every function that touches a
 * LiftObject field is in liftobject.cpp; the fields are private.  The layout
 * is fixed: the renderer reads each lift's position directly. */

#pragma once

#include "layout.h"
#include "game.h"

class CStaticSoundbuffer;

class __attribute__((packed)) LiftObject {
public:
    static const int ORIGIN = 0;

    // Spawns a lift; the arguments are masked to bytes.
    static void spawn(Game *game, unsigned int uArg, unsigned int vArg,
                      unsigned int baseArg, unsigned int topArg);

    // Destroys every lift and zeroes the count.
    static void purgeAll(Game *game);

    // One tick.
    void tick();

    // The moving-loop sound, attached by InitLevelBasedSounds
    // (levelsounds.cpp).
    void setSound(CStaticSoundbuffer *p) { sound_ = p; }

    // Where it is drawn (LevelPlacements_DrawLifts): u, live height, v.
    float posU() const   { return posU_; }
    float height() const { return height_; }
    float posV() const   { return posV_; }

private:

    // The one-slot vtable: the scalar deleting destructor (bit 0 of flags
    // frees the memory).
    struct Vtbl {
        void *(__attribute__((thiscall)) *scalarDeletingDtor)(LiftObject *self,
                                                              unsigned int flags);
    };
    static const Vtbl VTABLE;

    // Allocates and constructs one; NULL if the allocation fails.
    static LiftObject *create();
    LiftObject();
    // Vtable slot 0.
    static void *__attribute__((thiscall)) scalarDeletingDtor(LiftObject *self,
                                                              unsigned int flags);
    // Destroys through the object's own vtable, flags 1.
    void destroy();

    KAROO_LAYOUT_REGISTER(LiftObject);

    const Vtbl         *vtable_;      // +0x00  &VTABLE
    double              now_;         // +0x04  latched from *clock_
    double             *clock_;       // +0x0c  Game::clock()
    TickStep        *tickStep_;       // +0x10  Game::tickStep()
    unsigned char       field_14;     // +0x14
    TickStep         tickStepCopy_;   // +0x15  copied from *tickStep_
    unsigned char       field_1d[8];  // +0x1d
    float               posU_;        // +0x25
    float               height_;      // +0x29  the live height
    float               posV_;        // +0x2d
    signed char         cellU_;       // +0x31
    signed char         cellV_;       // +0x32
    signed char         heightCell_;  // +0x33  the live height, truncated
    unsigned char      *tileBase_;    // +0x34  Game::tileBase()
    signed char         baseHeight_;  // +0x38
    signed char         topHeight_;   // +0x39
    CStaticSoundbuffer *sound_;       // +0x3a  the moving loop; may be NULL
    unsigned char       slot_;        // +0x3e  its index in Game's lift slots
    int                 atTop_;       // +0x3f  1: parked at the top
    signed char         state_;       // +0x43  0 parked, 1 rising, 2 falling
    double              phaseStart_;  // +0x44
};

KAROO_LAYOUT_CHECKS(LiftObject)
{
    KAROO_LAYOUT_AT(now_,        0x04);
    KAROO_LAYOUT_AT(clock_,      0x0c);
    KAROO_LAYOUT_AT(tickStep_,     0x10);
    KAROO_LAYOUT_AT(tickStepCopy_, 0x15);
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

/* No size check: we allocate it, so nothing relies on its size. */
}

