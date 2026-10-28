/* MovableEntity: the base Bomb, Foe and Player share -- a position on the
 * grid, the step and animation in progress, and which source of sound it is.
 * updateMovement() is the step every entity tick ends in.
 *
 * A field whose meaning is unsettled is named field_<hex>, after where the
 * original kept it. */

#pragma once

#include "entitylist.h"
#include "playevents.h"

 
#include <stddef.h>
#include "tickstep.h"
#include "tile.h"

class LevelMap;
class FoePath;

/* One moving entity's shared state.  Foe, Bomb and Player derive from it. */
class MovableEntity : public Listed {
public:
     

    // The level's map, copied in at spawn from World::map().
    const LevelMap *map() const { return map_; }

    // Nonzero once this tick should remove the entity.  Read by GameTick after
    // a bomb's tick.
    int removeRequested() const { return removeRequested_; }

    // The movement step every entity tick ends in (player, foe, bomb).
    // Returns 1 on the two early outs taken while the entity is dying, 0
    // otherwise.
    unsigned int updateMovement(PlayEvents &events);

    // Readers shared by every entity.  Meaning is noted below only where the
    // name does not already say it.
    unsigned char facing() const               { return facing_; }
    float posU() const                         { return posU_; }
    float posY() const                         { return posY_; }
    float posV() const                         { return posV_; }
    signed char cellU() const                  { return cellU_; }
    signed char cellV() const                  { return cellV_; }
    signed char heightCell() const             { return heightCell_; }
    // Recomputed every movement tick: taking a stair or sliding this tick.
    // Stops UpdateViewTransform's camera lift while it holds.
    int   onStairOrSlide() const               { return onStairOrSlide_; }
    // The step phase, which pose() (entitydraw.h) turns into a 0..1 fraction.
    unsigned char turnKind() const             { return turnKind_; }
    bool  onSwitch() const                     { return onSwitch_ != 0; }
    double animDuration() const                { return animDuration_; }
    double animStart() const                   { return animStart_; }
    double stepEnd() const                     { return stepEnd_; }
    double stuckSince() const                  { return stuckSince_; }
    bool   launched() const                    { return launched_ != 0; }
    int    teleportPhase() const               { return teleportPhase_; }
    int    lastMoveDir() const                 { return lastMoveDir_; }
    bool   ridingPlatform() const              { return platformSlot_ != 0xff; }
    double teleportSince() const               { return teleportSince_; }
    double fallStart() const                   { return fallStart_; }
    // A foe: the clock of its last bomb, and a bomb asked for and not yet dropped.
    double lastContact() const                 { return lastContact_; }
    int   bombDropRequest() const              { return bombDropRequest_; }
    unsigned char type() const                 { return type_; }
    int   dyingStarted() const                 { return dyingStarted_; }
    // Which death this was in the level's count (0 while alive).
    unsigned deathNumber() const               { return deathNumber_; }
    // The foe's hold flag, or the Player's level-complete flag.  Either way,
    // updateMovement() drops the queued move while it is set.
    int   held() const                         { return held_; }
    // The animation state (see the field below); 10 is the dying animation
    // that DrawObjectShadows' dead/alive conditions test.
    unsigned char anim() const                 { return anim_; }
    // The tile contents picked up this tick, 0 if none (1 = a crystal).
    unsigned char pickedUp() const             { return pickedUp_; }
    // Paraglider charges, and whether it is open.
    unsigned char glides() const               { return glides_; }
    int   gliding() const                      { return gliding_; }
    // Nonzero while it falls.
    int   falling() const                      { return falling_; }
    // The direction being moved in, 0 while still.  1 = -V, 2 = +U, 3 = +V, 4
    // = -U, matching updateMovement()'s stepU_/stepV_ table.
    int   moveDir() const                      { return moveDir_; }
    unsigned char kind() const                 { return kind_; }
    unsigned char homeU() const                { return homeU_; }
    unsigned char homeV() const                { return homeV_; }
    unsigned char homeH() const                { return homeH_; }
    // 4 = despawn (the "kaputo" cheat sets it on every foe); the Player's is
    // nonzero while dead or respawning.
    unsigned char moveState() const            { return moveState_; }
    void  setMoveState(unsigned char s)        { moveState_ = s; }
    const FoePath *pathfinder() const          { return pathfinder_; }
    FoePath       *pathfinder()                { return pathfinder_; }

    // Which of the sources of sound it is, and one of its sounds by name
    // (playevents.h).
    SoundSource soundSource() const             { return soundSource_; }
    SoundKey    sound(Sfx sfx) const            { return {soundSource_, id(), sfx}; }

protected:
    // Our own subclasses use only the base's field work, the three zeroed
    // position floats.
    MovableEntity();

    LevelMap *map() { return map_; }

public:
    // Virtual, so a Foe or Bomb is destroyed through a MovableEntity pointer.
    virtual ~MovableEntity();

protected:
    double              now_{};
    double             *clock_{};
    TickStep        *tickStep_{};
    unsigned char       facing_{};
    TickStep         tickStepCopy_;
    // World position (U, height, V).
    float               posU_{};
    float               posY_{};
    float               posV_{};
    signed char         cellU_;
    signed char         cellV_;
    signed char         heightCell_;
    LevelMap               *map_{};
    double              idleDuration_{};  // ms the idle animation runs.
    int                 onStairOrSlide_{};
    int                 movingBackwards_{};  // Set when turnKind_ is 3 (reversing this step).
    // stepGrace_: the window after a step ends (animStart_ + animDuration_)
    // during which the next step may start without a fresh delay.  stepEnd_:
    // the clock value that window is measured from.
    double              stepGrace_{};
    double              stepEnd_{};
    int                 iceDir_{};  // Ice: the last direction sent.
    float               fallSpeed_{};    // Fall velocity; starts at -3.0.
    // Re-queued from here once kind 4 is released from a blocked step.
    unsigned char       queuedMove_{};
    unsigned char       queuedTurn_{};
    // The foe's behaviour type: 1 escort, 2 seek a listed tile, 3 seek a
    // flagged tile, 4 return to post, 5 follow, 7 seek the farthest tile
    // (GameTick's foe loop).  Read signed by the step and the chase.
    unsigned char       type_{};
    unsigned char       pickedUp_{};
    unsigned short      chaseSpeed_{};
    // ms one cell step takes; animDuration_ resets to this by kind and state
    // elsewhere.
    double              stepDuration_{};
    int                 idleStarted_{};
    // The clock the idle timeout counts from.
    double              lastActive_{};
    // The level's death counter (World::deaths), and this entity's number in
    // it, taken the tick dying starts.  A draw fires the death effects (the
    // dying look's debris and particles) for a death newer than the last one
    // drawn, so drawing changes nothing here.  Null for the player, whose
    // death has no effects.
    unsigned           *deathCounter_{};
    unsigned            deathNumber_{};
    int                 removeRequested_{};
    int                 dyingStarted_{};  // Set once the entity starts dying: it draws its dying look.
    int                 dying_{};  // Set on being crushed or blasted; starts the death sequence.
    double              dyingSince_{};  // Dying clock; removeRequested_ is set once it is half a second old.
    // The animation state: 0 = still, 0xfa = the idle animation; 0x16..0x1b
    // carry the height curves updateMovement() interpolates between.
    unsigned char       anim_{};
    int                 onLift_{};  // Riding a lift (kind 9).
    SoundSource         soundSource_ = SoundSource::Player;
    int                 onSwitch_{};    // Has stood on a switch and worked it; cleared by the next step forward or back.
    unsigned char       switchSlot_{};  // The switch stepped on and not yet acted on; 0xff for none.
    int                 field_d8{};
    double              lastContact_{};      // Foe: the clock of its last contact.
    int                 bombDropRequest_{};  // Requests a bomb be dropped this tick.
    unsigned char       bombsCarried_{};     // Player: bombs in hand; a pickup adds 3, a drop takes 1.
    unsigned char       glides_{};
    int                 gliding_{};
    unsigned char       field_ee{};
    int                 held_{};
    int                 sliding_{};
    unsigned char       teleportPhase_{};  // 0 idle, 1 armed, 2 sent.
    double              teleportSince_{};  // The clock the current teleport phase began.
    unsigned char       lastMoveDir_{};  // The last direction actually moved (as opposed to a turn on the spot).
    unsigned char       fallStartH_{};  // The height the current fall began at.
    double              fallStart_{};   // The clock the current fall began.
    int                 launched_{};    // On a jump pad's arc, since fallStart_.
    unsigned char       platformSlot_{};  // The platform being ridden, 0xff for none.
    unsigned char       moveState_{};
    int                 falling_{};
    unsigned char       field_124{};
    // pendingMove_'s turn relative to facing_: 1 forward, 2 and 4 the two
    // turns, 3 reverse, 0 when the move needs no turn at all.
    unsigned char       turnKind_{};
    double              stuckSince_{};  // The clock the sticky pad caught this entity; zero while not stuck.
    int                 field_12e{};
    double              animDuration_{};  // ms the current animation phase lasts.
    FoePath            *pathfinder_{};  // Foe: its FoePath.
    // stepU_ / stepV_: moveDir_ resolved to a cell step, each -1, 0 or +1.
    signed char         stepU_;
    signed char         stepV_;
    signed char         field_141;
    unsigned char       markerCellU_{};
    unsigned char       markerCellV_{};
    unsigned char       markerCellH_{};
    unsigned char       pendingMove_{};
    double              animStart_{};  // The clock the current animation phase began.
    int                 moveDir_{};
    unsigned char       kind_{};  // 9 identifies a bomb; a foe's is set from its spawn kind.
    // homeU_ / homeV_ / homeH_: the foe's spawn cell.
    unsigned char       homeU_{};
    unsigned char       homeV_{};
    unsigned char       homeH_{};
    int                 field_156{};

private:
     
};

