/* Player -- the player entity: a MovableEntity (movableentity.h, 0x15a
 * bytes) plus its own fields.  Embedded in Game at +0x1751c9 (reached as
 * Game::player()), not allocated, so there is no operator new size to tile
 * against; the layout is asserted field by field up to +0x241.  Nothing in
 * the ctor or the tick touches past +0x23d, so +0x241 is taken as the end:
 * Game+0x175412, which keypress.cpp writes, is +0x249 and is left as Game's.
 *
 * Ours (player.cpp): the tick, Game::UpdatePlayerTileEffects
 * 0x0041fcb0.  The ctor (PopulatePlayerEntityDefaults 0x0041f900) and dtor
 * (0x0041fa10) are still the game's; the ctor confirms the nine three-entry
 * sound arrays (+0x15e..+0x1ca, zeroed in one loop) and the LinkedList at
 * +0x21c (LinkedList::Init).
 *
 * Every Player field our code touches goes through the accessors below,
 * except determinism.cpp's hash table, which hashes opaque byte ranges by
 * design (see there).
 * COHESION_PLAN.md Band 4b pass 2 named the timed pickup effects, the
 * score, lives and marker cell; what is left is field_<off> / fieldXxx(),
 * one name per offset whose meaning is not settled.  The original
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
     * underfoot, expire timed effects.  Returns 0; see player.cpp. */
    unsigned int updateTileEffects();

    /* The six player actions DirectInputSetup registers (player.cpp):
     * 0x41fa90 forward, 0x41faf0 back, 0x41fb50 left, 0x41fbf0 right,
     * 0x41fc90 harakiri, 0x4208f0 release bomb. */
    void actMoveForward();
    void actMoveBack();
    void actTurnLeft();
    void actTurnRight();
    void actHarakiri();
    void actReleaseBomb();

    /* Game-embedded lifecycle, called only by Game_Construct / Game_Destruct
     * (gamelife.cpp); the vtable is ours, one slot (the game's 0x45d428 is a
     * tripwire). */
    void construct();       /* PopulatePlayerEntityDefaults 0x41f900 */
    void clearPathfinder()  { pathfinder_ = NULL; }
    void destruct();        /* RestorePlayerVtableBeforeEntityDtor 0x41fa10 */

    /* The tile the player stands on, from its SIGNED cell bytes -- the
     * arithmetic every caller used by hand. */
    Tile *curTile() const;

    /* ── the level-object base ──────────────────────────────────────── */
    void  setClock(double *c)                  { clock_ = c; }
    void  setTickStep(TickStep *r)            { tickStep_ = r; }
    void  setTileBase(unsigned char *b)        { tileBase_ = b; }
    /* The readers (facing, pos, cell) are MovableEntity's. */
    void  setFacing(unsigned char f)           { facing_ = f; }
    /* Stored in the order u, y, v -- as each caller stored them. */
    void  setPos(float u, float y, float v)    { posU_ = u; posY_ = y; posV_ = v; }
    void  setCell(unsigned char u, unsigned char v, unsigned char h)
    {
        cellU_ = (signed char)u;
        cellV_ = (signed char)v;
        heightCell_ = (signed char)h;
    }

    /* ── MovableEntity fields, meanings unknown unless noted ────────── */
    /* The doubles are written by some callers as two dword stores; one
     * double store is the same bits (COHESION_PLAN.md, template 3). */
    void  setIdleDuration(double d)                 { idleDuration_ = d; }
    void  setMovingBackwards(int n)                    { movingBackwards_ = n; }
    void  setConveyorDir(int n)                { conveyorDir_ = n; }
    /* +0x66: the move duration default (movableentity.cpp copies it into
     * +0x132): 200.0 normally, 100.0 / 400.0 under pickups 0xa / 0xc. */
    void  setStepDuration(double d)                 { stepDuration_ = d; }
    void  setIdleStarted(int n)                    { idleStarted_ = n; }
    void  setLastActive(double d)                 { lastActive_ = d; }
    void  setDying(int n)                    { dying_ = n; }
    void  setAnim(unsigned char b)          { anim_ = b; }
    void  setOnLift(int n)                    { onLift_ = n; }
    void  setFieldD3(int n)                    { field_d3 = n; }
    /* +0xd7: the switch the player stands on, 0xff = none. */
    unsigned char switchSlot() const           { return field_d7; }
    void  setSwitchSlot(unsigned char s)       { field_d7 = s; }
    int   completionNumerator() const          { return field_d8; }
    void  setCompletionNumerator(int n)        { field_d8 = n; }
    void  setLastContact(double d)                 { lastContact_ = d; }
    /* +0xe4: the player's bomb-drop request. */
    int   bombDropRequest() const              { return bombDropRequest_; }
    void  setBombDropRequest(int n)            { bombDropRequest_ = n; }
    unsigned char fieldE8() const              { return field_e8; }
    void  setFieldE8(unsigned char b)          { field_e8 = b; }
    unsigned char glides() const              { return glides_; }
    void  setGlides(unsigned char b)          { glides_ = b; }
    int   gliding() const                      { return gliding_; }
    void  setGliding(int n)                    { gliding_ = n; }
    /* +0xef: gamestate.cpp's level-complete flag (0 -> 1 on exit). */
    void  setHeld(int n)                    { held_ = n; }
    void  setTeleportPhase(unsigned char b)    { teleportPhase_ = b; }
    void  setLastMoveDir(unsigned char b)      { lastMoveDir_ = b; }
    unsigned char fallStartH() const             { return fallStartH_; }
    void  setField11a(int n)                   { field_11a = n; }
    void  setSlideSlot(unsigned char b)         { slideSlot_ = b; }
    /* +0x11f (moveState, in MovableEntity): nonzero while dead / timed out
     * (3 = time-out).  Foe::checkPlayerContact writes through it. */
    unsigned char *moveStateRef()              { return &moveState_; }
    int   falling() const                     { return falling_; }
    void  setFalling(int n)                   { falling_ = n; }
    void  setField126(double d)                { field_126 = d; }
    void  setField12e(int n)                   { field_12e = n; }
    signed char stepU() const                  { return stepU_; }
    signed char stepV() const                  { return stepV_; }
    signed char field141() const               { return field_141; }
    /* +0x142..+0x144: the marker-4 cell SetupLevelObjects looks up (u, v,
     * h) -- worldstate.cpp's level exit.  Written through the pointer. */
    unsigned char markerCellU() const             { return markerCellU_; }
    unsigned char markerCellV() const             { return markerCellV_; }
    unsigned char markerCellH() const             { return markerCellH_; }
    unsigned char *markerCellRef()               { return &markerCellU_; }
    void  setPendingMove(unsigned char m)      { pendingMove_ = m; }
    double animStart() const                    { return animStart_; }
    void  setMoveDir(int n)                    { moveDir_ = n; }
    void  setKind(unsigned char k)             { kind_ = k; }
    /* +0x153..+0x155: the start cell (u, v, h), from the marker-3 lookup,
     * which writes it through the pointer (read with MovableEntity::homeU). */
    unsigned char *homeRef()                   { return &homeU_; }

    /* The base's sound handles (pool9f, soundA3..soundCb, poolCf) are
     * MovableEntity's: the foe's sounds are attached through them too. */

    /* ── the Player's own fields ────────────────────────────────────── */
    /* +0x15a: the world's sound variant (0 Egypt, 1 Candy, 2 Space --
     * levelsounds.cpp); indexes bank SND_19A. */
    int   worldSoundVariant() const                     { return worldSoundVariant_; }
    void  setWorldSoundVariant(int n)                   { worldSoundVariant_ = n; }

    /* The nine three-entry pickup sound banks, +0x15e + 0x0c * bank. */
    enum PickupBank {
        SND_15E, SND_16A, SND_176, SND_182, SND_18E,
        SND_19A, SND_1A6, SND_1B2, SND_1BE,
    };
    CStaticSoundbuffer *pickupSound(int bank, int i) const { return pickupSounds_[bank][i]; }
    void  setPickupSound(int bank, int i, CStaticSoundbuffer *p) { pickupSounds_[bank][i] = p; }

    double lastSecondsMark() const                    { return lastSecondsMark_; }
    void  setLastSecondsMark(double d)                { lastSecondsMark_ = d; }
    void  setEffectBActive(int n)                   { effectBActive_ = n; }
    /* +0x1e6: effect 8's flag; worldstate.cpp's freeze timer. */
    int   effect8Active() const                     { return effect8Active_; }
    void  setEffect8Active(int n)                   { effect8Active_ = n; }
    void  setEffectDStart(double d)                { effectDStart_ = d; }
    int   effectDActive() const                     { return effectDActive_; }
    void  setEffectDActive(int n)                   { effectDActive_ = n; }
    void  setEffectCActive(int n)                   { effectCActive_ = n; }
    void  setEffectAActive(int n)                   { effectAActive_ = n; }
    /* Stored in the order +0x20e, +0x212, +0x216, as the caller does. */
    void  setMarker(float a, float b, float c)
    {
        markerU_ = a;
        markerH_ = b;
        markerV_ = c;
    }
    /* +0x21a: the pickup counter. */
    unsigned short itemsCollected() const            { return itemsCollected_; }
    void  setItemsCollected(unsigned short n)        { itemsCollected_ = n; }
    /* The active timed-effect list (a game LinkedList). */
    void  appendEffect(int code);
    void  clearEffects();
    /* +0x22c: the running score total, persisted to .sav. */
    int   score() const                     { return score_; }
    void  setScore(int n)                   { score_ = n; }
    void  setLastRoll(signed char c)           { lastRoll_ = c; }
    void  setField231(double d)                { field_231 = d; }
    /* +0x239: lives (a dword; byte readers take the low byte). */
    int   lives() const                     { return lives_; }
    void  setLives(int n)                   { lives_ = n; }
    /* +0x23d: crystals collected. */
    int   gemsCollected() const                     { return gemsCollected_; }
    void  setGemsCollected(int n)                   { gemsCollected_ = n; }

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

    int                 worldSoundVariant_; /* +0x15a  which world's sounds */
    SoundRef            pickupSounds_[9][3]; /* +0x15e  PickupBank          */
    /* +0x1ca: the seconds-remaining the countdown beep last fired at.
     * Held at 10.0 while more than 11 s remain. */
    double              lastSecondsMark_;
    double              effectBStart_;  /* +0x1d2  } effect 0xb: start,  */
    int                 effectBActive_; /* +0x1da  }   then running     */
    double              effect8Start_;  /* +0x1de  } effect 8            */
    int                 effect8Active_; /* +0x1e6  }                     */
    double              effectDStart_;  /* +0x1ea  } effect 0xd          */
    int                 effectDActive_; /* +0x1f2  }                     */
    double              effectCStart_;  /* +0x1f6  } effect 0xc          */
    int                 effectCActive_; /* +0x1fe  }                     */
    double              effectAStart_;  /* +0x202  } effect 0xa          */
    int                 effectAActive_; /* +0x20a  }                     */
    float               markerU_;     /* +0x20e  } the marker-4 cell as */
    float               markerH_;     /* +0x212  } floats (u, h, v)     */
    float               markerV_;     /* +0x216  }                      */
    unsigned short      itemsCollected_;  /* +0x21a  items picked up this level;
                                           the all-items bonus tests it */
    /* The game's LinkedList (linkedlist.h, 16 bytes) of active effect
     * codes; only ever handed to the game's LinkedList methods. */
    unsigned char       effectList_[16];  /* +0x21c                         */
    int                 score_;       /* +0x22c  the running score      */
    signed char         lastRoll_;    /* +0x230  last random roll       */
    double              field_231;        /* +0x231                         */
    int                 lives_;       /* +0x239  lives remaining        */
    int                 gemsCollected_;        /* +0x23d  crystals               */
};

KAROO_LAYOUT_CHECKS(Player)
{
    KAROO_LAYOUT_AT(worldSoundVariant_,         0x15a);
    KAROO_LAYOUT_AT(pickupSounds_,     0x15e);
    KAROO_LAYOUT_AT(lastSecondsMark_,         0x1ca);
    KAROO_LAYOUT_AT(effectBStart_,         0x1d2);
    KAROO_LAYOUT_AT(effectBActive_,         0x1da);
    KAROO_LAYOUT_AT(effect8Start_,         0x1de);
    KAROO_LAYOUT_AT(effect8Active_,         0x1e6);
    KAROO_LAYOUT_AT(effectDStart_,         0x1ea);
    KAROO_LAYOUT_AT(effectDActive_,         0x1f2);
    KAROO_LAYOUT_AT(effectCStart_,         0x1f6);
    KAROO_LAYOUT_AT(effectCActive_,         0x1fe);
    KAROO_LAYOUT_AT(effectAStart_,         0x202);
    KAROO_LAYOUT_AT(effectAActive_,         0x20a);
    KAROO_LAYOUT_AT(markerU_,         0x20e);
    KAROO_LAYOUT_AT(markerH_,         0x212);
    KAROO_LAYOUT_AT(markerV_,         0x216);
    KAROO_LAYOUT_AT(itemsCollected_,   0x21a);
    KAROO_LAYOUT_AT(effectList_,       0x21c);
    KAROO_LAYOUT_AT(score_,         0x22c);
    KAROO_LAYOUT_AT(lastRoll_,         0x230);
    KAROO_LAYOUT_AT(field_231,         0x231);
    KAROO_LAYOUT_AT(lives_,         0x239);
    KAROO_LAYOUT_AT(gemsCollected_,    0x23d);
    KAROO_LAYOUT_SIZE(0x241);
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_UpdatePlayerTileEffects(Player *self);

/* The ActionCallback shims for the six (progctrl.h's __cdecl(key, strength,
 * context); context = the Player). */
extern "C" {
__declspec(dllexport) void __cdecl Player_ActMoveForward(int key, int strength, void *player);
__declspec(dllexport) void __cdecl Player_ActMoveBack(int key, int strength, void *player);
__declspec(dllexport) void __cdecl Player_ActTurnLeft(int key, int strength, void *player);
__declspec(dllexport) void __cdecl Player_ActTurnRight(int key, int strength, void *player);
__declspec(dllexport) void __cdecl Player_ActHarakiri(int key, int strength, void *player);
__declspec(dllexport) void __cdecl Player_ActReleaseBomb(int key, int strength, void *player);
}

/* 0x41f9f0, slot 0 of our Player table. */
extern "C" __declspec(dllexport) Player *__attribute__((thiscall))
Player_ScalarDestructor(Player *self, unsigned char flags);
