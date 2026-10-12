/* Bomb: a dropped bomb.  A 2 s fuse during which it rolls forward, then a 3x3
 * kill zone for 0.4 s.  A MovableEntity plus its own fields.  The World's
 * bomb list holds only bombs.  It draws itself (draw()), and the
 * movement code treats a bomb as a plain entity. */

#pragma once

 
#include <cstddef>
#include "entitycontext.h"
#include "entitylist.h"
#include "movableentity.h"
#include "drawfwd.h"
#include "soundasset.h"

class SoundVoice;
class SoundLibrary;
class Tile;

/* The theme's sounds a bomb attaches when it is dropped. */
struct BombSounds {
    const SoundAssetName *b3, *b7bb, *blast, *c3, *roll;
};

class Bomb : public MovableEntity {
public:
     

    // Spawns a bomb at (u, v, h); the arguments are masked to bytes.
    static void spawn(const EntityContext &ctx, EntityList<Bomb> &bombs,
                      const BombSounds &sounds, unsigned int uArg,
                      unsigned int vArg, unsigned int hArg,
                      unsigned int flagArg);

    // Releases the sounds of bombs[i] and destroys it; those after it move
    // down one.
    static void remove(const EntityContext &ctx, EntityList<Bomb> &bombs,
                       std::size_t i);

    // Releases its sounds (remove, and World::clearObjects).
    void releaseSounds(SoundLibrary *sm);

    // One tick: the fuse, the roll and the blast.
    void tick();

    // Draws it on the debug map; n is its place in the World's bomb list.
    void debugDraw(unsigned n) const;

    // Draws the bomb for one pass of the frame: the bomb, or once it has
    // gone off the explosion (firing its debris), and its particles.
    // deathsDrawn is the level's death count at the last frame drawn: a
    // newer death fires its effects.
    void draw(draw::Frame &f, draw::Pass pass, unsigned deathsDrawn) const;

private:
    Bomb();

    // The cell (u, v) from this bomb's tile base, both read signed.
    Tile *tile(int u, int v) const;
    void acquireInto(SoundLibrary *sm, const SoundAssetName *asset, SoundVoice *Bomb::*field);
    void releaseField(SoundLibrary *sm, SoundVoice *Bomb::*field);

    SoundVoice *rollSound_;         // may be NULL
    SoundVoice *blastSound_;        // may be NULL
    int                 blastSoundPlayed_;
    int                 zoneCleared_;
    double              droppedAt_;
};
