/* BridgeObject: a switch-operated bridge.  A switch arms it, and while armed
 * it extends, or retracts, one deck cell at a time, stamping each cell it
 * covers.  Every function that touches a BridgeObject field is in
 * bridgeobject.cpp: the spawn, tick and sound release, and
 * the switch accessors GameTick and tilequery.cpp use. */

#pragma once
#include <stdint.h>
#include "entitycontext.h"
#include "entitylist.h"
#include "playevents.h"
#include "tickstep.h"

 


/* A bridge's extent for the debug map, in cell-edge coordinates: the deck
 * runs along its axis from `rest` (the anchor) towards `rest + step * span`,
 * and currently reaches `reach`.  The cross axis is the anchor cell's. */
struct BridgeExtent {
    int   axis;       // 1 along U, 2 along V
    float restU, restV;
    int   step, span;
    float reachU, reachV;
    int   slot;       // the switch that operates it
    bool  armed;      // moving
    int   phase;      // 0: extends next; 1: retracts next
    int   height;     // the deck's
};

class SwitchCells;

class BridgeObject : public Listed {
public:
     

    // Spawns a bridge; the arguments are masked to bytes.
    static void spawn(const EntityContext &ctx, EntityList<BridgeObject> &bridges, unsigned int uArg, unsigned int vArg,
                      unsigned int heightArg, unsigned int slotArg,
                      unsigned int axisArg);

    // One tick; its moving loop follows it and stops through events.
    void tick(PlayEvents &events);

    // Draws it on the debug map, with its switch links (dbg.h).
    void debugDraw(const SwitchCells &sc) const;

    // When the move under way began, clock ms.
    double phaseStart() const        { return phaseStart_; }
    // The switch (GameTick, tilequery.cpp).
    // Nonzero while a switch has set it moving.
    int  armed() const               { return armed_; }
    // 0: the next move extends; 1: it retracts.
    int  phase() const               { return phase_; }
    // The switch that operates it (World::bridge finds it by this).
    unsigned char switchSlot() const { return slot_; }
    // A switch fires: arm, and start the phase at the clock.
    void arm(double now)             { armed_ = 1; phaseStart_ = now; }
    // The player's switch also starts the moving loop.
    void playArmSound(PlayEvents &events);

    // The deck's live far point, its anchor, its length in cells, its
    // direction (+1 or -1 cell a step) and its axis (1 along U, 2 along V).
    float posU() const               { return posU_; }
    float posY() const               { return posY_; }
    float posV() const               { return posV_; }
    float restU() const              { return restU_; }
    float restY() const              { return restY_; }
    float restV() const              { return restV_; }
    signed char span() const         { return span_; }
    signed char step() const         { return step_; }
    unsigned char axis() const       { return axis_; }

    // A read-only view for the debug map.
    BridgeExtent extent() const
    {
        return { axis_, restU_, restV_, step_, span_, posU_, posV_, slot_, armed_ != 0, phase_, height_ };
    }

    ~BridgeObject();

private:
    BridgeObject();
    BridgeObject(const BridgeObject &) = delete;
    BridgeObject &operator=(const BridgeObject &) = delete;

    double              now_;         // latched from *clock_
    double             *clock_;       // World::clock()
    TickStep        *tickStep_;       // World::tickStep()
    unsigned char       field_14;
    TickStep         tickStepCopy_;   // copied from *tickStep_
    unsigned char       field_1d[8];
    float               posU_;        // the live position:
    float               posY_;        // the deck's far point
    float               posV_;
    signed char         cellU_;
    signed char         cellV_;
    signed char         heightCell_;
    LevelMap               *map_;    // World::map()
    unsigned char       guard_;       // the far end; read signed by the tick
    float               restU_;       // the anchor, where the deck
    float               restY_;       // starts
    float               restV_;
    signed char         span_;        // the deck's length, in cells
    unsigned char       slot_;        // the switch slot, stamped into cells
    double              phaseStart_;
    int                 armed_;
    signed char         step_;        // +1 or -1 cell per 100 ms
    int                 phase_;       // 0 extend next, 1 retract
    unsigned char       tileHeight_;  // stamped into the tile
    unsigned char       endU_;        // the scan's start cell
    unsigned char       endV_;
    unsigned char       height_;
    unsigned char       axis_;        // 1 along U, 2 along V
};
