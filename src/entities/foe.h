/* Foe: a chasing enemy, a MovableEntity (0x15a bytes) plus its own four.  The
 * Game's foe table holds only foes.  Most foe fields sit inside the
 * MovableEntity region, so they are declared there (protected).
 *
 * The layout is fixed: the sound attachment, the cheats, the tile queries and
 * the movement code treat a foe as a plain entity, and RenderGameFrame draws
 * it.  Its pathfinder at +0x13b is FoePath (foepath.cpp). */

#pragma once

 
#include "game.h"
#include "movableentity.h"

class SoundManager;
class Tile;

class __attribute__((packed)) Foe : public MovableEntity {
public:
     

    // Spawns a foe; the arguments are masked to bytes.  Returns the new ID.
    static unsigned char spawn(Game *game, unsigned int uArg, unsigned int vArg,
                               unsigned int hArg, unsigned int kindArg,
                               unsigned int typeArg);

    // Releases the sounds, destroys the foe, nulls the slot and removes its
    // ID.
    static void remove(Game *game, unsigned int idArg);

    // One tick, towards the player's cell (or whatever target GameTick chose).
    void step(unsigned char playerU, unsigned char playerV);

    // Searches towards (u, v) and queues the first step in pendingMove;
    // returns it.
    unsigned char chase(unsigned char targetU, unsigned char targetV,
                        unsigned short speed);

    // GameTick's foe loop (gametick.cpp) keeps the Game-side sequencing (the
    // switch hand-off to the bridge, the remove, the kill count); the foe-side
    // pieces are here.  Game and Player fields arrive as arguments, read at
    // the point the loop reads them.

    // Set by the timed spawners: 7 (an extra life) every 15th kill, else 1.
    void setDropContents(unsigned char c)  { dropContents_ = c; }
    unsigned char dropContents() const     { return dropContents_; }
    // The switch the foe stands on; 0xff is none.
    unsigned char switchSlot() const       { return field_d7; }
    void clearSwitchSlot()                 { field_d7 = 0xff; }

    // The target for each behaviour, the hold flag and the chase speed (types
    // 2 and 3 chase from here).  hold is the loop's 0 or 1.
    void chooseTarget(Game *game, int hold,
                      unsigned char playerU, unsigned char playerV,
                      unsigned char escortU, unsigned char escortV,
                      unsigned char *pu, unsigned char *pv);
    // The foe's own bomb drop, when step() raised the hit flag.
    void dropBomb(Game *game);
    // Touching the player kills it (*playerMoveState = 1); once the player is
    // down, a foe not held gets +0x9a = 0x28.
    void checkPlayerContact(unsigned char *playerMoveState,
                            float playerU, float playerY, float playerV);
    // A foe in a move state: mark +0x86, clear its home cell in homeMarks
    // unless that is 0x64, and if removal was requested stamp its drop
    // contents (when above ground) and return true; the caller removes it.
    bool finishDespawn(LevelMap *map);

private:
    // The one-slot vtable: the scalar deleting destructor (bit 0 of flags
    // frees the memory).  The remove destroys through it.
    struct Vtbl {
        void *(  *scalarDeletingDtor)(Foe *self,
                                                              unsigned int flags);
    };
    static const Vtbl VTABLE;

    // Allocates and constructs one; NULL if the allocation fails.
    static Foe *create();
    Foe();
    // The destructor body.
    void destroy();
    // Vtable slot 0.
    static void *  scalarDeletingDtor(Foe *self,
                                                              unsigned int flags);

    Tile *tile(int u, int v) const;

     

    unsigned char dropContents_;  // +0x15a  stamped into its tile's contents when it dies
    unsigned char targetU_;       // +0x15b  the last chase target,
    unsigned char targetV_;       // +0x15c  which "return to post" chases again
    unsigned char field_15d;      // +0x15d
};

 

