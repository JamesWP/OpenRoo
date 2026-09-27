/* SlideObject: a block that slides along its track and back.  Every function
 * that touches a SlideObject field is in slideobject.cpp; the fields are
 * private.  The layout is fixed: the renderer reads the position and the kind.
 */

#pragma once

#include "layout.h"
#include "game.h"

struct CStaticSoundbuffer;

class __attribute__((packed)) SlideObject {
public:
    static const int ORIGIN = 0;

    // Spawns a slide; the arguments are masked to bytes.
    static void spawn(Game *game, unsigned int uArg, unsigned int vArg,
                      unsigned int heightArg, unsigned int kindArg);

    // Destroys every slide and zeroes the count.
    static void purgeAll(Game *game);

    // One tick.
    void tick();

    // Where it is drawn (LevelPlacements_DrawSlides), and its axis: kind 0x0a
    // runs along U, any other along V.
    float posU() const          { return posU_; }
    float posY() const          { return posY_; }
    float posV() const          { return posV_; }
    signed char kind() const    { return kind_; }

    // The moving-loop sound, attached by InitLevelBasedSounds
    // (levelsounds.cpp).
    void setSound(CStaticSoundbuffer *p) { sound_ = p; }

private:

    // The one-slot vtable: the scalar deleting destructor (bit 0 of flags
    // frees the memory).
    struct Vtbl {
        void *(*scalarDeletingDtor)(SlideObject *self,
                                    unsigned int flags);
    };
    static const Vtbl VTABLE;

    // Allocates and constructs one; NULL if the allocation fails.
    static SlideObject *create();
    SlideObject();
    // Vtable slot 0.
    static void *scalarDeletingDtor(SlideObject *self,
                                    unsigned int flags);
    // Destroys through the object's own vtable, flags 1.
    void destroy();

    // Releases the tile the slide just left.
    void vacate();

    KAROO_LAYOUT_REGISTER(SlideObject);

    const Vtbl         *vtable_;        // +0x00  &VTABLE
    double              now_;           // +0x04  latched from *clock_
    double             *clock_;         // +0x0c  Game::clock()
    TickStep        *tickStep_;         // +0x10  Game::tickStep()
    unsigned char       field_14;       // +0x14
    TickStep         tickStepCopy_;     // +0x15  copied from *tickStep_
    unsigned char       field_1d[8];    // +0x1d
    float               posU_;          // +0x25  read by the renderer
    float               posY_;          // +0x29
    float               posV_;          // +0x2d
    signed char         cellU_;         // +0x31
    signed char         cellV_;         // +0x32
    signed char         heightCell_;    // +0x33
    unsigned char       field_34[4];    // +0x34  never written
    unsigned char       span_;          // +0x38  limit - trackStart
    CStaticSoundbuffer *sound_;         // +0x39  the moving loop; may be NULL
    signed char         originU_;       // +0x3d
    signed char         originV_;       // +0x3e
    signed char         originHeight_;  // +0x3f
    // PRESERVED: read unsigned to compare and signed to snap (see the tick).
    unsigned char       limit_;       // +0x40  the track's last cell
    unsigned char       trackStart_;  // +0x41  the track's first cell
    unsigned char       tileHeight_;  // +0x42  stamped into the tile
    unsigned char      *tileBase_;    // +0x43  Game::tileBase()
    signed char         kind_;        // +0x47  0x0a: along U; else V
    int                 atLimit_;     // +0x48  1: parked at the limit
    signed char         state_;       // +0x4c  0 parked, 1 advancing, 2 retreating
    double              phaseStart_;  // +0x4d
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

/* No size check: we allocate it, so nothing relies on its size. */
}

/* Destroys every slide and zeroes the count; Game's teardown calls it. */
void Sim_PurgeSlideObjects(Game *self);
