/* Player: the tick, the actions and the lifecycle.
 *
 * The tick latches the clock and moves the player, drops a respawning player
 * back in from above, and then, if the player stands on a tile, consumes the
 * tile's pickup and expires any timed effect that has run out.
 *
 * Each pickup block tests the tile's contents byte, applies its effect, zeroes
 * the byte and plays a sound.  The blocks run in a fixed order and each
 * re-reads the byte, so at most one fires.  The five timed effects push their
 * code onto the active list if not already running and stamp a start time; the
 * expiry section removes them.
 *
 * KAROO_SIM_FX=nopickup forces the tile gate to fail, so nothing is ever
 * consumed; it moves items_collected and elapsed_ms. */

#include <windows.h>
#include <string.h>
#include <math.h>

#include "static.h"
#include "tile.h"
#include "levelmap.h"
#include "linkedlist.h"
#include "crtrand.h"
#include "player.h"
#include "entitymath.h"
#include "foepath.h"
#include <stdlib.h>
#include "voicepool.h"
#include "log.h"

#include "movableentity.h"

/* Durations in ms; the rise rate is per ms. */
static const float  K_LAND_Y      = 1.0f;
static const float  K_RESPAWN_TOP = 150.0f;
static const float  K_RISE_RATE   = 0.001f;
static const float  K_CENTRE_TOL  = 0.3f;
static const float  K_CENTRE_W    = 3.0f;
static const double K_EFFECT8_MS  = 5000.0;
static const double K_EFFECT_MS   = 10000.0;

static int s_fx_nopickup = 0;
static int s_init = 0;

static void fx_init(void)
{
    char buf[64];
    DWORD n;

    if (s_init)
        return;
    s_init = 1;

    n = GetEnvironmentVariableA("KAROO_SIM_FX", buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf) && strcmp(buf, "nopickup") == 0) {
        s_fx_nopickup = 1;
        log_write("tileeffects: KAROO_SIM_FX=nopickup -- tile gate always fails\n");
    }
}

/* Copies a double as eight bytes, as the original does. */
static inline void copy8(void *dst, const void *src) { memcpy(dst, src, 8); }

static inline double load_double(const void *p)
{
    double v;
    memcpy(&v, p, 8);
    return v;
}

/* Re-derived at every use: movement can change the cell between blocks. */
Tile *Player::curTile() const
{
    return Tile::at(tileBase_, cellU_, cellV_);
}

int Player::soundVariant() const
{
    return (int)(long long)now_ % 3;
}

void Player::playAtCell(CStaticSoundbuffer *buf) const
{
    buf->set3DPosition((float)(int)cellU_,
                          (float)(int)heightCell_,
                          -(float)(int)cellV_,
                          1);
}

/* PRESERVED: the variant is computed twice, for the array and the trigger. */
void Player::pickupSound(const SoundRef *arr) const
{
    CStaticSoundbuffer *buf = arr[soundVariant()];

    if (buf != NULL) {
        playAtCell(buf);
        (arr[soundVariant()])->triggerPlayback(0);
    }
}

void Player::appendEffect(int code)
{
    effects()->append((void *)(unsigned int)code);
}

void Player::clearEffects()
{
    effects()->clear();
}

void Player::endEffect(int code)
{
    LinkedListNode *node = effects()->find((void *)(unsigned int)code, NULL);
    effects()->unlink(node);
}

unsigned int Player::updateTileEffects()
{
    fx_init();

    copy8(&tickStepCopy_, tickStep_);
    copy8(&now_, clock_);

    pendingMove_ = 0;

    if ((signed char)anim_ != 10)
        updateMovement();

    if (moveState_ != 0) {
        curTile()->setOccupant(0);

        effectBActive_ = 0;
        effect8Active_ = 0;
        effectDActive_ = 0;
        effectCActive_ = 0;
        effectAActive_ = 0;

        clearEffects();

        copy8(&lastActive_, &now_);
        idleStarted_ = 0;

        if (posY_ <= K_LAND_Y) {
            anim_ = 8;
        } else {
            //             // Evaluated before the state store, as the original orders it.
            int rising = (posY_ < K_RESPAWN_TOP);
            anim_ = 10;
            if (rising)
                posY_ = (float)(load_double(&tickStepCopy_) * (double)K_RISE_RATE
                                + (double)posY_);
        }
    }

    pickedUp_ = 0;

    {
        Tile *t = curTile();
        int tileH = (int)t->height();              // Read unsigned.
        int y     = (int)(signed char)(int)posY_;  // Truncated, then read signed.
        int onTile;

        if (y != tileH)
            goto expire;

        {
            //             // Held in double: rounding each step to float can cross the 0.3
            //             // threshold.
            double d;
            int centred;

            d = (double)(posU_ - (float)(int)cellU_);
            centred = (sqrt(d * d * (double)K_CENTRE_W) < (double)K_CENTRE_TOL);

            if (centred) {
                d = (double)(posY_ - (float)(int)heightCell_);
                centred = (sqrt(d * d * (double)K_CENTRE_W) < (double)K_CENTRE_TOL);
            }
            if (centred) {
                d = (double)(posV_ - (float)(int)cellV_);
                centred = (sqrt(d * d * (double)K_CENTRE_W) < (double)K_CENTRE_TOL);
            }

            onTile = centred || (moveDir_ == 0);
        }

        if (s_fx_nopickup)
            onTile = 0;

        if (!onTile)
            goto expire;

        //         // DETERMINISM: a random pickup rolls rand() here, inside the
        //         // simulation: codes 5 to 13, written back to the tile.
        if ((signed char)t->contents() == CONTENTS_RANDOM) {
            int r = (int)crt_rand() * 8;
            signed char rolled = (signed char)((char)(r / 0x7FFF) + 5);  // An 8-bit add.

            lastRoll_ = rolled;
            curTile()->setContents((unsigned char)rolled);
        }

        if ((signed char)curTile()->contents() == CONTENTS_TIME_BONUS) {
            LevelMap *map = LevelMap::fromTileBase(tileBase_);
            map->setTimeLimit(map->timeLimit() + 5);
            curTile()->setContents(0);
            itemsCollected_ += 1;
            lastSecondsMark_ = (double)(map->timeLimit() + 1);

            pickupSound(pickupSounds_[SND_176]);
            pickedUp_ = 6;
        }

        if ((signed char)curTile()->contents() == CONTENTS_CRYSTAL) {
            gemsCollected_ += 1;
            curTile()->setContents(0);
            itemsCollected_ += 1;

            if (pool_9f_ != NULL) {
                pool_9f_->broadcastCoordinates((float)(int)cellU_,
                    (float)(int)heightCell_,
                    -(float)(int)cellV_,
                    1);
                pool_9f_->cycle(0);
            }
            pickedUp_ = 1;
        }

        if ((signed char)curTile()->contents() == CONTENTS_EXTRA_LIFE) {
            lives_ += 1;
            curTile()->setContents(0);
            itemsCollected_ += 1;

            pickupSound(pickupSounds_[SND_16A]);
            pickedUp_ = 7;
        }

        if ((signed char)curTile()->contents() == CONTENTS_PARAGLIDER && falling_ == 0) {
            glides_ += 1;
            curTile()->setContents(0);
            itemsCollected_ += 1;

            {
                CStaticSoundbuffer *buf = pickupSounds_[SND_19A][worldSoundVariant_];
                if (buf != NULL) {
                    playAtCell(buf);
                    pickupSounds_[SND_19A][worldSoundVariant_]->triggerPlayback(0);
                }
            }
            pickedUp_ = 5;
        }

        if ((signed char)curTile()->contents() == CONTENTS_GRANT_09) {
            field_e8 += 3;
            curTile()->setContents(0);
            itemsCollected_ += 1;

            pickupSound(pickupSounds_[SND_1A6]);
            pickedUp_ = 9;
        }

        if ((signed char)curTile()->contents() == CONTENTS_EFFECT_8) {
            if (effect8Active_ == 0)
                appendEffect(8);
            copy8(&effect8Start_, &now_);
            effect8Active_ = 1;
            curTile()->setContents(0);
            itemsCollected_ += 1;

            pickupSound(pickupSounds_[SND_15E]);
            pickedUp_ = 8;
        }

        if ((signed char)curTile()->contents() == CONTENTS_REVERSED) {
            if (effectBActive_ == 0)
                appendEffect(0xb);
            copy8(&effectBStart_, &now_);
            effectBActive_ = 1;
            curTile()->setContents(0);
            itemsCollected_ += 1;

            pickupSound(pickupSounds_[SND_1B2]);
            pickedUp_ = 0xb;
        }

        if ((signed char)curTile()->contents() == CONTENTS_SPEED_UP) {
            if (effectAActive_ == 0)
                appendEffect(0xa);
            copy8(&effectAStart_, &now_);
            effectAActive_ = 1;
            stepDuration_ = 100.0;
            curTile()->setContents(0);
            itemsCollected_ += 1;

            pickupSound(pickupSounds_[SND_182]);
            pickedUp_ = 0xa;
        }

        if ((signed char)curTile()->contents() == CONTENTS_SPEED_DOWN) {
            if (effectCActive_ == 0)
                appendEffect(0xc);
            copy8(&effectCStart_, &now_);
            effectCActive_ = 1;
            stepDuration_ = 400.0;
            curTile()->setContents(0);
            itemsCollected_ += 1;

            pickupSound(pickupSounds_[SND_18E]);
            pickedUp_ = 0xc;
        }

        if ((signed char)curTile()->contents() == CONTENTS_TRANSFORM) {
            if (effectDActive_ == 0)
                appendEffect(0xd);

            effectDActive_ = 1;
            kind_     = 3;
            curTile()->setOccupant(3);

            //             // PRESERVED: stamped after the flag, unlike the other four.
            copy8(&effectDStart_, &now_);

            curTile()->setContents(0);
            itemsCollected_ += 1;

            pickupSound(pickupSounds_[SND_1BE]);
            pickedUp_ = 0xd;
        }
    }

expire:

    //     // PRESERVED: effect 8 ends at >= 5000 ms; the other four end only at
    //     // > 10000 ms.
    if (effect8Active_ != 0) {
        if (now_ - effect8Start_ >= K_EFFECT8_MS) {
            effect8Active_ = 0;
            endEffect(8);
        }
    }

    if (effectBActive_ != 0) {
        if (now_ - effectBStart_ > K_EFFECT_MS) {
            effectBActive_ = 0;
            endEffect(0xb);
        }
    }

    if (effectAActive_ != 0) {
        if (now_ - effectAStart_ > K_EFFECT_MS) {
            stepDuration_  = 200.0;
            effectAActive_ = 0;
            endEffect(0xa);
        }
    }

    if (effectCActive_ != 0) {
        if (now_ - effectCStart_ > K_EFFECT_MS) {
            stepDuration_  = 200.0;
            effectCActive_ = 0;
            endEffect(0xc);
        }
    }

    if (effectDActive_ != 0) {
        if (now_ - effectDStart_ > K_EFFECT_MS) {
            effectDActive_ = 0;
            kind_     = 4;
            curTile()->setOccupant(4);
            endEffect(0xd);
        }
    }

    //     // The original returns garbage here; its one caller discards it.
    return 0;
}

/* turnKind_ gives pendingMove_ relative to the facing: 1 forward, 3 back, 2
 * and 4 a right and left turn (whose pendingMove_ is the new facing + 10).
 * While effect 0xb is active, forward and back, left and right swap.  A turn
 * pressed while moving is queued unless one is already queued or under way. */
void Player::actMoveForward()
{
    if (moveDir_ != 0)
        return;
    if (effectBActive_ == 0) {
        pendingMove_ = facing_;
        turnKind_ = 1;
    } else {
        pendingMove_ = Sim_GetTurnedDirection(facing_, 2);
        turnKind_ = 3;
    }
    updateMovement();
}

void Player::actMoveBack()
{
    if (moveDir_ != 0)
        return;
    if (effectBActive_ == 0) {
        pendingMove_ = Sim_GetTurnedDirection(facing_, 2);
        turnKind_ = 3;
    } else {
        pendingMove_ = facing_;
        turnKind_ = 1;
    }
    updateMovement();
}

/* `delta` 3 turns left, 1 right. */
static inline unsigned char turned_plus_10(unsigned char facing, unsigned char delta)
{
    return (unsigned char)(Sim_GetTurnedDirection(facing, delta) + 10);
}

void Player::actTurnLeft()
{
    if (moveDir_ != 0) {
        if (turnKind_ == 2 || turnKind_ == 4 || queuedTurn_ != 0)
            return;
        queuedMove_ = turned_plus_10(facing_, 3);
        queuedTurn_ = 4;
        return;
    }
    queuedMove_ = 0;
    queuedTurn_ = 0;
    if (effectBActive_ == 0) {
        pendingMove_ = turned_plus_10(facing_, 3);
        turnKind_ = 4;
    } else {
        pendingMove_ = turned_plus_10(facing_, 1);
        turnKind_ = 2;
    }
    updateMovement();
}

void Player::actTurnRight()
{
    if (moveDir_ != 0) {
        if (turnKind_ == 4 || turnKind_ == 2 || queuedTurn_ != 0)
            return;
        queuedMove_ = turned_plus_10(facing_, 1);
        queuedTurn_ = 2;
        return;
    }
    queuedMove_ = 0;
    queuedTurn_ = 0;
    if (effectBActive_ == 0) {
        pendingMove_ = turned_plus_10(facing_, 1);
        turnKind_ = 2;
    } else {
        pendingMove_ = turned_plus_10(facing_, 3);
        turnKind_ = 4;
    }
    updateMovement();
}

/* Only while standing still. */
void Player::actHarakiri()
{
    if (moveDir_ == 0)
        moveState_ = 6;
}

/* At most one bomb per 2000 ms, only while alive and with bombs left; the tick
 * drops it. */
void Player::actReleaseBomb()
{
    if (!(now_ - lastContact_ >= 2000.0))  // A NaN also skips.
        return;
    if (moveState_ != 0 || field_e8 == 0)
        return;
    lastContact_ = now_;
    bombDropRequest_ = 1;
    field_e8--;
}

extern "C" {
__declspec(dllexport) void __cdecl Player_ActMoveForward(int, int, void *p) { ((Player *)p)->actMoveForward(); }
__declspec(dllexport) void __cdecl Player_ActMoveBack(int, int, void *p)    { ((Player *)p)->actMoveBack(); }
__declspec(dllexport) void __cdecl Player_ActTurnLeft(int, int, void *p)    { ((Player *)p)->actTurnLeft(); }
__declspec(dllexport) void __cdecl Player_ActTurnRight(int, int, void *p)   { ((Player *)p)->actTurnRight(); }
__declspec(dllexport) void __cdecl Player_ActHarakiri(int, int, void *p)    { ((Player *)p)->actHarakiri(); }
__declspec(dllexport) void __cdecl Player_ActReleaseBomb(int, int, void *p) { ((Player *)p)->actReleaseBomb(); }
}

/* PRESERVED: the ctor and dtor keep the original's transient vtable stores,
 * and zeroSoundSlots() runs twice. */
static void *const g_PlayerVtable[1] = { (void *)&Player_ScalarDestructor };

void Player::construct()
{
    populateBaseForGame();
    ((LinkedList *)effectList_)->init();
    vtable_  = g_PlayerVtable;
    pool_9f_ = NULL;
    memset(pickupSounds_, 0, sizeof(pickupSounds_));
    zeroSoundSlots();
    field_126      = 0.0;
    stepDuration_  = 200.0;
    field_156      = 1;
    gemsCollected_ = 0;
    field_124      = 1;
    facing_        = 1;
    moveDir_       = 0;
    teleportPhase_ = 0;
    climbing_      = 0;
    falling_       = 0;
    moveState_     = 0;
    zeroSoundSlots();
    stepGrace_     = 20.0;
    pathfinder_    = NULL;
}

/* The path-finder is allocated on the game heap, so it is freed there. */
void Player::destruct()
{
    vtable_ = g_PlayerVtable;
    if (pathfinder_ != NULL) {
        pathfinder_->dispose();
        free(pathfinder_);
    }
    ((LinkedList *)effectList_)->destruct();
    destroyBaseForGame();
}

extern "C" __declspec(dllexport) Player *__attribute__((thiscall))
Player_ScalarDestructor(Player *self, unsigned char flags)
{
    self->destruct();
    if (flags & 1)
        free(self);
    return self;
}
