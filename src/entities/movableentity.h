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
 * also read by still-raw code -- UpdateEntityMovement (entitymove.cpp), the
 * foe files, and the original RenderGameFrame -- so the layout is packed and
 * asserted.  Names are the ones the code relies on; field_<off> otherwise.
 */
#pragma once

#include "layout.h"
#include "game.h"

struct CStaticSoundbuffer;

class __attribute__((packed)) MovableEntity {
public:
    static const int ORIGIN = 0;

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

    /* +0x7e, read by GameTick after a bomb's tick: nonzero = remove me.
     * UpdateEntityMovement raises it once moveState reaches 4. */
    int removeRequested() const { return removeRequested_; }

protected:
    /* Our own subclasses: the base's only field work, the three zeroed
     * floats of 0x401000.  The vtable is the subclass's to set. */
    MovableEntity();

    const void         *vtable_;          /* +0x000                         */
    double              now_;             /* +0x004  latched from *clock_   */
    double             *clock_;           /* +0x00c  Game::clock()          */
    Field170a5c        *record_;          /* +0x010  Game::field_170a5c()   */
    unsigned char       facing_;          /* +0x014                         */
    Field170a5c         recordCopy_;      /* +0x015  copied from *record_   */
    unsigned char       gap_01d[0x025 - 0x01d];
    float               posU_;            /* +0x025  } world position, read */
    float               posY_;            /* +0x029  } by RenderGameFrame   */
    float               posV_;            /* +0x02d  }                      */
    signed char         cellU_;           /* +0x031                         */
    signed char         cellV_;           /* +0x032                         */
    signed char         heightCell_;      /* +0x033  read SIGNED (MOVSX)    */
    unsigned char      *tileBase_;        /* +0x034  Game::tileBase()       */
    unsigned char       gap_038[0x048 - 0x038];
    /* One double, though the ctors write it as two dwords: bomb 50.0
     * (0x40490000 at +0x4c), foe and player 20.0 (0x40340000). */
    double              field_48;         /* +0x048                         */
    unsigned char       gap_050[0x058 - 0x050];
    int                 field_58;         /* +0x058                         */
    unsigned char       gap_05c[0x066 - 0x05c];
    /* One double, 200.0 for bomb and player (0x40690000 at +0x6a). */
    double              field_66;         /* +0x066                         */
    unsigned char       gap_06e[0x07a - 0x06e];
    void               *field_7a;         /* +0x07a  RenderGameFrame reads
                                                     and writes it          */
    int                 removeRequested_; /* +0x07e                         */
    int                 field_82;         /* +0x082  read by RenderGameFrame */
    int                 field_86;         /* +0x086                         */
    unsigned char       gap_08a[0x09b - 0x08a];
    int                 field_9b;         /* +0x09b                         */
    unsigned char       gap_09f[0x0a3 - 0x09f];
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
    CStaticSoundbuffer *sound_cf_;        /* +0x0cf  }                      */
    unsigned char       gap_0d3[0x0d8 - 0x0d3];
    int                 field_d8;         /* +0x0d8                         */
    unsigned char       gap_0dc[0x0e4 - 0x0dc];
    int                 field_e4;         /* +0x0e4                         */
    unsigned char       field_e8;         /* +0x0e8                         */
    unsigned char       field_e9;         /* +0x0e9                         */
    int                 field_ea;         /* +0x0ea                         */
    unsigned char       gap_0ee[0x0ef - 0x0ee];
    int                 field_ef;         /* +0x0ef                         */
    unsigned char       gap_0f3[0x0fb - 0x0f3];
    int                 field_fb;         /* +0x0fb                         */
    unsigned char       field_ff;         /* +0x0ff                         */
    unsigned char       gap_100[0x11a - 0x100];
    int                 field_11a;        /* +0x11a                         */
    unsigned char       field_11e;        /* +0x11e                         */
    unsigned char       moveState_;       /* +0x11f  4 = despawn            */
    int                 field_120;        /* +0x120                         */
    unsigned char       field_124;        /* +0x124                         */
    unsigned char       gap_125[0x126 - 0x125];
    int                 field_126;        /* +0x126                         */
    int                 field_12a;        /* +0x12a                         */
    unsigned char       gap_12e[0x145 - 0x12e];
    unsigned char       pendingMove_;     /* +0x145                         */
    unsigned char       gap_146[0x14e - 0x146];
    int                 field_14e;        /* +0x14e                         */
    unsigned char       kind_;            /* +0x152  bomb 9; foe from spawn */
    unsigned char       gap_153[0x156 - 0x153];
    int                 field_156;        /* +0x156                         */

private:
    KAROO_LAYOUT_REGISTER(MovableEntity);
};

KAROO_LAYOUT_CHECKS(MovableEntity)
{
    KAROO_LAYOUT_AT(now_,             0x004);
    KAROO_LAYOUT_AT(clock_,           0x00c);
    KAROO_LAYOUT_AT(record_,          0x010);
    KAROO_LAYOUT_AT(facing_,          0x014);
    KAROO_LAYOUT_AT(recordCopy_,      0x015);
    KAROO_LAYOUT_AT(posU_,            0x025);
    KAROO_LAYOUT_AT(posY_,            0x029);
    KAROO_LAYOUT_AT(posV_,            0x02d);
    KAROO_LAYOUT_AT(cellU_,           0x031);
    KAROO_LAYOUT_AT(cellV_,           0x032);
    KAROO_LAYOUT_AT(heightCell_,      0x033);
    KAROO_LAYOUT_AT(tileBase_,        0x034);
    KAROO_LAYOUT_AT(field_48,         0x048);
    KAROO_LAYOUT_AT(field_58,         0x058);
    KAROO_LAYOUT_AT(field_66,         0x066);
    KAROO_LAYOUT_AT(field_7a,         0x07a);
    KAROO_LAYOUT_AT(removeRequested_, 0x07e);
    KAROO_LAYOUT_AT(field_82,         0x082);
    KAROO_LAYOUT_AT(field_86,         0x086);
    KAROO_LAYOUT_AT(field_9b,         0x09b);
    KAROO_LAYOUT_AT(sound_a3_,        0x0a3);
    KAROO_LAYOUT_AT(sound_a7_,        0x0a7);
    KAROO_LAYOUT_AT(sound_ab_,        0x0ab);
    KAROO_LAYOUT_AT(sound_af_,        0x0af);
    KAROO_LAYOUT_AT(sound_b3_,        0x0b3);
    KAROO_LAYOUT_AT(sound_b7_,        0x0b7);
    KAROO_LAYOUT_AT(sound_bb_,        0x0bb);
    KAROO_LAYOUT_AT(sound_bf_,        0x0bf);
    KAROO_LAYOUT_AT(sound_c3_,        0x0c3);
    KAROO_LAYOUT_AT(sound_c7_,        0x0c7);
    KAROO_LAYOUT_AT(sound_cb_,        0x0cb);
    KAROO_LAYOUT_AT(sound_cf_,        0x0cf);
    KAROO_LAYOUT_AT(field_d8,         0x0d8);
    KAROO_LAYOUT_AT(field_e4,         0x0e4);
    KAROO_LAYOUT_AT(field_e8,         0x0e8);
    KAROO_LAYOUT_AT(field_e9,         0x0e9);
    KAROO_LAYOUT_AT(field_ea,         0x0ea);
    KAROO_LAYOUT_AT(field_ef,         0x0ef);
    KAROO_LAYOUT_AT(field_fb,         0x0fb);
    KAROO_LAYOUT_AT(field_ff,         0x0ff);
    KAROO_LAYOUT_AT(field_11a,        0x11a);
    KAROO_LAYOUT_AT(field_11e,        0x11e);
    KAROO_LAYOUT_AT(moveState_,       0x11f);
    KAROO_LAYOUT_AT(field_120,        0x120);
    KAROO_LAYOUT_AT(field_124,        0x124);
    KAROO_LAYOUT_AT(field_126,        0x126);
    KAROO_LAYOUT_AT(field_12a,        0x12a);
    KAROO_LAYOUT_AT(pendingMove_,     0x145);
    KAROO_LAYOUT_AT(field_14e,        0x14e);
    KAROO_LAYOUT_AT(kind_,            0x152);
    KAROO_LAYOUT_AT(field_156,        0x156);
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
