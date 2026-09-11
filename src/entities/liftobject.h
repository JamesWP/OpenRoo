/* LiftObject -- the rising/falling platform.  Ghidra struct `LiftObject`.
 *
 * Every function that touches a LiftObject field is in liftobject.cpp:
 * spawn (0x417b90), tick (0x411cb0) and purge (0x417d90).  The fields are
 * private, so the compiler enforces that -- code elsewhere reaches a lift
 * only through the public methods.
 *
 * The layout is the game's: operator new(0x4c), the game's constructor and
 * vtable, and GameTick still iterating the slot array.  So the class is
 * packed, and every offset is asserted in liftobject.cpp.  It may choose its
 * own layout only once the constructor and destructor are ours too
 * (COHESION_PLAN.md, "When may a class own its layout?").
 */
#pragma once

class Game;
struct CStaticSoundbuffer;

class __attribute__((packed)) LiftObject {
public:
    /* Game::SpawnLiftObject 0x00417b90.  Arguments are dwords masked to
     * bytes, exactly as the original reads them. */
    static void spawn(Game *game, unsigned int uArg, unsigned int vArg,
                      unsigned int baseArg, unsigned int topArg);

    /* Game::PurgeLiftObjects 0x00417d90 -- destroy every lift and zero the
     * count. */
    static void purgeAll(Game *game);

    /* Game::UpdateVerticalLiftObject 0x00411cb0 -- one tick. */
    void tick();

private:
    LiftObject() = delete;          /* constructed by the game; see construct() */

    /* ── Placeholders for the game code this class still calls ─────── */
    /* 0x00411c50 -- base ctor 0x401000, then vtable 0x45d380, state 1,
     * atTop 1, sound 0.  Returns `raw`. */
    static LiftObject *construct(void *raw);
    /* vtable slot 0 (0x411c80, scalar deleting dtor), flags 1. */
    void destroy();

    static void assertLayout();

    void               *vtable_;       /* +0x00  game's, 0x45d380            */
    double              now_;          /* +0x04  latched from *clock_        */
    double             *clock_;        /* +0x0c  Game::clock()               */
    unsigned char      *record_;       /* +0x10  Game::field_170a5c()        */
    unsigned char       field_14;      /* +0x14                              */
    unsigned char       recordCopy_[8];/* +0x15  copied from *record_        */
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
