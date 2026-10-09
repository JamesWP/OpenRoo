/* Foe: a chasing enemy, a MovableEntity plus its own four fields.  The Game's
 * foe table holds only foes.  Most foe fields sit inside the MovableEntity, so
 * they are declared there (protected).
 *
 * The sound attachment, the cheats, the tile queries and the movement code
 * treat a foe as a plain entity, and RenderGameFrame draws it.  Its
 * pathfinder is FoePath (foepath.cpp). */

#pragma once

 
#include "entitycontext.h"
#include "bomb.h"
#include "movableentity.h"

class SoundManager;
class Tile;
class Foe;

typedef EntityIdTable<Foe, 500> FoeTable;

class Foe : public MovableEntity {
public:
     

    // Spawns a foe; the arguments are masked to bytes.  Returns the new ID.
    static unsigned char spawn(const EntityContext &ctx, FoeTable &foes,
                               unsigned int uArg, unsigned int vArg,
                               unsigned int hArg, unsigned int kindArg,
                               unsigned int typeArg);

    // Releases the sounds, destroys the foe, nulls the slot and removes its
    // ID.
    static void remove(const EntityContext &ctx, FoeTable &foes,
                       unsigned int idArg);

    // Draws it on the debug map and adds it to the tooltip; id is its slot
    // in the Game's foe table.
    void debugDraw(unsigned id) const;

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
    void chooseTarget(const EntityContext &ctx, FoeTable &foes, int hold,
                      unsigned char playerU, unsigned char playerV,
                      unsigned char escortU, unsigned char escortV,
                      unsigned char *pu, unsigned char *pv);
    // The foe's own bomb drop, when step() raised the hit flag.
    void dropBomb(const EntityContext &ctx, BombTable &bombs,
                  const BombSounds &sounds);
    // Touching the player kills it (*playerMoveState = 1); once the player is
    // down, a foe not held gets anim 0x28.
    void checkPlayerContact(unsigned char *playerMoveState,
                            float playerU, float playerY, float playerV);
    // A foe in a move state: mark it dying, clear its home cell in homeMarks
    // unless that is 0x64, and if removal was requested stamp its drop
    // contents (when above ground) and return true; the caller removes it.
    bool finishDespawn(LevelMap *map);

private:
    // Allocates and constructs one; NULL if the allocation fails.
    static Foe *create();
    Foe();
    // Frees its tile claims and its path-finder.
    ~Foe() override;

    Tile *tile(int u, int v) const;

    unsigned char dropContents_;  // stamped into its tile's contents when it dies
    unsigned char targetU_;       // the last chase target,
    unsigned char targetV_;       // which "return to post" chases again
    unsigned char field_15d;
};

 

