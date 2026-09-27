/* BridgeObject: a switch-operated bridge.  A switch arms it, and while armed
 * it extends, or retracts, one deck cell at a time, stamping each cell it
 * covers.  Every function that touches a BridgeObject field is in
 * bridgeobject.cpp: the spawn, tick and purge, the deck quad
 * DrawBridgeSurfaces draws (bridgesurf.cpp), and the switch accessors GameTick
 * and tilequery.cpp use. */

#pragma once

#include "game.h"

struct CStaticSoundbuffer;

/* An FVF 0x242 vertex: XYZ, diffuse, two texture-coordinate sets. */
struct BridgeVertex {
    float x, y, z;
    unsigned long diffuse;
    float u0, v0;
    float u1, v1;
};
static_assert(sizeof(BridgeVertex) == 0x20, "BridgeVertex stride mismatch");

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

    // The moving-loop sound, attached by InitLevelBasedSounds
    // (levelsounds.cpp).
    void setSound(CStaticSoundbuffer *p) { sound_ = p; }

private:

    // The one-slot vtable: the scalar deleting destructor (bit 0 of flags
    // frees the memory).
    struct Vtbl {
        void *(*scalarDeletingDtor)(BridgeObject *self,
                                    unsigned int flags);
    };
    static const Vtbl VTABLE;

    // Allocates and constructs one; NULL if the allocation fails.
    static BridgeObject *create();
    BridgeObject();
    // Vtable slot 0.
    static void *scalarDeletingDtor(BridgeObject *self,
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
    float               posU_;        // the live position:
    float               posY_;        // the deck's far point
    float               posV_;
    signed char         cellU_;
    signed char         cellV_;
    signed char         heightCell_;
    Tile               *tileBase_;    // Game::tileBase()
    unsigned char       guard_;       // the far end; read signed by the tick
    float               restU_;       // the anchor, where the deck
    float               restY_;       // starts
    float               restV_;
    signed char         span_;        // the deck's length, in cells
    unsigned char       slot_;        // the switch slot, stamped into cells
    CStaticSoundbuffer *sound_;       // the moving loop; may be NULL
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

/* Destroys every bridge and zeroes the count; Game's teardown calls it. */
void Sim_PurgeBridgeObjects(Game *self);
