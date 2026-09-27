/* LiftObject: the rising and falling platform.  Every function that touches a
 * LiftObject field is in liftobject.cpp; the fields are private.  The layout
 * is fixed: the renderer reads each lift's position directly. */

#pragma once

 
#include "game.h"

class CStaticSoundbuffer;

class LiftObject {
public:
     

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

    // Where it is drawn (LevelPlacements::drawLifts): u, live height, v.
    float posU() const   { return posU_; }
    float height() const { return height_; }
    float posV() const   { return posV_; }

private:

    // The one-slot vtable: the scalar deleting destructor (bit 0 of flags
    // frees the memory).
    struct Vtbl {
        void *(  *scalarDeletingDtor)(LiftObject *self,
                                                              unsigned int flags);
    };
    static const Vtbl VTABLE;

    // Allocates and constructs one; NULL if the allocation fails.
    static LiftObject *create();
    LiftObject();
    // Vtable slot 0.
    static void *  scalarDeletingDtor(LiftObject *self,
                                                              unsigned int flags);
    // Destroys through the object's own vtable, flags 1.
    void destroy();

    const Vtbl         *vtable_;      // &VTABLE
    double              now_;         // latched from *clock_
    double             *clock_;       // Game::clock()
    TickStep        *tickStep_;       // Game::tickStep()
    unsigned char       field_14;
    TickStep         tickStepCopy_;   // copied from *tickStep_
    unsigned char       field_1d[8];
    float               posU_;
    float               height_;      // the live height
    float               posV_;
    signed char         cellU_;
    signed char         cellV_;
    signed char         heightCell_;  // the live height, truncated
    Tile               *tileBase_;    // Game::tileBase()
    signed char         baseHeight_;
    signed char         topHeight_;
    CStaticSoundbuffer *sound_;       // the moving loop; may be NULL
    unsigned char       slot_;        // its index in Game's lift slots
    int                 atTop_;       // 1: parked at the top
    signed char         state_;       // 0 parked, 1 rising, 2 falling
    double              phaseStart_;
};

 