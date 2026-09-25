/* MovableEntity -- the base shared by every moving entity: Bomb, Foe and the
 * Player.  Ghidra struct `MovableEntity`, 0x15a bytes (the derived classes'
 * own fields start at +0x15a: the bomb's two sound handles, the foe's type
 * byte).
 *
 * The base's own code is small, and all of it is ours (movableentity.cpp):
 *
 *   PopulateMovableEntityBase    0x00438720  ctor: the level-object base ctor
 *                                            0x401000 (vtable 0x45d290, zero
 *                                            +0x25/+0x29/+0x2d), then vtable
 *                                            0x45d6a4
 *   ZeroEntitySoundSlotPointers  0x0043ad60  zero the twelve sound handles
 *                                            +0xa3..+0xcf
 *   DestroyMovableEntityBase     0x00438760  dtor: vtable 0x45d6a4, then the
 *                                            level-object base dtor 0x401060
 *                                            (vtable 0x45d290)
 *
 * Bomb derives from it in C++ and constructs through MovableEntity().  The
 * Foe (0x411ff0 / 0x412160) and Player (0x41f900 / 0x41fa10) ctors and dtors
 * are still the game's; they reach the base through the three exports below,
 * which patch.py routes every E8 and E9 site to -- including the five EH
 * unwind funclets that undo a partly built entity.
 *
 * The fields are every entity field a converted class touches.  Most are
 * also read by still-raw code -- the foe files, and the original RenderGameFrame -- so the layout is packed and
 * asserted.  Names are the ones the code relies on; field_<off> otherwise.
 */
#pragma once

#include "layout.h"
#include "game.h"

struct CStaticSoundbuffer;
struct VoicePool;
class FoePath;

class __attribute__((packed)) MovableEntity {
public:
    static const int ORIGIN = 0;

    /* The level's tile array, copied in at spawn from Game::tileBase().
     * Public because tilequery.cpp's farthest-tile search takes a
     * MovableEntity and reaches the map through it. */
    unsigned char *tileBase() const { return tileBase_; }

    /* 0x0043ad60 -- zero the twelve sound handles, in the original's store
     * order.  Public: the Foe and Player ctors (still the game's) call it
     * through Sim_ZeroEntitySoundSlotPointers. */
    void zeroSoundSlots();

    /* 0x00438720 and 0x00438760 exactly as the game's derived ctors and dtors
     * expect them, including the transient vtable stores (0x45d290, then
     * 0x45d6a4 / the reverse) that the derived class overwrites or that the
     * free follows.  Not used by our own subclasses. */
    void populateBaseForGame();
    void destroyBaseForGame();

    /* Destroy through the object's OWN vtable slot 0 -- the game built these
     * objects and still owns their vtables, so the call has to go through
     * the pointer the object carries, not through anything of ours.
     *
     * `MOV EDX,[ECX]; PUSH flags; CALL [EDX]` in the original.  `flags` is
     * the MSVC scalar-deleting-destructor flag word; the removes pass 1.
     *
     * This is the one place a MovableEntity is destroyed by a caller that
     * does not know which subclass it holds (Object_DestroyAndCompactId,
     * objectremove.cpp, reached from both foe.cpp and bomb.cpp).  It lives
     * here because the vtable is the base's field -- COHESION_PLAN.md Band
     * 7d, which moved it out of a `void *self` typedef in that file. */
    void destroyViaVtable(int flags);

    /* +0x7e, read by GameTick after a bomb's tick: nonzero = remove me.
     * UpdateEntityMovement raises it once moveState reaches 4. */
    int removeRequested() const { return removeRequested_; }

    /* 0x00438770 -- the movement step every entity tick ends in (player,
     * foe, bomb); movableentity.cpp.  Returns 1 in AL on the two early outs. */
    unsigned int updateMovement();

    /* ── readers shared by every entity (Player, Foe, Bomb) ─────────────
     * worldstate.cpp's snapshot reads foes and bombs through these; the
     * Player's callers too.  Meanings unknown unless noted. */
    unsigned char facing() const               { return facing_; }
    float posU() const                         { return posU_; }
    float posY() const                         { return posY_; }
    float posV() const                         { return posV_; }
    signed char cellU() const                  { return cellU_; }
    signed char cellV() const                  { return cellV_; }
    signed char heightCell() const             { return heightCell_; }
    /* +0x40: taking a stair or sliding this tick (the player's is
     * Game+0x175209); stops UpdateViewTransform's camera lift. */
    int   onStairOrSlide() const               { return onStairOrSlide_; }
    /* The step phase framepose.cpp turns into a 0..1 fraction. */
    unsigned char turnKind() const             { return turnKind_; }
    double animDuration() const                { return animDuration_; }
    double animStart() const                   { return animStart_; }
    unsigned char type() const                 { return type_; }
    int   dyingStarted() const                 { return dyingStarted_; }
    /* +0xef: the foe's hold flag; the Player's level-complete flag.
     * Either way updateMovement() drops the queued move. */
    int   held() const                         { return held_; }
    /* +0x9a: the animation state (see the field); 10 is the dying anim that
     * DrawObjectShadows' `dead`/`alive` conditions test. */
    unsigned char anim() const                 { return anim_; }
    /* +0x63: the tile contents picked up this tick (1 = a crystal). */
    unsigned char pickedUp() const             { return pickedUp_; }
    /* +0x7a: RenderGameFrame's "start the explosion debris" latch -- set by
     * the tick, consumed (zeroed) by the next frame. */
    bool  debrisPending() const                { return field_7a != NULL; }
    void  clearDebrisPending()                 { field_7a = NULL; }
    /* +0xe9 / +0xea: paraglider charges, and whether it is open. */
    unsigned char glides() const               { return glides_; }
    int   gliding() const                      { return gliding_; }
    /* +0x14e: the direction being moved in, 0 while still.  1 = -V,
     * 2 = +U, 3 = +V, 4 = -U (updateMovement's stepU_/stepV_ table). */
    int   moveDir() const                      { return moveDir_; }
    unsigned char kind() const                 { return kind_; }
    unsigned char homeU() const                { return homeU_; }
    unsigned char homeV() const                { return homeV_; }
    unsigned char homeH() const                { return homeH_; }
    /* +0x11f: 4 = despawn (the "kaputo" cheat sets it on every foe); the
     * Player's is nonzero while dead / timed out (3 = time-out). */
    unsigned char moveState() const            { return moveState_; }
    void  setMoveState(unsigned char s)        { moveState_ = s; }

    /* ── the sound handles (soundobj.cpp for foes; levelsounds.cpp and
     *    fixedsounds.cpp for the Player) ──────────────────────────────── */
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
    /* +0xcf holds a voice pool on the player and on a foe. */
    VoicePool *poolCf() const                  { return (VoicePool *)sound_cf_; }
    void  setPoolCf(VoicePool *p)              { sound_cf_ = (CStaticSoundbuffer *)p; }

protected:
    /* Our own subclasses: the base's only field work, the three zeroed
     * floats of 0x401000.  The vtable is the subclass's to set. */
    MovableEntity();

    const void         *vtable_;          /* +0x000                         */
    double              now_;             /* +0x004  latched from *clock_   */
    double             *clock_;           /* +0x00c  Game::clock()          */
    TickStep        *tickStep_;          /* +0x010  Game::tickStep()     */
    unsigned char       facing_;          /* +0x014                         */
    TickStep         tickStepCopy_;      /* +0x015  copied from *tickStep_   */
    unsigned char       gap_01d[0x025 - 0x01d];
    float               posU_;            /* +0x025  } world position, read */
    float               posY_;            /* +0x029  } by RenderGameFrame   */
    float               posV_;            /* +0x02d  }                      */
    signed char         cellU_;           /* +0x031                         */
    signed char         cellV_;           /* +0x032                         */
    signed char         heightCell_;      /* +0x033  read SIGNED (MOVSX)    */
    unsigned char      *tileBase_;        /* +0x034  Game::tileBase()       */
    /* One double, though the foe ctor writes it as two dwords: 1000.0
     * (0 at +0x38, 0x408f4000 at +0x3c). */
    double              idleDuration_; /* +0x038  ms the idle anim runs   */
    /* +0x040: recomputed every movement tick -- 1 while taking a stair
     * or sliding (see updateMovement); UpdateViewTransform then skips its
     * lift-over-a-blocking-cell. */
    int                 onStairOrSlide_;
    int                 movingBackwards_; /* +0x044  turnKind_ 3: reversing */
    /* One double, though the ctors write it as two dwords: bomb 50.0
     * (0x40490000 at +0x4c), foe and player 20.0 (0x40340000). */
    double              stepGrace_;    /* +0x048  } the window after a    */
    double              stepEnd_;      /* +0x050  } step (animStart_ +
                                       *   animDuration_) ends          */
    int                 conveyorDir_;  /* +0x058  conveyor: last way sent */
    /* Set as the bits 0xc0400000, i.e. -3.0f, and read as a float. */
    float               fallSpeed_;    /* +0x05c  fall velocity, -3.0     */
    unsigned char       queuedMove_;   /* +0x060  } re-queued by kind 4   */
    unsigned char       queuedTurn_;   /* +0x061  } after a blocked step  */
    /* The foe's behaviour type (1 escort, 2 seek listed tile, 3 seek
     * flagged tile, 4 return to post, 5 follow, 7 farthest -- GameTick's
     * foe loop).  Read signed by the step and the chase. */
    unsigned char       type_;            /* +0x062                         */
    unsigned char       pickedUp_;    /* +0x063  the tile contents taken
                                       * this tick, 0 if none          */
    unsigned short      chaseSpeed_;  /* +0x064  foe: chase speed        */
    /* One double: 200.0 for bomb and player (0x40690000 at +0x6a); the foe
     * spawn stores 500.0 (kind 2) or 700.0 (kind 3). */
    /* +0x066: ms one cell step takes -- animDuration_ resets to it.
     * Player 100/200/400 by state, foe 500/700 by type, bomb 200. */
    double              stepDuration_;
    int                 idleStarted_;  /* +0x06e  the idle anim is running */
    /* +0x072: the clock the idle timeout counts from.  The foe spawner
     * staggers it by 1500 ms per foe, so idle animations do not sync. */
    double              lastActive_;
    void               *field_7a;         /* +0x07a  RenderGameFrame reads
                                                     and writes it          */
    int                 removeRequested_; /* +0x07e                         */
    int                 dyingStarted_; /* +0x082  read by RenderGameFrame */
    int                 dying_;        /* +0x086  crushed / blasted       */
    double              dyingSince_;   /* +0x08a  removed 0.5 s later     */
    unsigned char       gap_092[0x09a - 0x092];
    /* +0x09a: the animation state.  0 = still, 0xfa = the idle anim;
     * 0x16..0x1b carry the height curves updateMovement() interpolates. */
    unsigned char       anim_;
    int                 onLift_;       /* +0x09b  riding a lift (kind 9)  */
    VoicePool          *pool_9f_;         /* +0x09f  foe: a voice pool      */
    CStaticSoundbuffer *sound_a3_;        /* +0x0a3  } the twelve handles   */
    CStaticSoundbuffer *sound_a7_;        /* +0x0a7  } zeroSoundSlots()     */
    CStaticSoundbuffer *sound_ab_;        /* +0x0ab  } clears               */
    CStaticSoundbuffer *sound_af_;        /* +0x0af  }                      */
    CStaticSoundbuffer *sound_b3_;        /* +0x0b3  }                      */
    CStaticSoundbuffer *sound_b7_;        /* +0x0b7  }                      */
    CStaticSoundbuffer *sound_bb_;        /* +0x0bb  }                      */
    CStaticSoundbuffer *sound_bf_;        /* +0x0bf  }                      */
    CStaticSoundbuffer *sound_c3_;        /* +0x0c3  }                      */
    CStaticSoundbuffer *sound_c7_;        /* +0x0c7  }                      */
    CStaticSoundbuffer *sound_cb_;        /* +0x0cb  }                      */
    CStaticSoundbuffer *sound_cf_;        /* +0x0cf  } (a voice pool on a foe) */
    int                 field_d3;         /* +0x0d3                         */
    unsigned char       field_d7;         /* +0x0d7  foe: switch it is on   */
    int                 field_d8;         /* +0x0d8                         */
    double              lastContact_;  /* +0x0dc  foe: last contact time  */
    int                 bombDropRequest_; /* +0x0e4  drop a bomb this tick       */
    unsigned char       field_e8;         /* +0x0e8                         */
    unsigned char       glides_;       /* +0x0e9  paraglider charges      */
    int                 gliding_;      /* +0x0ea  the paraglider is open  */
    unsigned char       field_ee;         /* +0x0ee                         */
    int                 held_;         /* +0x0ef  queued moves dropped    */
    unsigned char       gap_0f3[0x0fb - 0x0f3];
    int                 climbing_;     /* +0x0fb  stepping up a height    */
    unsigned char       teleportPhase_;/* +0x0ff 0 idle, 1 armed, 2 sent  */
    double              teleportSince_;/* +0x100 clock the phase began    */
    unsigned char       lastMoveDir_;  /* +0x108  the last direction moved */
    unsigned char       gap_109[0x111 - 0x109];
    unsigned char       fallStartH_;   /* +0x111  height the fall began at*/
    double              fallStart_;    /* +0x112  clock the fall began    */
    int                 field_11a;        /* +0x11a                         */
    unsigned char       slideSlot_;    /* +0x11e  slide ridden, 0xff none */
    unsigned char       moveState_;       /* +0x11f  4 = despawn            */
    int                 falling_;      /* +0x120  in the air              */
    unsigned char       field_124;        /* +0x124                         */
    /* +0x125: pendingMove_ relative to facing_ -- 1 forward, 2 and 4 the
     * two turns, 3 reverse.  0 when the move needs no turn. */
    unsigned char       turnKind_;
    /* One double, though the ctors write it as two zero dwords. */
    double              field_126;        /* +0x126                         */
    int                 field_12e;        /* +0x12e                         */
    double              animDuration_; /* +0x132  ms the phase lasts      */
    unsigned char       gap_13a[0x13b - 0x13a];
    FoePath            *pathfinder_;      /* +0x13b  foe: its FoePath       */
    signed char         stepU_;        /* +0x13f  } moveDir_ as a cell   */
    signed char         stepV_;        /* +0x140  } step, -1 / 0 / +1    */
    signed char         field_141;        /* +0x141  }                      */
    unsigned char       markerCellU_;  /* +0x142  } player: the marker-4 */
    unsigned char       markerCellV_;  /* +0x143  } cell (u, v, h)       */
    unsigned char       markerCellH_;  /* +0x144  }                      */
    unsigned char       pendingMove_;     /* +0x145                         */
    double              animStart_;    /* +0x146  clock the phase began   */
    int                 moveDir_;     /* +0x14e  0 = still, else 1..4   */
    unsigned char       kind_;            /* +0x152  bomb 9; foe from spawn */
    unsigned char       homeU_;           /* +0x153  } foe: its spawn cell  */
    unsigned char       homeV_;           /* +0x154  }                      */
    unsigned char       homeH_;           /* +0x155  }                      */
    int                 field_156;        /* +0x156                         */

private:
    KAROO_LAYOUT_REGISTER(MovableEntity);
};

KAROO_LAYOUT_CHECKS(MovableEntity)
{
    KAROO_LAYOUT_AT(now_,              0x004);
    KAROO_LAYOUT_AT(clock_,            0x00c);
    KAROO_LAYOUT_AT(tickStep_,         0x010);
    KAROO_LAYOUT_AT(facing_,           0x014);
    KAROO_LAYOUT_AT(tickStepCopy_,     0x015);
    KAROO_LAYOUT_AT(posU_,             0x025);
    KAROO_LAYOUT_AT(posY_,             0x029);
    KAROO_LAYOUT_AT(posV_,             0x02d);
    KAROO_LAYOUT_AT(cellU_,            0x031);
    KAROO_LAYOUT_AT(cellV_,            0x032);
    KAROO_LAYOUT_AT(heightCell_,       0x033);
    KAROO_LAYOUT_AT(tileBase_,         0x034);
    KAROO_LAYOUT_AT(idleDuration_,          0x038);
    KAROO_LAYOUT_AT(onStairOrSlide_,   0x040);
    KAROO_LAYOUT_AT(movingBackwards_,  0x044);
    KAROO_LAYOUT_AT(stepGrace_,          0x048);
    KAROO_LAYOUT_AT(stepEnd_,          0x050);
    KAROO_LAYOUT_AT(conveyorDir_,      0x058);
    KAROO_LAYOUT_AT(fallSpeed_,        0x05c);
    KAROO_LAYOUT_AT(queuedMove_,       0x060);
    KAROO_LAYOUT_AT(queuedTurn_,       0x061);
    KAROO_LAYOUT_AT(type_,             0x062);
    KAROO_LAYOUT_AT(pickedUp_,         0x063);
    KAROO_LAYOUT_AT(chaseSpeed_,       0x064);
    KAROO_LAYOUT_AT(stepDuration_,     0x066);
    KAROO_LAYOUT_AT(idleStarted_,      0x06e);
    KAROO_LAYOUT_AT(lastActive_,       0x072);
    KAROO_LAYOUT_AT(field_7a,          0x07a);
    KAROO_LAYOUT_AT(removeRequested_,  0x07e);
    KAROO_LAYOUT_AT(dyingStarted_,     0x082);
    KAROO_LAYOUT_AT(dying_,            0x086);
    KAROO_LAYOUT_AT(dyingSince_,       0x08a);
    KAROO_LAYOUT_AT(anim_,             0x09a);
    KAROO_LAYOUT_AT(onLift_,           0x09b);
    KAROO_LAYOUT_AT(pool_9f_,          0x09f);
    KAROO_LAYOUT_AT(sound_a3_,         0x0a3);
    KAROO_LAYOUT_AT(sound_a7_,         0x0a7);
    KAROO_LAYOUT_AT(sound_ab_,         0x0ab);
    KAROO_LAYOUT_AT(sound_af_,         0x0af);
    KAROO_LAYOUT_AT(sound_b3_,         0x0b3);
    KAROO_LAYOUT_AT(sound_b7_,         0x0b7);
    KAROO_LAYOUT_AT(sound_bb_,         0x0bb);
    KAROO_LAYOUT_AT(sound_bf_,         0x0bf);
    KAROO_LAYOUT_AT(sound_c3_,         0x0c3);
    KAROO_LAYOUT_AT(sound_c7_,         0x0c7);
    KAROO_LAYOUT_AT(sound_cb_,         0x0cb);
    KAROO_LAYOUT_AT(sound_cf_,         0x0cf);
    KAROO_LAYOUT_AT(field_d3,          0x0d3);
    KAROO_LAYOUT_AT(field_d7,          0x0d7);
    KAROO_LAYOUT_AT(field_d8,          0x0d8);
    KAROO_LAYOUT_AT(lastContact_,          0x0dc);
    KAROO_LAYOUT_AT(bombDropRequest_, 0x0e4);
    KAROO_LAYOUT_AT(field_e8,          0x0e8);
    KAROO_LAYOUT_AT(glides_,           0x0e9);
    KAROO_LAYOUT_AT(gliding_,          0x0ea);
    KAROO_LAYOUT_AT(field_ee,          0x0ee);
    KAROO_LAYOUT_AT(held_,             0x0ef);
    KAROO_LAYOUT_AT(climbing_,         0x0fb);
    KAROO_LAYOUT_AT(teleportPhase_,    0x0ff);
    KAROO_LAYOUT_AT(teleportSince_,    0x100);
    KAROO_LAYOUT_AT(lastMoveDir_,      0x108);
    KAROO_LAYOUT_AT(fallStartH_,       0x111);
    KAROO_LAYOUT_AT(fallStart_,        0x112);
    KAROO_LAYOUT_AT(field_11a,         0x11a);
    KAROO_LAYOUT_AT(slideSlot_,        0x11e);
    KAROO_LAYOUT_AT(moveState_,        0x11f);
    KAROO_LAYOUT_AT(falling_,          0x120);
    KAROO_LAYOUT_AT(field_124,         0x124);
    KAROO_LAYOUT_AT(turnKind_,         0x125);
    KAROO_LAYOUT_AT(field_126,         0x126);
    KAROO_LAYOUT_AT(field_12e,         0x12e);
    KAROO_LAYOUT_AT(animDuration_,     0x132);
    KAROO_LAYOUT_AT(pathfinder_,       0x13b);
    KAROO_LAYOUT_AT(stepU_,            0x13f);
    KAROO_LAYOUT_AT(stepV_,            0x140);
    KAROO_LAYOUT_AT(field_141,         0x141);
    KAROO_LAYOUT_AT(markerCellU_,         0x142);
    KAROO_LAYOUT_AT(markerCellV_,         0x143);
    KAROO_LAYOUT_AT(markerCellH_,         0x144);
    KAROO_LAYOUT_AT(pendingMove_,      0x145);
    KAROO_LAYOUT_AT(animStart_,        0x146);
    KAROO_LAYOUT_AT(moveDir_,          0x14e);
    KAROO_LAYOUT_AT(kind_,             0x152);
    KAROO_LAYOUT_AT(homeU_,            0x153);
    KAROO_LAYOUT_AT(homeV_,            0x154);
    KAROO_LAYOUT_AT(homeH_,            0x155);
    KAROO_LAYOUT_AT(field_156,         0x156);
    /* Relied on: every derived class's own fields start here. */
    KAROO_LAYOUT_SIZE(0x15a);
}

/* ─── Exports -- patch.py routes the three originals here ─────────────── */
extern "C" __declspec(dllexport) MovableEntity *__attribute__((thiscall))
Sim_PopulateMovableEntityBase(MovableEntity *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_ZeroEntitySoundSlotPointers(MovableEntity *self);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_DestroyMovableEntityBase(MovableEntity *self);

/* 0x00438740 -- vtable slot 0 of the base's own (now ours) one-slot table:
 * the dtor body, then the game heap's Free2 when bit 0 is set. */
extern "C" __declspec(dllexport) MovableEntity *__attribute__((thiscall))
Sim_DeleteMovableEntityWithFlags(MovableEntity *self, unsigned int flags);

/* UpdateEntityMovement 0x00438770, the shared movement step for every
 * entity (player, foe, bomb): a shim over MovableEntity::updateMovement(). */
extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_UpdateEntityMovement(MovableEntity *self);
