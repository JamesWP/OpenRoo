/* MovableEntity: the base Bomb, Foe and Player share -- a position on the
 * grid, the step and animation in progress, and twelve sound handles.
 * updateMovement() is the step every entity tick ends in.
 *
 * A field whose meaning is unsettled is named field_<hex>, after where the
 * original kept it. */

#pragma once

 
#include <stddef.h>
#include "tickstep.h"
#include "tile.h"

class CStaticSoundbuffer;
class VoicePool;
class FoePath;

/* One moving entity's shared state.  Foe, Bomb and Player derive from it. */
class MovableEntity {
public:
     

    // The level's tile array, copied in at spawn from Game::tileBase().
    // Public because tilequery.cpp's farthest-tile search takes a
    // MovableEntity and reaches the map through it.
    Tile *tileBase() const { return tileBase_; }

    // Zero the twelve sound handles.
    void zeroSoundSlots();

    // Nonzero once this tick should remove the entity.  Read by GameTick after
    // a bomb's tick.
    int removeRequested() const { return removeRequested_; }

    // The movement step every entity tick ends in (player, foe, bomb).
    // Returns 1 on the two early outs taken while the entity is dying, 0
    // otherwise.
    unsigned int updateMovement();

    // Readers shared by every entity.  worldstate.cpp's snapshot reads foes
    // and bombs through these; the Player's own callers use them too.  Meaning
    // is noted below only where the name does not already say it.
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
    // The step phase, which framepose.cpp turns into a 0..1 fraction.
    unsigned char turnKind() const             { return turnKind_; }
    double animDuration() const                { return animDuration_; }
    double animStart() const                   { return animStart_; }
    unsigned char type() const                 { return type_; }
    int   dyingStarted() const                 { return dyingStarted_; }
    // The foe's hold flag, or the Player's level-complete flag.  Either way,
    // updateMovement() drops the queued move while it is set.
    int   held() const                         { return held_; }
    // The animation state (see the field below); 10 is the dying animation
    // that DrawObjectShadows' dead/alive conditions test.
    unsigned char anim() const                 { return anim_; }
    // The tile contents picked up this tick, 0 if none (1 = a crystal).
    unsigned char pickedUp() const             { return pickedUp_; }
    // RenderGameFrame's "start the explosion debris" latch: set by the tick,
    // consumed (cleared) by the next frame.
    bool  debrisPending() const                { return field_7a != NULL; }
    void  clearDebrisPending()                 { field_7a = NULL; }
    // Paraglider charges, and whether it is open.
    unsigned char glides() const               { return glides_; }
    int   gliding() const                      { return gliding_; }
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

    // The sound handles: soundobj.cpp writes them for foes, levelsounds.cpp
    // and fixedsounds.cpp for the Player.
    VoicePool *pool9f() const                  { return pool_9f_; }
    void  setPool9f(VoicePool *p)              { pool_9f_ = p; }
    CStaticSoundbuffer *soundA3() const        { return sound_a3_; }
    void  setSoundA3(CStaticSoundbuffer *p)    { sound_a3_ = p; }
    CStaticSoundbuffer *soundA7() const        { return sound_a7_; }
    void  setSoundA7(CStaticSoundbuffer *p)    { sound_a7_ = p; }
    CStaticSoundbuffer *soundAb() const        { return sound_ab_; }
    void  setSoundAb(CStaticSoundbuffer *p)    { sound_ab_ = p; }
    CStaticSoundbuffer *soundAf() const        { return sound_af_; }
    void  setSoundAf(CStaticSoundbuffer *p)    { sound_af_ = p; }
    CStaticSoundbuffer *soundB3() const        { return sound_b3_; }
    void  setSoundB3(CStaticSoundbuffer *p)    { sound_b3_ = p; }
    CStaticSoundbuffer *soundB7() const        { return sound_b7_; }
    void  setSoundB7(CStaticSoundbuffer *p)    { sound_b7_ = p; }
    CStaticSoundbuffer *soundBb() const        { return sound_bb_; }
    void  setSoundBb(CStaticSoundbuffer *p)    { sound_bb_ = p; }
    CStaticSoundbuffer *soundBf() const        { return sound_bf_; }
    void  setSoundBf(CStaticSoundbuffer *p)    { sound_bf_ = p; }
    CStaticSoundbuffer *soundC3() const        { return sound_c3_; }
    void  setSoundC3(CStaticSoundbuffer *p)    { sound_c3_ = p; }
    CStaticSoundbuffer *soundC7() const        { return sound_c7_; }
    void  setSoundC7(CStaticSoundbuffer *p)    { sound_c7_ = p; }
    CStaticSoundbuffer *soundCb() const        { return sound_cb_; }
    void  setSoundCb(CStaticSoundbuffer *p)    { sound_cb_ = p; }
    // Also holds a voice pool, on both the Player and a foe.
    VoicePool *poolCf() const                  { return (VoicePool *)sound_cf_; }
    void  setPoolCf(VoicePool *p)              { sound_cf_ = (CStaticSoundbuffer *)p; }

protected:
    // Our own subclasses use only the base's field work, the three zeroed
    // position floats.
    MovableEntity();

public:
    // Virtual, so a Foe or Bomb is destroyed through a MovableEntity pointer.
    virtual ~MovableEntity();

protected:
    double              now_;
    double             *clock_;
    TickStep        *tickStep_;
    unsigned char       facing_;
    TickStep         tickStepCopy_;
    // World position (U, height, V), read every frame by RenderGameFrame.
    float               posU_;
    float               posY_;
    float               posV_;
    signed char         cellU_;
    signed char         cellV_;
    signed char         heightCell_;
    Tile               *tileBase_;
    double              idleDuration_;  // ms the idle animation runs.
    int                 onStairOrSlide_;
    int                 movingBackwards_;  // Set when turnKind_ is 3 (reversing this step).
    // stepGrace_: the window after a step ends (animStart_ + animDuration_)
    // during which the next step may start without a fresh delay.  stepEnd_:
    // the clock value that window is measured from.
    double              stepGrace_;
    double              stepEnd_;
    int                 conveyorDir_;  // Conveyor: the last direction sent.
    float               fallSpeed_;    // Fall velocity; starts at -3.0.
    // Re-queued from here once kind 4 is released from a blocked step.
    unsigned char       queuedMove_;
    unsigned char       queuedTurn_;
    // The foe's behaviour type: 1 escort, 2 seek a listed tile, 3 seek a
    // flagged tile, 4 return to post, 5 follow, 7 seek the farthest tile
    // (GameTick's foe loop).  Read signed by the step and the chase.
    unsigned char       type_;
    unsigned char       pickedUp_;
    unsigned short      chaseSpeed_;
    // ms one cell step takes; animDuration_ resets to this by kind and state
    // elsewhere.
    double              stepDuration_;
    int                 idleStarted_;
    // The clock the idle timeout counts from.
    double              lastActive_;
    void               *field_7a;
    int                 removeRequested_;
    int                 dyingStarted_;  // Read by RenderGameFrame once the entity starts dying.
    int                 dying_;  // Set on being crushed or blasted; starts the death sequence.
    double              dyingSince_;  // Dying clock; removeRequested_ is set once it is half a second old.
    // The animation state: 0 = still, 0xfa = the idle animation; 0x16..0x1b
    // carry the height curves updateMovement() interpolates between.
    unsigned char       anim_;
    int                 onLift_;  // Riding a lift (kind 9).
    VoicePool          *pool_9f_;
    CStaticSoundbuffer *sound_a3_;
    CStaticSoundbuffer *sound_a7_;
    CStaticSoundbuffer *sound_ab_;
    CStaticSoundbuffer *sound_af_;
    CStaticSoundbuffer *sound_b3_;
    CStaticSoundbuffer *sound_b7_;
    CStaticSoundbuffer *sound_bb_;
    CStaticSoundbuffer *sound_bf_;
    CStaticSoundbuffer *sound_c3_;
    CStaticSoundbuffer *sound_c7_;
    CStaticSoundbuffer *sound_cb_;
    CStaticSoundbuffer *sound_cf_;
    int                 field_d3;
    unsigned char       field_d7;  // The switch this foe is standing on, when it is standing on one.
    int                 field_d8;
    double              lastContact_;      // Foe: the clock of its last contact.
    int                 bombDropRequest_;  // Requests a bomb be dropped this tick.
    unsigned char       field_e8;
    unsigned char       glides_;
    int                 gliding_;
    unsigned char       field_ee;
    int                 held_;
    int                 climbing_;
    unsigned char       teleportPhase_;  // 0 idle, 1 armed, 2 sent.
    double              teleportSince_;  // The clock the current teleport phase began.
    unsigned char       lastMoveDir_;  // The last direction actually moved (as opposed to a turn on the spot).
    unsigned char       fallStartH_;  // The height the current fall began at.
    double              fallStart_;   // The clock the current fall began.
    int                 field_11a;
    unsigned char       slideSlot_;  // The slide being ridden, 0xff for none.
    unsigned char       moveState_;
    int                 falling_;
    unsigned char       field_124;
    // pendingMove_'s turn relative to facing_: 1 forward, 2 and 4 the two
    // turns, 3 reverse, 0 when the move needs no turn at all.
    unsigned char       turnKind_;
    double              field_126;  // The clock the glue pad caught this entity; zero while not stuck.
    int                 field_12e;
    double              animDuration_;  // ms the current animation phase lasts.
    FoePath            *pathfinder_;  // Foe: its FoePath.
    // stepU_ / stepV_: moveDir_ resolved to a cell step, each -1, 0 or +1.
    signed char         stepU_;
    signed char         stepV_;
    signed char         field_141;
    unsigned char       markerCellU_;
    unsigned char       markerCellV_;
    unsigned char       markerCellH_;
    unsigned char       pendingMove_;
    double              animStart_;  // The clock the current animation phase began.
    int                 moveDir_;
    unsigned char       kind_;  // 9 identifies a bomb; a foe's is set from its spawn kind.
    // homeU_ / homeV_ / homeH_: the foe's spawn cell.
    unsigned char       homeU_;
    unsigned char       homeV_;
    unsigned char       homeH_;
    int                 field_156;

private:
     
};

