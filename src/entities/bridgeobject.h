/* BridgeObject -- a switch-operated bridge that extends across a gap and
 * back.  Ghidra struct `BridgeObject`.
 *
 * One class, formerly scattered over four files and two names: the slot
 * array Game+0x170643 (count +0x170a43) is filled by SpawnBridgeObject
 * 0x419ed0 and ticked by the function GAMETICK_PLAN.md called
 * "UpdateSlidingHazardObject" 0x43ec50.  It is not a hazard: a switch arms
 * it (GameTick's player and foe switch code), and while armed it extends --
 * or retracts -- one deck cell at a time, stamping each cell it covers.
 * Ghidra now names the tick UpdateBridgeObject.
 *
 * Every function that touches a BridgeObject field is in bridgeobject.cpp:
 * spawn (0x419ed0), tick (0x43ec50), purge (0x41a190), the per-bridge quad of
 * DrawBridgeSurfaces (0x408a00, driven from bridgesurf.cpp), and the switch
 * accessors GameTick and tilequery.cpp use.  Construction and destruction
 * are ours too -- our own `new`, and our own one-slot vtable in place of the
 * game's 0x45d714; the originals 0x43ec00, 0x43ec20 and 0x43ec40 are
 * UD2-stubbed.
 *
 * No outside accessor remains: no original code reads a bridge field (see
 * COHESION_PLAN.md), and levelsounds.cpp attaches the sound through
 * setSound().  The layout is still packed and asserted; unpacking it is a
 * separate decision, not made here.
 */
#pragma once

#include "layout.h"
#include "game.h"

struct CStaticSoundbuffer;

/* FVF 0x242 vertex: XYZ | DIFFUSE | two texture coordinate sets. */
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

class __attribute__((packed)) BridgeObject {
public:
    static const int ORIGIN = 0;

    /* Game::SpawnBridgeObject 0x00419ed0.  Arguments are dwords masked to
     * bytes, exactly as the original reads them. */
    static void spawn(Game *game, unsigned int uArg, unsigned int vArg,
                      unsigned int heightArg, unsigned int slotArg,
                      unsigned int axisArg);

    /* Game::PurgeBridgeObjects 0x0041a190 -- destroy every bridge and zero
     * the count. */
    static void purgeAll(Game *game);

    /* UpdateBridgeObject 0x0043ec50 -- one tick. */
    void tick();

    /* ── the switch (GameTick, tilequery.cpp) ──────────────────────── */
    /* Nonzero while a switch has set it moving (+0x53). */
    int  armed() const               { return armed_; }
    /* 0 = next move extends, 1 = next move retracts (+0x58). */
    int  phase() const               { return phase_; }
    /* A switch fires: arm, and start the phase at the game clock. */
    void arm(const double *clock)    { armed_ = 1; phaseStart_ = *clock; }
    /* The player's switch also starts the moving loop sound. */
    void playArmSound();

    /* ── the renderer (bridgesurf.cpp, DrawBridgeSurfaces loop C) ─── */
    /* Build this bridge's scrolling deck quad for animation time t (ms).
     * False, and v untouched, when the bridge is not drawn (never armed
     * and in phase 0).  `backward` is KAROO_BRIDGE_FX=backward.  Diffuse
     * is left 0xFFFFFFFF for the caller to overwrite. */
    bool buildSurface(BridgeVertex v[4], double t, bool backward,
                      BridgeSurfaceInfo *info) const;

    /* +0x47, attached by InitLevelBasedSounds (levelsounds.cpp). */
    void setSound(CStaticSoundbuffer *p) { sound_ = p; }

private:

    /* The vtable.  MSVC layout: one slot, the scalar deleting destructor,
     * __thiscall with a flags argument (bit 0 = free the memory). */
    struct Vtbl {
        void *(__attribute__((thiscall)) *scalarDeletingDtor)(BridgeObject *self,
                                                              unsigned int flags);
    };
    static const Vtbl VTABLE;

    /* 0x43ec00 -- allocate and construct, with our own new.  NULL if
     * allocation fails, as the original's operator new returned NULL. */
    static BridgeObject *create();
    BridgeObject();
    /* 0x43ec20 -- vtable slot 0. */
    static void *__attribute__((thiscall)) scalarDeletingDtor(BridgeObject *self,
                                                              unsigned int flags);
    /* Destroy through the object's own vtable, flags 1, as the purge did. */
    void destroy();

    KAROO_LAYOUT_REGISTER(BridgeObject);

    const Vtbl         *vtable_;       /* +0x00  &VTABLE                     */
    double              now_;          /* +0x04  latched from *clock_        */
    double             *clock_;        /* +0x0c  Game::clock()               */
    TickStep        *tickStep_;       /* +0x10  Game::tickStep()          */
    unsigned char       field_14;      /* +0x14                              */
    TickStep         tickStepCopy_;   /* +0x15  copied from *tickStep_        */
    unsigned char       field_1d[8];   /* +0x1d                              */
    float               posU_;         /* +0x25  } live position; base-class */
    float               posY_;         /* +0x29  } fields zeroed by 0x401000 */
    float               posV_;         /* +0x2d  } -- the deck's far point   */
    signed char         cellU_;        /* +0x31                              */
    signed char         cellV_;        /* +0x32                              */
    signed char         heightCell_;   /* +0x33                              */
    unsigned char      *tileBase_;     /* +0x34  Game::tileBase()            */
    unsigned char       guard_;        /* +0x38  far end; read SIGNED by the
                                                 tick                        */
    float               restU_;        /* +0x39  } the anchor: where the     */
    float               restY_;        /* +0x3d  } deck starts               */
    float               restV_;        /* +0x41  }                           */
    signed char         span_;         /* +0x45  deck length, in cells       */
    unsigned char       slot_;         /* +0x46  the switch slot; stamped    */
    CStaticSoundbuffer *sound_;        /* +0x47  moving loop, may be NULL    */
    double              phaseStart_;   /* +0x4b                              */
    int                 armed_;        /* +0x53                              */
    signed char         step_;         /* +0x57  +1 or -1 cell per 100 ms    */
    int                 phase_;        /* +0x58  0 extend next, 1 retract    */
    unsigned char       tileHeight_;   /* +0x5c  stamped into tile+0x19c     */
    unsigned char       endU_;         /* +0x5d  } the scan's start cell     */
    unsigned char       endV_;         /* +0x5e  }                           */
    unsigned char       height_;       /* +0x5f                              */
    unsigned char       axis_;         /* +0x60  1 = along U, 2 = along V    */
};

KAROO_LAYOUT_CHECKS(BridgeObject)
{
    KAROO_LAYOUT_AT(now_,         0x04);
    KAROO_LAYOUT_AT(clock_,       0x0c);
    KAROO_LAYOUT_AT(tickStep_,      0x10);
    KAROO_LAYOUT_AT(tickStepCopy_,  0x15);
    KAROO_LAYOUT_AT(posU_,        0x25);
    KAROO_LAYOUT_AT(posY_,        0x29);
    KAROO_LAYOUT_AT(posV_,        0x2d);
    KAROO_LAYOUT_AT(cellU_,       0x31);
    KAROO_LAYOUT_AT(cellV_,       0x32);
    KAROO_LAYOUT_AT(heightCell_,  0x33);
    KAROO_LAYOUT_AT(tileBase_,    0x34);
    KAROO_LAYOUT_AT(guard_,       0x38);
    KAROO_LAYOUT_AT(restU_,       0x39);
    KAROO_LAYOUT_AT(restY_,       0x3d);
    KAROO_LAYOUT_AT(restV_,       0x41);
    KAROO_LAYOUT_AT(span_,        0x45);
    KAROO_LAYOUT_AT(slot_,        0x46);
    KAROO_LAYOUT_AT(sound_,       0x47);
    KAROO_LAYOUT_AT(phaseStart_,  0x4b);
    KAROO_LAYOUT_AT(armed_,       0x53);
    KAROO_LAYOUT_AT(step_,        0x57);
    KAROO_LAYOUT_AT(phase_,       0x58);
    KAROO_LAYOUT_AT(tileHeight_,  0x5c);
    KAROO_LAYOUT_AT(endU_,        0x5d);
    KAROO_LAYOUT_AT(endV_,        0x5e);
    KAROO_LAYOUT_AT(height_,      0x5f);
    KAROO_LAYOUT_AT(axis_,        0x60);
    /* No size check: the object is ours to allocate, so nothing relies on
     * it being the original's 0x61 -- which the fields above tile exactly. */
}
