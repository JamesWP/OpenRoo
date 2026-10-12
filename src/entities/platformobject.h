/* PlatformObject: a block that slides along its track and back.  Every function
 * that touches a PlatformObject field is in platformobject.cpp; the fields are
 * private.  The renderer reads the position and the kind. */

#pragma once
#include "entitycontext.h"
#include "entitylist.h"
#include "tickstep.h"
#include "drawfwd.h"

 

class SoundVoice;
class SoundLibrary;

class PlatformObject {
public:
     

    // Spawns a platform; the arguments are masked to bytes.
    static void spawn(const EntityContext &ctx, EntityList<PlatformObject> &platforms, unsigned int uArg, unsigned int vArg,
                      unsigned int heightArg, unsigned int kindArg);

    // Releases its sounds, before World destroys it.
    void releaseSounds(SoundLibrary *sm);

    // One tick.
    void tick();

    // Where it stands, and its axis: kind 0x0a runs along U, any other
    // along V.
    float posU() const          { return posU_; }
    float posY() const          { return posY_; }
    float posV() const          { return posV_; }
    signed char kind() const    { return kind_; }

    // Draws the platform, in the opaque pass.
    void draw(draw::Frame &f, draw::Pass pass) const;

    // The moving-loop sound, attached by InitLevelBasedSounds
    // (levelsounds.cpp).
    void setSound(SoundVoice *p) { sound_ = p; }

    ~PlatformObject();

private:

    PlatformObject();
    PlatformObject(const PlatformObject &) = delete;
    PlatformObject &operator=(const PlatformObject &) = delete;

    // Releases the tile the platform just left.
    void vacate();

    double              now_;           // latched from *clock_
    double             *clock_;         // World::clock()
    TickStep        *tickStep_;         // World::tickStep()
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
    SoundVoice *sound_;         // the moving loop; may be NULL
    signed char         originU_;
    signed char         originV_;
    signed char         originHeight_;
    // PRESERVED: read unsigned to compare and signed to snap (see the tick).
    unsigned char       limit_;       // the track's last cell
    unsigned char       trackStart_;  // the track's first cell
    unsigned char       tileHeight_;  // stamped into the tile
    LevelMap               *map_;    // World::map()
    signed char         kind_;        // 0x0a: along U; else V
    int                 atLimit_;     // 1: parked at the limit
    signed char         state_;       // 0 parked, 1 advancing, 2 retreating
    double              phaseStart_;
};

 