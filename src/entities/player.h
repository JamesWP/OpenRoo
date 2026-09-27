/* Player: the player entity, a MovableEntity embedded in Game rather than
 * separately allocated, so there is no factory size to tile against -- the
 * layout is asserted field by field instead.
 *
 * The tick, the actions and the lifecycle are in player.cpp; every field this
 * codebase touches goes through the accessors below.  Five of the Game fields
 * determinism.cpp hashes by raw offset are Player fields reached through the
 * Game, so the layout must stay packed and in place. */

#pragma once

#include "layout.h"
#include "movableentity.h"

class CStaticSoundbuffer;
struct LinkedList;
class VoicePool;
class Tile;

class __attribute__((packed)) Player : public MovableEntity {
public:
    static const int ORIGIN = 0;

    // Latches the clock, moves the player, drops a respawning player back in,
    // and consumes the tile underfoot.  See player.cpp.
    unsigned int updateTileEffects();

    // The six actions DirectInputSetup registers as player controls.
    void actMoveForward();
    void actMoveBack();
    void actTurnLeft();
    void actTurnRight();
    void actHarakiri();
    void actReleaseBomb();

    // Game-embedded lifecycle: called only from Game's own construction and
    // destruction.
    void construct();
    void clearPathfinder()  { pathfinder_ = NULL; }
    void destruct();

    // The tile the player stands on, from its signed cell coordinates.
    Tile *curTile() const;

    void  setClock(double *c)                  { clock_ = c; }
    void  setTickStep(TickStep *r)            { tickStep_ = r; }
    void  setTileBase(unsigned char *b)        { tileBase_ = b; }
    // The readers (facing, pos, cell) are MovableEntity's.
    void  setFacing(unsigned char f)           { facing_ = f; }
    // Stored in the order u, y, v, as every caller stores them.
    void  setPos(float u, float y, float v)    { posU_ = u; posY_ = y; posV_ = v; }
    void  setCell(unsigned char u, unsigned char v, unsigned char h)
    {
        cellU_ = (signed char)u;
        cellV_ = (signed char)v;
        heightCell_ = (signed char)h;
    }

    void  setIdleDuration(double d)                 { idleDuration_ = d; }
    void  setMovingBackwards(int n)                    { movingBackwards_ = n; }
    void  setConveyorDir(int n)                { conveyorDir_ = n; }
    // The move duration default: movableentity.cpp copies it into the moving
    // entity's step timer. 200 ms normally; 100 or 400 ms under the speed-up
    // or speed-down effects.
    void  setStepDuration(double d)                 { stepDuration_ = d; }
    void  setIdleStarted(int n)                    { idleStarted_ = n; }
    void  setLastActive(double d)                 { lastActive_ = d; }
    void  setDying(int n)                    { dying_ = n; }
    void  setAnim(unsigned char b)          { anim_ = b; }
    void  setOnLift(int n)                    { onLift_ = n; }
    void  setFieldD3(int n)                    { field_d3 = n; }
    // The switch the player is standing on; 0xff means none.
    unsigned char switchSlot() const           { return field_d7; }
    void  setSwitchSlot(unsigned char s)       { field_d7 = s; }
    int   completionNumerator() const          { return field_d8; }
    void  setCompletionNumerator(int n)        { field_d8 = n; }
    void  setLastContact(double d)                 { lastContact_ = d; }
    // The player's pending bomb-drop request.
    int   bombDropRequest() const              { return bombDropRequest_; }
    void  setBombDropRequest(int n)            { bombDropRequest_ = n; }
    unsigned char fieldE8() const              { return field_e8; }
    void  setFieldE8(unsigned char b)          { field_e8 = b; }
    unsigned char glides() const              { return glides_; }
    void  setGlides(unsigned char b)          { glides_ = b; }
    int   gliding() const                      { return gliding_; }
    void  setGliding(int n)                    { gliding_ = n; }
    // gamestate.cpp's level-complete flag, set once on exit.
    void  setHeld(int n)                    { held_ = n; }
    void  setTeleportPhase(unsigned char b)    { teleportPhase_ = b; }
    void  setLastMoveDir(unsigned char b)      { lastMoveDir_ = b; }
    unsigned char fallStartH() const             { return fallStartH_; }
    void  setField11a(int n)                   { field_11a = n; }
    void  setSlideSlot(unsigned char b)         { slideSlot_ = b; }
    // Nonzero while dead or respawning. Foe::checkPlayerContact sets it to 1
    // on contact with a foe.
    unsigned char *moveStateRef()              { return &moveState_; }
    int   falling() const                     { return falling_; }
    void  setFalling(int n)                   { falling_ = n; }
    void  setField126(double d)                { field_126 = d; }
    void  setField12e(int n)                   { field_12e = n; }
    signed char stepU() const                  { return stepU_; }
    signed char stepV() const                  { return stepV_; }
    signed char field141() const               { return field_141; }
    // The marker-4 cell levelsetup.cpp looks up as the level exit, written
    // through the pointer.
    unsigned char markerCellU() const             { return markerCellU_; }
    unsigned char markerCellV() const             { return markerCellV_; }
    unsigned char markerCellH() const             { return markerCellH_; }
    unsigned char *markerCellRef()               { return &markerCellU_; }
    void  setPendingMove(unsigned char m)      { pendingMove_ = m; }
    double animStart() const                    { return animStart_; }
    void  setMoveDir(int n)                    { moveDir_ = n; }
    void  setKind(unsigned char k)             { kind_ = k; }
    // The start cell (u, v, h), from the marker-3 lookup, which writes it
    // through the pointer.
    unsigned char *homeRef()                   { return &homeU_; }

    // The world's sound variant (0 Egypt, 1 Candy, 2 Space); indexes the
    // pickup sound bank.
    int   worldSoundVariant() const                     { return worldSoundVariant_; }
    void  setWorldSoundVariant(int n)                   { worldSoundVariant_ = n; }

    // The nine three-entry pickup sound banks.
    enum PickupBank {
        SND_15E, SND_16A, SND_176, SND_182, SND_18E,
        SND_19A, SND_1A6, SND_1B2, SND_1BE,
    };
    CStaticSoundbuffer *pickupSound(int bank, int i) const { return pickupSounds_[bank][i]; }
    void  setPickupSound(int bank, int i, CStaticSoundbuffer *p) { pickupSounds_[bank][i] = p; }

    double lastSecondsMark() const                    { return lastSecondsMark_; }
    void  setLastSecondsMark(double d)                { lastSecondsMark_ = d; }
    void  setEffectBActive(int n)                   { effectBActive_ = n; }
    // The freeze effect's flag; worldstate.cpp reports it as the freeze timer.
    int   effect8Active() const                     { return effect8Active_; }
    void  setEffect8Active(int n)                   { effect8Active_ = n; }
    void  setEffectDStart(double d)                { effectDStart_ = d; }
    int   effectDActive() const                     { return effectDActive_; }
    int   effectAActive() const                     { return effectAActive_; }
    // When each timed effect started, in clock ms; the HUD counts down from
    // them.
    double effectBStart() const                     { return effectBStart_; }
    double effect8Start() const                     { return effect8Start_; }
    double effectDStart() const                     { return effectDStart_; }
    double effectCStart() const                     { return effectCStart_; }
    double effectAStart() const                     { return effectAStart_; }
    // The active timed-effect codes, for the HUD.
    LinkedList *effectList()                        { return effects(); }
    void  setEffectDActive(int n)                   { effectDActive_ = n; }
    void  setEffectCActive(int n)                   { effectCActive_ = n; }
    void  setEffectAActive(int n)                   { effectAActive_ = n; }
    // Stored in the order u, h, v, as the caller stores them.
    void  setMarker(float a, float b, float c)
    {
        markerU_ = a;
        markerH_ = b;
        markerV_ = c;
    }
    // The pickup counter for this level.
    unsigned short itemsCollected() const            { return itemsCollected_; }
    void  setItemsCollected(unsigned short n)        { itemsCollected_ = n; }
    // The active timed-effect list, a game LinkedList.
    void  appendEffect(int code);
    void  clearEffects();
    // The running score total, persisted to the save.
    int   score() const                     { return score_; }
    void  setScore(int n)                   { score_ = n; }
    void  setLastRoll(signed char c)           { lastRoll_ = c; }
    void  setField231(double d)                { field_231 = d; }
    // Lives remaining.
    int   lives() const                     { return lives_; }
    void  setLives(int n)                   { lives_ = n; }
    // Crystals collected.
    int   gemsCollected() const                     { return gemsCollected_; }
    void  setGemsCollected(int n)                   { gemsCollected_ = n; }

private:
    Player() = delete;  // game-owned, embedded in Game
    KAROO_LAYOUT_REGISTER(Player);

    // An element type that may sit at any address: taking a packed array's
    // address as a plain pointer-to-pointer would claim four-byte alignment.
    typedef CStaticSoundbuffer *SoundRef __attribute__((aligned(1)));

    int   soundVariant() const;
    void  playAtCell(CStaticSoundbuffer *buf) const;
    void  pickupSound(const SoundRef *arr) const;
    // The raw bytes cast, not the array directly: a LinkedList pointer to a
    // packed member would trip -Waddress-of-packed-member.
    LinkedList *effects() { return (LinkedList *)effectList_; }
    void  endEffect(int code);

    int                 worldSoundVariant_;
    SoundRef            pickupSounds_[9][3];
    double              lastSecondsMark_;
    double              effectBStart_;
    int                 effectBActive_;
    double              effect8Start_;
    int                 effect8Active_;
    double              effectDStart_;
    int                 effectDActive_;
    double              effectCStart_;
    int                 effectCActive_;
    double              effectAStart_;
    int                 effectAActive_;
    float               markerU_;
    float               markerH_;
    float               markerV_;
    unsigned short      itemsCollected_;  // items picked up this level; the all-items bonus tests it
    // The game's LinkedList of active effect codes; only ever handed to the
    // game's own LinkedList functions.
    unsigned char       effectList_[16];
    int                 score_;
    signed char         lastRoll_;
    double              field_231;
    int                 lives_;
    int                 gemsCollected_;
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

/* The ActionCallback shims for the six actions (progctrl.h's __cdecl(key,
 * strength, context), context = the Player). */
extern "C" {
__declspec(dllexport) void __cdecl Player_ActMoveForward(int key, int strength, void *player);
__declspec(dllexport) void __cdecl Player_ActMoveBack(int key, int strength, void *player);
__declspec(dllexport) void __cdecl Player_ActTurnLeft(int key, int strength, void *player);
__declspec(dllexport) void __cdecl Player_ActTurnRight(int key, int strength, void *player);
__declspec(dllexport) void __cdecl Player_ActHarakiri(int key, int strength, void *player);
__declspec(dllexport) void __cdecl Player_ActReleaseBomb(int key, int strength, void *player);
}

extern "C" __declspec(dllexport) Player *__attribute__((thiscall))
Player_ScalarDestructor(Player *self, unsigned char flags);
