/* Foe -- a chasing enemy.  Ghidra struct `Foe`, 0x15e bytes: a MovableEntity
 * (movableentity.h, 0x15a bytes) plus its own four.
 *
 * The game's foe table (Game+0x174804, count +0x174fd4, IDs +0x174fd5) is
 * filled only by SpawnFoeObject.
 *
 * Ours (foe.cpp): spawn (0x4172d0), remove (0x417530), step (0x412240) and
 * chase (0x43a9d0).  Most foe fields sit inside the MovableEntity region, so
 * they are declared there (protected) rather than here.
 *
 * Outside accessors remain, which is why the layout stays packed and
 * asserted: GameTick's foe loop (gametick.cpp) picks each foe's target and
 * handles its bomb drop, contact and despawn by raw offset;
 * soundobj.cpp and levelsounds.cpp attach its sounds; cheatcode.cpp forces
 * moveState; tilequery.cpp (FindFarthestOccupiedTile) and entitymove.cpp
 * (UpdateEntityMovement) treat it as a plain entity; worldstate.cpp reads the
 * table; the original RenderGameFrame draws it.  The pathfinder at +0x13b is
 * FoePath (foepath.cpp), whose fields the chase still reaches by offset.
 */
#pragma once

#include "layout.h"
#include "game.h"
#include "movableentity.h"

class SoundManager;
class Tile;

class __attribute__((packed)) Foe : public MovableEntity {
public:
    static const int ORIGIN = 0;

    /* Game::SpawnFoeObject 0x004172d0.  Dword arguments masked to bytes;
     * returns the new ID. */
    static unsigned char spawn(Game *game, unsigned int uArg, unsigned int vArg,
                               unsigned int hArg, unsigned int kindArg,
                               unsigned int typeArg);

    /* Game::RemoveFoeObject 0x00417530 -- release the sounds, destroy
     * through the vtable, null the slot, compact the ID list. */
    static void remove(Game *game, unsigned int idArg);

    /* Game::UpdateFoeObjectStep 0x00412240 -- one tick, given the player's
     * cell (or whatever target GameTick's foe loop chose). */
    void step(unsigned char playerU, unsigned char playerV);

    /* Game::SetFoeChaseTarget 0x0043a9d0 -- search toward (u, v) and queue
     * the first step in pendingMove.  Returns it. */
    unsigned char chase(unsigned char targetU, unsigned char targetV,
                        unsigned short speed);

private:
    Tile *tile(int u, int v) const;

    KAROO_LAYOUT_REGISTER(Foe);

    unsigned char dropContents_;  /* +0x15a  stamped into the tile's contents
                                             when the foe dies (GameTick)  */
    unsigned char targetU_;       /* +0x15b  } the last chase target:       */
    unsigned char targetV_;       /* +0x15c  } "return to post" re-chases it */
    unsigned char field_15d;      /* +0x15d                                 */
};

KAROO_LAYOUT_CHECKS(Foe)
{
    /* The base sits at 0 (its own fields are asserted in MovableEntity). */
    KAROO_LAYOUT_AT(posU_,         0x025);
    KAROO_LAYOUT_AT(pathfinder_,   0x13b);
    KAROO_LAYOUT_AT(dropContents_, 0x15a);
    KAROO_LAYOUT_AT(targetU_,      0x15b);
    KAROO_LAYOUT_AT(targetV_,      0x15c);
    /* The original's operator_new(0x15e): the class tiles it exactly. */
    KAROO_LAYOUT_SIZE(0x15e);
}
