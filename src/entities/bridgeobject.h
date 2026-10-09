/* BridgeObject: a switch-operated bridge.  A switch arms it, and while armed
 * it extends, or retracts, one deck cell at a time, stamping each cell it
 * covers.  Every function that touches a BridgeObject field is in
 * bridgeobject.cpp: the spawn, tick and purge, the deck quad
 * DrawBridgeSurfaces draws (bridgesurf.cpp), and the switch accessors GameTick
 * and tilequery.cpp use. */

#pragma once

 
#include "game.h"

namespace audiodev { class Buffer; }

/* An FVF 0x242 vertex: XYZ, diffuse, two texture-coordinate sets. */
struct BridgeVertex {
    float x, y, z;
    uint32_t diffuse;
    float u0, v0;
    float u1, v1;
};

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
};

/* What buildSurface() computed, for bridgesurf.cpp's logs. */
struct BridgeSurfaceInfo {
    int   axis, dir, n;
    float len, f;
};

class BridgeObject {
public:
     

    // Spawns a bridge; the arguments are masked to bytes.
    static void spawn(Game *game, unsigned int uArg, unsigned int vArg,
                      unsigned int heightArg, unsigned int slotArg,
                      unsigned int axisArg);

    // Destroys every bridge and zeroes the count.
    static void purgeAll(Game *game);

    // One tick.
    void tick();

    // Draws it on the debug map, with its switch links (dbg.h).
    void debugDraw() const;

    // The switch (GameTick, tilequery.cpp).
    // Nonzero while a switch has set it moving.
    int  armed() const               { return armed_; }
    // 0: the next move extends; 1: it retracts.
    int  phase() const               { return phase_; }
    // A switch fires: arm, and start the phase at the clock.
    void arm(const double *clock)    { armed_ = 1; phaseStart_ = *clock; }
    // The player's switch also starts the moving-loop sound.
    void playArmSound();

    // The renderer (bridgesurf.cpp).
    // Builds this bridge's scrolling deck quad for animation time t (ms).
    // False, with v untouched, when the bridge is not drawn (never armed and
    // in phase 0).  backward is KAROO_BRIDGE_FX=backward.  The diffuse is left
    // 0xFFFFFFFF for the caller to overwrite.
    bool buildSurface(BridgeVertex v[4], double t, bool backward,
                      BridgeSurfaceInfo *info) const;

    // A read-only view for the debug map.
    BridgeExtent extent() const
    {
        return { axis_, restU_, restV_, step_, span_, posU_, posV_, slot_, armed_ != 0, phase_ };
    }

    // The moving-loop sound, attached by InitLevelBasedSounds
    // (levelsounds.cpp).
    void setSound(audiodev::Buffer *p) { sound_ = p; }

private:

    // Allocates and constructs one; NULL if the allocation fails.
    static BridgeObject *create();
    BridgeObject();
    virtual ~BridgeObject();
    BridgeObject(const BridgeObject &) = delete;
    BridgeObject &operator=(const BridgeObject &) = delete;

    double              now_;         // latched from *clock_
    double             *clock_;       // Game::clock()
    TickStep        *tickStep_;       // Game::tickStep()
    unsigned char       field_14;
    TickStep         tickStepCopy_;   // copied from *tickStep_
    unsigned char       field_1d[8];
    float               posU_;        // the live position:
    float               posY_;        // the deck's far point
    float               posV_;
    signed char         cellU_;
    signed char         cellV_;
    signed char         heightCell_;
    LevelMap               *map_;    // Game::map()
    unsigned char       guard_;       // the far end; read signed by the tick
    float               restU_;       // the anchor, where the deck
    float               restY_;       // starts
    float               restV_;
    signed char         span_;        // the deck's length, in cells
    unsigned char       slot_;        // the switch slot, stamped into cells
    audiodev::Buffer *sound_;       // the moving loop; may be NULL
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
