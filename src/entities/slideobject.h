/* SlideObject: a block that slides along its track and back.  Every function
 * that touches a SlideObject field is in slideobject.cpp; the fields are
 * private.  The renderer reads the position and the kind. */

#pragma once

 
#include "game.h"

namespace audiodev { class Buffer; }

class SlideObject {
public:
     

    // Spawns a slide; the arguments are masked to bytes.
    static void spawn(Game *game, unsigned int uArg, unsigned int vArg,
                      unsigned int heightArg, unsigned int kindArg);

    // Destroys every slide and zeroes the count.
    static void purgeAll(Game *game);

    // One tick.
    void tick();

    // Where it is drawn (LevelPlacements::drawSlides), and its axis: kind 0x0a
    // runs along U, any other along V.
    float posU() const          { return posU_; }
    float posY() const          { return posY_; }
    float posV() const          { return posV_; }
    signed char kind() const    { return kind_; }

    // The moving-loop sound, attached by InitLevelBasedSounds
    // (levelsounds.cpp).
    void setSound(audiodev::Buffer *p) { sound_ = p; }

private:

    // Allocates and constructs one; NULL if the allocation fails.
    static SlideObject *create();
    SlideObject();
    virtual ~SlideObject();
    SlideObject(const SlideObject &) = delete;
    SlideObject &operator=(const SlideObject &) = delete;

    // Releases the tile the slide just left.
    void vacate();

    double              now_;           // latched from *clock_
    double             *clock_;         // Game::clock()
    TickStep        *tickStep_;         // Game::tickStep()
    unsigned char       field_14;
    TickStep         tickStepCopy_;     // copied from *tickStep_
    unsigned char       field_1d[8];
    float               posU_;          // read by the renderer
    float               posY_;
    float               posV_;
    signed char         cellU_;
    signed char         cellV_;
    signed char         heightCell_;
    unsigned char       field_34[4];    // never written
    unsigned char       span_;          // limit - trackStart
    audiodev::Buffer *sound_;         // the moving loop; may be NULL
    signed char         originU_;
    signed char         originV_;
    signed char         originHeight_;
    // PRESERVED: read unsigned to compare and signed to snap (see the tick).
    unsigned char       limit_;       // the track's last cell
    unsigned char       trackStart_;  // the track's first cell
    unsigned char       tileHeight_;  // stamped into the tile
    Tile               *tileBase_;    // Game::tileBase()
    signed char         kind_;        // 0x0a: along U; else V
    int                 atLimit_;     // 1: parked at the limit
    signed char         state_;       // 0 parked, 1 advancing, 2 retreating
    double              phaseStart_;
};

 