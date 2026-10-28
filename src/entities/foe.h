/* Foe: a chasing enemy, a MovableEntity plus its own four fields.  The World's
 * foe list holds only foes.  Most foe fields sit inside the MovableEntity, so
 * they are declared there (protected).
 *
 * The sound attachment, the cheats, the tile queries and the movement code
 * treat a foe as a plain entity; entitydraw.cpp draws it.  Its pathfinder
 * is FoePath (foepath.cpp). */

#pragma once

 
#include <cstddef>
#include "entitycontext.h"
#include "entitylist.h"
#include "bomb.h"
#include "movableentity.h"

class Tile;

/* What a foe is (MovableEntity::kind()): a catcher chases and touches, a
 * thrower drops bombs.  Each has its own look and sounds.  The value is also
 * what the foe stamps into its tile's occupant. */
enum class FoeKind : unsigned char {
    Catcher = 2,
    Thrower = 3,
};

class Foe : public MovableEntity {
public:
     

    // Spawns a foe at the end of the list; the arguments are masked to
    // bytes.  Returns the new foe.
    static Foe &spawn(const EntityContext &ctx, EntityList<Foe> &foes,
                      unsigned int uArg, unsigned int vArg,
                      unsigned int hArg, unsigned int kindArg,
                      unsigned int typeArg);

    // Destroys foes[i], reporting it destroyed; those after it move down
    // one.
    static void remove(EntityList<Foe> &foes, std::size_t i, PlayEvents &events);

    // Despawns it: its fall sound stops and its sounds are released
    // (remove, and World::clearObjects).
    void despawn(PlayEvents &events) const;

    // Frees its tile claims and its path-finder.
    ~Foe() override;

    // Draws it on the debug map and adds it to the tooltip; n is its place
    // in the World's foe list.
    void debugDraw(unsigned n) const;

    // One tick, towards the player's cell (or whatever target GameTick chose).
    void step(unsigned char playerU, unsigned char playerV, PlayEvents &events);

    // Searches towards (u, v) and queues the first step in pendingMove;
    // returns it.
    unsigned char chase(unsigned char targetU, unsigned char targetV,
                        unsigned short speed);

    // GameTick's foe loop (gametick.cpp) keeps the Game-side sequencing (the
    // switch hand-off to the bridge, the remove, the kill count); the foe-side
    // pieces are here.  Game and Player fields arrive as arguments, read at
    // the point the loop reads them.

    FoeKind foeKind() const { return (FoeKind)kind_; }
    bool    isCatcher() const { return foeKind() == FoeKind::Catcher; }
    bool    isThrower() const { return foeKind() == FoeKind::Thrower; }
    // The last cell its chase found a way to.
    unsigned char lastTargetU() const { return targetU_; }
    unsigned char lastTargetV() const { return targetV_; }

    // Set by the timed spawners: 7 (an extra life) every 15th kill, else 1.
    void setDropContents(unsigned char c)  { dropContents_ = c; }
    unsigned char dropContents() const     { return dropContents_; }
    // The switch the foe stands on; 0xff is none.
    unsigned char switchSlot() const       { return switchSlot_; }
    void clearSwitchSlot()                 { switchSlot_ = 0xff; }

    // The target for each behaviour, the hold flag and the chase speed (types
    // 2 and 3 chase from here).  hold is the loop's 0 or 1.
    void chooseTarget(const EntityContext &ctx, const EntityList<Foe> &foes, int hold,
                      unsigned char playerU, unsigned char playerV,
                      unsigned char escortU, unsigned char escortV,
                      unsigned char *pu, unsigned char *pv);
    // The foe's own bomb drop, when step() raised the hit flag.
    void dropBomb(const EntityContext &ctx, EntityList<Bomb> &bombs, PlayEvents &events);
    // Touching the player kills it (*playerMoveState = 1); once the player is
    // down, a foe not held gets anim 0x28.
    void checkPlayerContact(unsigned char *playerMoveState,
                            float playerU, float playerY, float playerV);
    // A foe in a move state: mark it dying, clear its home cell in homeMarks
    // unless that is 0x64, and if removal was requested stamp its drop
    // contents (when above ground) and return true; the caller removes it.
    bool finishDespawn(LevelMap *map);

private:
    Foe();

    Tile *tile(int u, int v) const;

    unsigned char dropContents_;  // stamped into its tile's contents when it dies
    unsigned char targetU_;       // the last chase target,
    unsigned char targetV_;       // which "return to post" chases again
    unsigned char field_15d;
};

 

