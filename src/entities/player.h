/* Player -- the player entity: a MovableEntity (movableentity.h, 0x15a
 * bytes) plus its own fields.  Embedded in Game at +0x1751c9 (reached as
 * Game::player()), not allocated, so there is no operator new size to tile
 * against; the layout is asserted field by field up to +0x241.  Nothing in
 * the ctor or the tick touches past +0x23d, so +0x241 is taken as the end:
 * Game+0x175412, which keypress.cpp writes, is +0x249 and is left as Game's.
 *
 * Ours (tileeffects.cpp): the tick, Game::UpdatePlayerTileEffects
 * 0x0041fcb0.  The ctor (PopulatePlayerEntityDefaults 0x0041f900) and dtor
 * (0x0041fa10) are still the game's; the ctor confirms the nine three-entry
 * sound arrays (+0x15e..+0x1ca, zeroed in one loop) and the LinkedList at
 * +0x21c (LinkedList::Init).
 *
 * Every Player field our code touches goes through the accessors below,
 * except determinism.cpp's hash table, which hashes opaque byte ranges by
 * design (see there).
 * COHESION_PLAN.md Band 4b pass 1: names are field_<off> / fieldXxx()
 * unless the code already relied on a meaning.  The original
 * RenderGameFrame still reads the Player by offset, so the layout stays
 * packed.
 */
#pragma once

#include "layout.h"
#include "movableentity.h"

struct CStaticSoundbuffer;
struct LinkedList;
struct VoicePool;
class Tile;

class __attribute__((packed)) Player : public MovableEntity {
public:
    static const int ORIGIN = 0;

    /* 0x0041fcb0 -- latch the clock, move, respawn, consume the tile
     * underfoot, expire timed effects.  Returns 0; see tileeffects.cpp. */
    unsigned int updateTileEffects();

    /* The tile the player stands on, from its SIGNED cell bytes -- the
     * arithmetic every caller used by hand. */
    Tile *curTile() const;

    /* ── the level-object base ──────────────────────────────────────── */
    void  setClock(double *c)                  { clock_ = c; }
    void  setRecord(Field170a5c *r)            { record_ = r; }
    void  setTileBase(unsigned char *b)        { tileBase_ = b; }
    unsigned char facing() const               { return facing_; }
    void  setFacing(unsigned char f)           { facing_ = f; }
    float posU() const                         { return posU_; }
    float posY() const                         { return posY_; }
    float posV() const                         { return posV_; }
    /* Stored in the order u, y, v -- as each caller stored them. */
    void  setPos(float u, float y, float v)    { posU_ = u; posY_ = y; posV_ = v; }
    signed char cellU() const                  { return cellU_; }
    signed char cellV() const                  { return cellV_; }
    signed char heightCell() const             { return heightCell_; }
    void  setCell(unsigned char u, unsigned char v, unsigned char h)
    {
        cellU_ = (signed char)u;
        cellV_ = (signed char)v;
        heightCell_ = (signed char)h;
    }

    /* ── MovableEntity fields, meanings unknown unless noted ────────── */
    /* The doubles are written by some callers as two dword stores; one
     * double store is the same bits (COHESION_PLAN.md, template 3). */
    void  setField38(double d)                 { field_38 = d; }
    void  setField44(int n)                    { field_44 = n; }
    void  setField58(int n)                    { field_58 = n; }
    /* +0x66: the move duration default (entitymove.cpp copies it into
     * +0x132): 200.0 normally, 100.0 / 400.0 under pickups 0xa / 0xc. */
    void  setField66(double d)                 { field_66 = d; }
    void  setField6e(int n)                    { field_6e = n; }
    void  setField72(double d)                 { field_72 = d; }
    void  setField86(int n)                    { field_86 = n; }
    void  setField9a(unsigned char b)          { field_9a = b; }
    void  setField9b(int n)                    { field_9b = n; }
    void  setFieldD3(int n)                    { field_d3 = n; }
    /* +0xd7: the switch the player stands on, 0xff = none. */
    unsigned char switchSlot() const           { return field_d7; }
    void  setSwitchSlot(unsigned char s)       { field_d7 = s; }
    int   fieldD8() const                      { return field_d8; }
    void  setFieldD8(int n)                    { field_d8 = n; }
    void  setFieldDc(double d)                 { field_dc = d; }
    /* +0xe4: the player's bomb-drop request. */
    int   fieldE4() const                      { return field_e4; }
    void  setFieldE4(int n)                    { field_e4 = n; }
    unsigned char fieldE8() const              { return field_e8; }
    void  setFieldE8(unsigned char b)          { field_e8 = b; }
    unsigned char fieldE9() const              { return field_e9; }
    void  setFieldE9(unsigned char b)          { field_e9 = b; }
    int   fieldEa() const                      { return field_ea; }
    void  setFieldEa(int n)                    { field_ea = n; }
    /* +0xef: gamestate.cpp's level-complete flag (0 -> 1 on exit). */
    int   fieldEf() const                      { return field_ef; }
    void  setFieldEf(int n)                    { field_ef = n; }
    void  setFieldFf(unsigned char b)          { field_ff = b; }
    void  setField108(unsigned char b)         { field_108 = b; }
    unsigned char field111() const             { return field_111; }
    void  setField11a(int n)                   { field_11a = n; }
    void  setField11e(unsigned char b)         { field_11e = b; }
    /* +0x11f: nonzero while dead / timed out (3 = time-out). */
    unsigned char moveState() const            { return moveState_; }
    void  setMoveState(unsigned char s)        { moveState_ = s; }
    /* Foe::checkPlayerContact writes through it. */
    unsigned char *moveStateRef()              { return &moveState_; }
    int   field120() const                     { return field_120; }
    void  setField120(int n)                   { field_120 = n; }
    void  setField126(double d)                { field_126 = d; }
    void  setField12e(int n)                   { field_12e = n; }
    signed char field13f() const               { return field_13f; }
    signed char field140() const               { return field_140; }
    signed char field141() const               { return field_141; }
    /* +0x142..+0x144: the marker-4 cell SetupLevelObjects looks up (u, v,
     * h) -- worldstate.cpp's level exit.  Written through the pointer. */
    unsigned char field142() const             { return field_142; }
    unsigned char field143() const             { return field_143; }
    unsigned char field144() const             { return field_144; }
    unsigned char *field142Ref()               { return &field_142; }
    void  setPendingMove(unsigned char m)      { pendingMove_ = m; }
    double field146() const                    { return field_146; }
    int   field14e() const                     { return field_14e; }
    void  setField14e(int n)                   { field_14e = n; }
    void  setKind(unsigned char k)             { kind_ = k; }
    /* +0x153..+0x155: the start cell (u, v, h), from the marker-3 lookup,
     * which writes it through the pointer. */
    unsigned char homeU() const                { return homeU_; }
    unsigned char homeV() const                { return homeV_; }
    unsigned char homeH() const                { return homeH_; }
    unsigned char *homeRef()                   { return &homeU_; }

    /* ── the base's sound handles (levelsounds.cpp, fixedsounds.cpp) ── */
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
    /* +0xcf holds a voice pool on the player, as on a foe. */
    VoicePool *poolCf() const                  { return (VoicePool *)sound_cf_; }
    void  setPoolCf(VoicePool *p)              { sound_cf_ = (CStaticSoundbuffer *)p; }

    /* ── the Player's own fields ────────────────────────────────────── */
    /* +0x15a: the world's sound variant (0 Egypt, 1 Candy, 2 Space --
     * levelsounds.cpp); indexes bank SND_19A. */
    int   field15a() const                     { return field_15a; }
    void  setField15a(int n)                   { field_15a = n; }

    /* The nine three-entry pickup sound banks, +0x15e + 0x0c * bank. */
    enum PickupBank {
        SND_15E, SND_16A, SND_176, SND_182, SND_18E,
        SND_19A, SND_1A6, SND_1B2, SND_1BE,
    };
    CStaticSoundbuffer *pickupSound(int bank, int i) const { return pickupSounds_[bank][i]; }
    void  setPickupSound(int bank, int i, CStaticSoundbuffer *p) { pickupSounds_[bank][i] = p; }

    double field1ca() const                    { return field_1ca; }
    void  setField1ca(double d)                { field_1ca = d; }
    void  setField1da(int n)                   { field_1da = n; }
    /* +0x1e6: effect 8's flag; worldstate.cpp's freeze timer. */
    int   field1e6() const                     { return field_1e6; }
    void  setField1e6(int n)                   { field_1e6 = n; }
    void  setField1ea(double d)                { field_1ea = d; }
    int   field1f2() const                     { return field_1f2; }
    void  setField1f2(int n)                   { field_1f2 = n; }
    void  setField1fe(int n)                   { field_1fe = n; }
    void  setField20a(int n)                   { field_20a = n; }
    /* Stored in the order +0x20e, +0x212, +0x216, as the caller does. */
    void  setField20e(float a, float b, float c)
    {
        field_20e = a;
        field_212 = b;
        field_216 = c;
    }
    /* +0x21a: the pickup counter. */
    unsigned short field21a() const            { return field_21a; }
    void  setField21a(unsigned short n)        { field_21a = n; }
    /* The active timed-effect list (a game LinkedList). */
    void  appendEffect(int code);
    void  clearEffects();
    /* +0x22c: the running score total, persisted to .sav. */
    int   field22c() const                     { return field_22c; }
    void  setField22c(int n)                   { field_22c = n; }
    void  setField230(signed char c)           { field_230 = c; }
    void  setField231(double d)                { field_231 = d; }
    /* +0x239: lives (a dword; byte readers take the low byte). */
    int   field239() const                     { return field_239; }
    void  setField239(int n)                   { field_239 = n; }
    /* +0x23d: crystals collected. */
    int   field23d() const                     { return field_23d; }
    void  setField23d(int n)                   { field_23d = n; }

private:
    Player() = delete;   /* game-owned, embedded in Game */
    KAROO_LAYOUT_REGISTER(Player);

    /* An element type that may sit at any address: taking a packed array's
     * address as a plain CStaticSoundbuffer ** would claim 4-byte alignment. */
    typedef CStaticSoundbuffer *SoundRef __attribute__((aligned(1)));

    int   soundVariant() const;
    void  playAtCell(CStaticSoundbuffer *buf) const;
    void  pickupSound(const SoundRef *arr) const;
    /* The raw bytes cast, not &effectList_: a LinkedList * to a packed
     * member would trip -Waddress-of-packed-member. */
    LinkedList *effects() { return (LinkedList *)effectList_; }
    void  endEffect(int code);

    int                 field_15a;        /* +0x15a  world sound variant    */
    SoundRef            pickupSounds_[9][3]; /* +0x15e  PickupBank          */
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
    float               field_20e;        /* +0x20e  } the marker-4 cell as */
    float               field_212;        /* +0x212  } floats (u, h, v)     */
    float               field_216;        /* +0x216  }                      */
    unsigned short      field_21a;        /* +0x21a  pickup counter         */
    /* The game's LinkedList (linkedlist.h, 16 bytes) of active effect
     * codes; only ever handed to the game's LinkedList methods. */
    unsigned char       effectList_[16];  /* +0x21c                         */
    int                 field_22c;        /* +0x22c  running score          */
    signed char         field_230;        /* +0x230  last random roll       */
    double              field_231;        /* +0x231                         */
    int                 field_239;        /* +0x239  lives                  */
    int                 field_23d;        /* +0x23d  crystals               */
};

KAROO_LAYOUT_CHECKS(Player)
{
    KAROO_LAYOUT_AT(field_15a,     0x15a);
    KAROO_LAYOUT_AT(pickupSounds_, 0x15e);
    KAROO_LAYOUT_AT(field_1ca,     0x1ca);
    KAROO_LAYOUT_AT(field_1d2,     0x1d2);
    KAROO_LAYOUT_AT(field_1da,     0x1da);
    KAROO_LAYOUT_AT(field_1de,     0x1de);
    KAROO_LAYOUT_AT(field_1e6,     0x1e6);
    KAROO_LAYOUT_AT(field_1ea,     0x1ea);
    KAROO_LAYOUT_AT(field_1f2,     0x1f2);
    KAROO_LAYOUT_AT(field_1f6,     0x1f6);
    KAROO_LAYOUT_AT(field_1fe,     0x1fe);
    KAROO_LAYOUT_AT(field_202,     0x202);
    KAROO_LAYOUT_AT(field_20a,     0x20a);
    KAROO_LAYOUT_AT(field_20e,     0x20e);
    KAROO_LAYOUT_AT(field_212,     0x212);
    KAROO_LAYOUT_AT(field_216,     0x216);
    KAROO_LAYOUT_AT(field_21a,     0x21a);
    KAROO_LAYOUT_AT(effectList_,   0x21c);
    KAROO_LAYOUT_AT(field_22c,     0x22c);
    KAROO_LAYOUT_AT(field_230,     0x230);
    KAROO_LAYOUT_AT(field_231,     0x231);
    KAROO_LAYOUT_AT(field_239,     0x239);
    KAROO_LAYOUT_AT(field_23d,     0x23d);
    KAROO_LAYOUT_SIZE(0x241);
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_UpdatePlayerTileEffects(Player *self);
