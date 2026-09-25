/* Foe -- a chasing enemy.  Ghidra struct `Foe`, 0x15e bytes: a MovableEntity
 * (movableentity.h, 0x15a bytes) plus its own four.
 *
 * The game's foe table (Game+0x174804, count +0x174fd4, IDs +0x174fd5) is
 * filled only by SpawnFoeObject.
 *
 * Ours (foe.cpp): spawn (0x4172d0), remove (0x417530), step (0x412240) and
 * chase (0x43a9d0), and construction and destruction -- our own `new`, and
 * our own one-slot vtable in place of the game's 0x45d388; the originals
 * 0x411ff0, 0x412140 and 0x412160 are UD2-stubbed.  Most foe fields sit
 * inside the MovableEntity region, so they are declared there (protected)
 * rather than here.
 *
 * Outside accessors remain, which is why the layout stays packed and
 * asserted (GameTick's foe loop now goes through the methods below);
 * soundobj.cpp and levelsounds.cpp attach its sounds; cheatcode.cpp forces
 * moveState; tilequery.cpp (FindFarthestOccupiedTile) and movableentity.cpp
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

    /* ── GameTick's foe loop (gametick.cpp) ─────────────────────────────
     * The loop keeps the Game-side sequencing (the switch hand-off to the
     * bridge, the remove, the kill counter); each foe-side piece is here.
     * Game and Player fields arrive as arguments, read by the loop at the
     * same point the original reads them. */

    /* +0x15a, set by the runtime foe spawners: 7 every 15th kill, else 1. */
    void setDropContents(unsigned char c)  { dropContents_ = c; }
    unsigned char dropContents() const     { return dropContents_; }
    /* +0xd7: the switch the foe stands on, 0xff = none. */
    unsigned char switchSlot() const       { return field_d7; }
    void clearSwitchSlot()                 { field_d7 = 0xff; }

    /* The target per behaviour type, the hold flag +0xef and chase speed
     * +0x64 (types 2 and 3 chase from here).  `hold` is the loop's 0/1. */
    void chooseTarget(Game *game, int hold,
                      unsigned char playerU, unsigned char playerV,
                      unsigned char escortU, unsigned char escortV,
                      unsigned char *pu, unsigned char *pv);
    /* The foe's own bomb drop: the hit flag +0xe4 raised by step(). */
    void dropBomb(Game *game);
    /* Touching the player kills it (*playerMoveState = 1); once the player
     * is down, an unfrozen foe gets +0x9a = 0x28. */
    void checkPlayerContact(unsigned char *playerMoveState,
                            float playerU, float playerY, float playerV);
    /* A foe in a move state: mark +0x86, clear its home cell's entry in
     * `homeMarks` (Game+0x3e181c) unless it is 0x64, and if removal was
     * requested stamp its drop contents (when above ground) and return
     * true -- the caller removes it. */
    bool finishDespawn(LevelMap *map);

private:
    /* The vtable.  MSVC layout: one slot, the scalar deleting destructor,
     * __thiscall with a flags argument (bit 0 = free the memory).  The
     * remove dispatches through it (Object_DestroyAndCompactId). */
    struct Vtbl {
        void *(__attribute__((thiscall)) *scalarDeletingDtor)(Foe *self,
                                                              unsigned int flags);
    };
    static const Vtbl VTABLE;

    /* 0x411ff0 -- allocate and construct, with our own new.  NULL if
     * allocation fails, as the original's operator new returned NULL. */
    static Foe *create();
    Foe();
    /* 0x412160 -- the destructor body. */
    void destroy();
    /* 0x412140 -- vtable slot 0. */
    static void *__attribute__((thiscall)) scalarDeletingDtor(Foe *self,
                                                              unsigned int flags);

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
    KAROO_LAYOUT_AT(posU_,             0x025);
    KAROO_LAYOUT_AT(pathfinder_,       0x13b);
    KAROO_LAYOUT_AT(dropContents_,     0x15a);
    KAROO_LAYOUT_AT(targetU_,          0x15b);
    KAROO_LAYOUT_AT(targetV_,          0x15c);
    /* The original's operator_new(0x15e): the class tiles it exactly. */
    KAROO_LAYOUT_SIZE(0x15e);
}

/* 0x417530 -- remove the foe with this id and compact the id table. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_RemoveFoeObject(Game *self, unsigned int idArg);
