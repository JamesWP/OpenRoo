/* LiftObject: the rising and falling platform.  Every function that touches a
 * LiftObject field is in liftobject.cpp; the fields are private.  The layout
 * is fixed: the renderer reads each lift's position directly. */

#pragma once
#include "entitycontext.h"
#include "entitylist.h"
#include "playevents.h"
#include "tickstep.h"
#include "drawfwd.h"

 

class LiftObject : public Listed {
public:
     

    // Spawns a lift; the arguments are masked to bytes.
    static void spawn(const EntityContext &ctx, EntityList<LiftObject> &lifts, unsigned int uArg,
                      unsigned int vArg, unsigned int baseArg, unsigned int topArg);

    // One tick; its moving loop starts, follows it and stops through events.
    void tick(PlayEvents &events);

    // Where it stands: u, live height, v.
    float posU() const   { return posU_; }
    float height() const { return height_; }
    float posV() const   { return posV_; }

    // Its cell, its two parking heights and where it is in its cycle: state
    // 0 parked, 1 rising, 2 falling; atTop whether it last parked at the top;
    // the clock when the phase began.
    int    cellU() const      { return cellU_; }
    int    cellV() const      { return cellV_; }
    int    baseHeight() const { return baseHeight_; }
    int    topHeight() const  { return topHeight_; }
    int    state() const      { return state_; }
    bool   atTop() const      { return atTop_ != 0; }
    double phaseStart() const { return phaseStart_; }

    // Draws the lift, in the opaque pass.
    void draw(draw::Frame &f, draw::Pass pass) const;

    ~LiftObject();

private:
    LiftObject();
    LiftObject(const LiftObject &) = delete;
    LiftObject &operator=(const LiftObject &) = delete;

    double              now_;         // latched from *clock_
    double             *clock_;       // World::clock()
    TickStep        *tickStep_;       // World::tickStep()
    unsigned char       field_14;
    TickStep         tickStepCopy_;   // copied from *tickStep_
    unsigned char       field_1d[8];
    float               posU_;
    float               height_;      // the live height
    float               posV_;
    signed char         cellU_;
    signed char         cellV_;
    signed char         heightCell_;  // the live height, truncated
    LevelMap               *map_;    // World::map()
    signed char         baseHeight_;
    signed char         topHeight_;
    unsigned char       slot_;        // its index in World's lift list
    int                 atTop_;       // 1: parked at the top
    signed char         state_;       // 0 parked, 1 rising, 2 falling
    double              phaseStart_;
};

 