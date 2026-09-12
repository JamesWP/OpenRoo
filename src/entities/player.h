/* Player -- the player entity: a MovableEntity (movableentity.h, 0x15a
 * bytes) plus its own fields.  Embedded in Game at +0x1751c9, not
 * allocated, so there is no operator new size to tile against; the layout
 * is asserted field by field up to +0x241, the last field any converted
 * code touches.
 *
 * Ours (tileeffects.cpp): the tick, Game::UpdatePlayerTileEffects
 * 0x0041fcb0.  The ctor (PopulatePlayerEntityDefaults 0x0041f900) and dtor
 * (0x0041fa10) are still the game's; the ctor confirms the nine three-entry
 * sound arrays (+0x15e..+0x1ca, zeroed in one loop) and the LinkedList at
 * +0x21c (LinkedList::Init).
 *
 * COHESION_PLAN.md Band 4b pass 1: names are field_<off> unless the code
 * already relied on a meaning.  Outside raw readers remain (gamestate.cpp,
 * worldstate.cpp, cheatcode.cpp and the original RenderGameFrame reach the
 * Player through Game offsets), so the layout stays packed.
 */
#pragma once

#include "layout.h"
#include "movableentity.h"

struct CStaticSoundbuffer;
class Tile;

class __attribute__((packed)) Player : public MovableEntity {
public:
    static const int ORIGIN = 0;

    /* 0x0041fcb0 -- latch the clock, move, respawn, consume the tile
     * underfoot, expire timed effects.  Returns 0; see tileeffects.cpp. */
    unsigned int updateTileEffects();

private:
    Player() = delete;   /* game-owned, embedded in Game */
    KAROO_LAYOUT_REGISTER(Player);

    /* An element type that may sit at any address: taking a packed array's
     * address as a plain CStaticSoundbuffer ** would claim 4-byte alignment. */
    typedef CStaticSoundbuffer *SoundRef __attribute__((aligned(1)));

    Tile *curTile() const;
    int   soundVariant() const;
    void  playAtCell(CStaticSoundbuffer *buf) const;
    void  pickupSound(const SoundRef *arr) const;
    void  listAppend(int code);
    void  endEffect(int code);

    int                 field_15a;        /* +0x15a  indexes sound_19a      */
    SoundRef            sound_15e[3];     /* +0x15e  pickup 8               */
    SoundRef            sound_16a[3];     /* +0x16a  pickup 7               */
    SoundRef            sound_176[3];     /* +0x176  pickup 6               */
    SoundRef            sound_182[3];     /* +0x182  pickup 0xa             */
    SoundRef            sound_18e[3];     /* +0x18e  pickup 0xc             */
    SoundRef            sound_19a[3];     /* +0x19a  pickup 5, by +0x15a    */
    SoundRef            sound_1a6[3];     /* +0x1a6  pickup 9               */
    SoundRef            sound_1b2[3];     /* +0x1b2  pickup 0xb             */
    SoundRef            sound_1be[3];     /* +0x1be  pickup 0xd             */
    double              field_1ca;        /* +0x1ca                         */
    double              field_1d2;        /* +0x1d2  } effect 0xb start/flag */
    int                 field_1da;        /* +0x1da  }                      */
    double              field_1de;        /* +0x1de  } effect 8             */
    int                 field_1e6;        /* +0x1e6  }                      */
    double              field_1ea;        /* +0x1ea  } effect 0xd           */
    int                 field_1f2;        /* +0x1f2  }                      */
    double              field_1f6;        /* +0x1f6  } effect 0xc           */
    int                 field_1fe;        /* +0x1fe  }                      */
    double              field_202;        /* +0x202  } effect 0xa           */
    int                 field_20a;        /* +0x20a  }                      */
    unsigned char       gap_20e[0x21a - 0x20e];
    short               field_21a;        /* +0x21a  pickup counter         */
    /* The game's LinkedList (direct3d.h, 16 bytes) of active effect codes;
     * only ever handed to the game's LinkedList methods. */
    unsigned char       effectList_[16];  /* +0x21c                         */
    unsigned char       gap_22c[0x230 - 0x22c];
    signed char         field_230;        /* +0x230  last random roll       */
    unsigned char       gap_231[0x239 - 0x231];
    int                 field_239;        /* +0x239                         */
    int                 field_23d;        /* +0x23d  crystals               */
};

KAROO_LAYOUT_CHECKS(Player)
{
    KAROO_LAYOUT_AT(field_15a,   0x15a);
    KAROO_LAYOUT_AT(sound_15e,   0x15e);
    KAROO_LAYOUT_AT(sound_16a,   0x16a);
    KAROO_LAYOUT_AT(sound_176,   0x176);
    KAROO_LAYOUT_AT(sound_182,   0x182);
    KAROO_LAYOUT_AT(sound_18e,   0x18e);
    KAROO_LAYOUT_AT(sound_19a,   0x19a);
    KAROO_LAYOUT_AT(sound_1a6,   0x1a6);
    KAROO_LAYOUT_AT(sound_1b2,   0x1b2);
    KAROO_LAYOUT_AT(sound_1be,   0x1be);
    KAROO_LAYOUT_AT(field_1ca,   0x1ca);
    KAROO_LAYOUT_AT(field_1d2,   0x1d2);
    KAROO_LAYOUT_AT(field_1da,   0x1da);
    KAROO_LAYOUT_AT(field_1de,   0x1de);
    KAROO_LAYOUT_AT(field_1e6,   0x1e6);
    KAROO_LAYOUT_AT(field_1ea,   0x1ea);
    KAROO_LAYOUT_AT(field_1f2,   0x1f2);
    KAROO_LAYOUT_AT(field_1f6,   0x1f6);
    KAROO_LAYOUT_AT(field_1fe,   0x1fe);
    KAROO_LAYOUT_AT(field_202,   0x202);
    KAROO_LAYOUT_AT(field_20a,   0x20a);
    KAROO_LAYOUT_AT(field_21a,   0x21a);
    KAROO_LAYOUT_AT(effectList_, 0x21c);
    KAROO_LAYOUT_AT(field_230,   0x230);
    KAROO_LAYOUT_AT(field_239,   0x239);
    KAROO_LAYOUT_AT(field_23d,   0x23d);
    KAROO_LAYOUT_SIZE(0x241);
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_UpdatePlayerTileEffects(Player *self);
