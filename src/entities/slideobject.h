/* SlideObject -- a block that slides along its track and back.  Ghidra
 * struct `SlideObject`.
 *
 * One class, formerly split across two names: the slot array Game+0x173588
 * (count +0x173718) is filled by SpawnSlideObject 0x417e20 and ticked by the
 * function GAMETICK_PLAN.md called "UpdatePushedBlockObject" 0x43ae00.  There
 * are no pushable blocks in the game; that tick is the slide's.  Ghidra now
 * names it UpdateSlideObject.
 *
 * Every function that touches a SlideObject field is in slideobject.cpp:
 * spawn (0x417e20), tick (0x43ae00) and purge (0x4181b0).  Construction and
 * destruction are ours too -- our own `new`, and our own one-slot vtable in
 * place of the game's 0x45d6f0; the originals 0x43adb0, 0x43add0 and
 * 0x43adf0 are UD2-stubbed.
 *
 * One outside reader remains, original code, which is why the layout stays
 * packed and asserted: the renderer FUN_00408920 reads +0x25/+0x29/+0x2d and
 * the kind +0x47.  (levelsounds.cpp attaches the sound through setSound().)
 */
#pragma once

#include "layout.h"
#include "game.h"

struct CStaticSoundbuffer;

class __attribute__((packed)) SlideObject {
public:
    static const int ORIGIN = 0;

    /* Game::SpawnSlideObject 0x00417e20.  Arguments are dwords masked to
     * bytes, exactly as the original reads them. */
    static void spawn(Game *game, unsigned int uArg, unsigned int vArg,
                      unsigned int heightArg, unsigned int kindArg);

    /* Game::PurgeSlideObjects 0x004181b0 -- destroy every slide and zero the
     * count. */
    static void purgeAll(Game *game);

    /* UpdateSlideObject 0x0043ae00 -- one tick. */
    void tick();

    /* +0x39, attached by InitLevelBasedSounds (levelsounds.cpp). */
    void setSound(CStaticSoundbuffer *p) { sound_ = p; }

private:

    /* The vtable.  MSVC layout: one slot, the scalar deleting destructor,
     * __thiscall with a flags argument (bit 0 = free the memory). */
    struct Vtbl {
        void *(__attribute__((thiscall)) *scalarDeletingDtor)(SlideObject *self,
                                                              unsigned int flags);
    };
    static const Vtbl VTABLE;

    /* 0x43adb0 -- allocate and construct, with our own new.  NULL if
     * allocation fails, as the original's operator new returned NULL. */
    static SlideObject *create();
    SlideObject();
    /* 0x43add0 -- vtable slot 0. */
    static void *__attribute__((thiscall)) scalarDeletingDtor(SlideObject *self,
                                                              unsigned int flags);
    /* Destroy through the object's own vtable, flags 1, as the purge did. */
    void destroy();

    /* Release the tile the slide just left (tick: the "vacate" stores). */
    void vacate();

    KAROO_LAYOUT_REGISTER(SlideObject);

    const Vtbl         *vtable_;       /* +0x00  &VTABLE                     */
    double              now_;          /* +0x04  latched from *clock_        */
    double             *clock_;        /* +0x0c  Game::clock()               */
    TickStep        *tickStep_;       /* +0x10  Game::tickStep()          */
    unsigned char       field_14;      /* +0x14                              */
    TickStep         tickStepCopy_;   /* +0x15  copied from *tickStep_        */
    unsigned char       field_1d[8];   /* +0x1d                              */
    float               posU_;         /* +0x25  } base-class fields, zeroed */
    float               posY_;         /* +0x29  } by 0x401000; read by the  */
    float               posV_;         /* +0x2d  } renderer FUN_00408920     */
    signed char         cellU_;        /* +0x31                              */
    signed char         cellV_;        /* +0x32                              */
    signed char         heightCell_;   /* +0x33                              */
    unsigned char       field_34[4];   /* +0x34  never written (the lift's
                                                 tile base sits here)        */
    unsigned char       span_;         /* +0x38  limit - trackStart          */
    CStaticSoundbuffer *sound_;        /* +0x39  moving loop, may be NULL    */
    signed char         originU_;      /* +0x3d                              */
    signed char         originV_;      /* +0x3e                              */
    signed char         originHeight_; /* +0x3f                              */
    /* Read UNSIGNED to compare and SIGNED to snap -- see the tick. */
    unsigned char       limit_;        /* +0x40  last cell of the track      */
    unsigned char       trackStart_;   /* +0x41  first cell of the track     */
    unsigned char       tileHeight_;   /* +0x42  stamped into the tile       */
    unsigned char      *tileBase_;     /* +0x43  Game::tileBase()            */
    signed char         kind_;         /* +0x47  0x0a = along U, else V      */
    int                 atLimit_;      /* +0x48  1 = parked at the limit     */
    signed char         state_;        /* +0x4c  0 parked, 1 advancing,
                                                 2 retreating                */
    double              phaseStart_;   /* +0x4d                              */
};

KAROO_LAYOUT_CHECKS(SlideObject)
{
    KAROO_LAYOUT_AT(now_,          0x04);
    KAROO_LAYOUT_AT(clock_,        0x0c);
    KAROO_LAYOUT_AT(tickStep_,       0x10);
    KAROO_LAYOUT_AT(tickStepCopy_,   0x15);
    KAROO_LAYOUT_AT(posU_,         0x25);
    KAROO_LAYOUT_AT(posY_,         0x29);
    KAROO_LAYOUT_AT(posV_,         0x2d);
    KAROO_LAYOUT_AT(cellU_,        0x31);
    KAROO_LAYOUT_AT(cellV_,        0x32);
    KAROO_LAYOUT_AT(heightCell_,   0x33);
    KAROO_LAYOUT_AT(span_,         0x38);
    KAROO_LAYOUT_AT(sound_,        0x39);
    KAROO_LAYOUT_AT(originU_,      0x3d);
    KAROO_LAYOUT_AT(originV_,      0x3e);
    KAROO_LAYOUT_AT(originHeight_, 0x3f);
    KAROO_LAYOUT_AT(limit_,        0x40);
    KAROO_LAYOUT_AT(trackStart_,   0x41);
    KAROO_LAYOUT_AT(tileHeight_,   0x42);
    KAROO_LAYOUT_AT(tileBase_,     0x43);
    KAROO_LAYOUT_AT(kind_,         0x47);
    KAROO_LAYOUT_AT(atLimit_,      0x48);
    KAROO_LAYOUT_AT(state_,        0x4c);
    KAROO_LAYOUT_AT(phaseStart_,   0x4d);
    /* No size check: the object is ours to allocate, so nothing relies on
     * it being the original's 0x55. */
}
