/* GAMETICK_PLAN.md Band A — Game::UpdatePlayerTileEffects 0x0041fcb0.
 *
 * ─── What this is ────────────────────────────────────────────────────────
 *
 * The PLAYER tick.  GameTick calls it once per frame from 0x0041520a, and it
 * is the only caller (tools/xref.py over Karoo.exe.orig: one
 * UNCONDITIONAL_CALL, no DATA ref, no `68 imm32`).  Three jobs, in order:
 *
 *   1. Latch the clock, then drive UpdateEntityMovement for the player.
 *   2. If a respawn is pending, reset the effect state and drop the player
 *      back in from above.
 *   3. If the player is standing on a tile, CONSUME whatever the tile holds
 *      -- the pickup dispatch -- and then EXPIRE any timed effect that has
 *      run out.
 *
 * `this` is the Player (player.h), a MovableEntity embedded in Game at
 * +0x1751c9.  Tiles are reached through Tile::at, as in movableentity.cpp; the
 * tile's contents byte is +0x19f and its occupant byte +0x1a5.
 *
 * ─── The pickup table ────────────────────────────────────────────────────
 *
 * Each block tests the contents byte, applies its effect, ZEROES the contents
 * byte (so it cannot be taken twice), bumps the pickup counter at +0x21a, and
 * plays a sound.  The blocks run in this order, and each re-reads the
 * contents byte, so a block can only fire if no earlier block claimed it:
 *
 *   6   time bonus     tile(0,0)+0x004 += 5, and +0x1ca = that + 1
 *   1   crystal        +0x23d += 1        (VoicePool, not a static buffer)
 *   7   +0x239 += 1
 *   5   +0xe9  += 1    gated on +0x120 == 0
 *   9   +0xe8  += 3
 *   8   timed effect at +0x1e6, start time +0x1de
 *   0xb timed effect at +0x1da -- while set, PlayerMoveForward reverses
 *   0xa timed effect at +0x20a, speed double +0x66 = 100.0
 *   0xc timed effect at +0x1fe, speed double +0x66 = 400.0
 *   0xd timed effect at +0x1f2, form +0x152 = 3, occupant byte = 3
 *
 * The five timed effects each push their code onto the LinkedList at +0x21c
 * (only when not already active), record a start time, and are removed again
 * by the expiry section at the bottom.
 *
 * A contents byte of -1 (0xff) is a RANDOM pickup, rolled here:
 *
 *     tile = (char)((rand() * 8) / 0x7FFF) + 5          // codes 5..13
 *
 * rand() is the CRT LCG at 0x0045167c (seed*0x343FD + 0x269EC3, >>16 &
 * 0x7FFF), named CrtRandLcg in Ghidra this session.  The `+ 5` is an 8-BIT
 * add (`add $0x5,%dl`), and the roll is stored both into +0x230 and back into
 * the tile, so the tile keeps the rolled value.  This is a rand() caller
 * inside the simulation -- exactly the thing GAMETICK_PLAN.md says to suspect
 * first if a replay ever diverges.
 *
 * ─── Two things the decompiler gets wrong, read from the LISTING ─────────
 *
 * `decompile_function` renders all seventeen __ftol call sites with EMPTY
 * parentheses -- the argument is on the x87 stack, so it does not appear.
 * Same trap as SetFoeChaseTarget (foechase.cpp).  Read from the disassembly:
 *
 *   0x0041fdcc   flds 0x29(%esi)   -> (int)(float)this->y, then (signed char)
 *   the other 16  fldl 0x04(%esi)  -> (int)(double)this->now, then % 3
 *
 * So every static-sound block picks its variant with `(int)now % 3` over a
 * three-entry array, and RECOMPUTES that expression a second time for the
 * TriggerPlayback call rather than caching it.  Both computations are kept
 * here even though `now` cannot change between them.
 *
 * The height gate compares `(signed char)(int)y` against the tile's height
 * byte +0x19c read UNSIGNED (`xor %ebx,%ebx` then `mov %bl` -- so a
 * zero-extended byte, compared as int).  Preserved exactly.
 *
 * ─── A real asymmetry, preserved ─────────────────────────────────────────
 *
 * The five expiry tests are NOT the same comparison.  In the listing:
 *
 *   +0x1e6 (effect 8)   fcompl 5000.0   test $0x01,%ah   -> expires at >=
 *   the other four      fcompl 10000.0  test $0x41,%ah   -> expires at >
 *
 * `test $0x41` masks C0|C3, so it skips on "less OR equal" and the effect
 * only ends when the difference is STRICTLY greater than 10000 ms.  Effect 8
 * tests C0 alone and ends at exactly 5000 ms.  That is a one-frame difference
 * at the boundary, and it is reproduced rather than regularised.
 *
 * ─── Why LinkedList is called, not replaced ──────────────────────────────
 *
 * This function's callee list, read this session:
 *
 *   UpdateEntityMovement           0x438770   ours, movableentity.cpp
 *   Set3DPosition                  0x4429c0   ours, static.cpp
 *   TriggerPlayback                0x442900   ours, static.cpp
 *   BroadcastPoolVoiceCoordinates  0x442df0   ours, voicepool.cpp
 *   VoicePoolCycle                 0x442d90   ours, voicepool.cpp
 *   CrtRandLcg                     0x45167c   CRT rand(), reimplemented below
 *   LinkedList::Append             0x4254a0   the game's, called through
 *   LinkedList::Clear              0x4254f0   the game's, called through
 *   LinkedList::UnlinkAndFreeListNode 0x425530  the game's, called through
 *   LinkedList::FindListNodeByValue   0x425580  the game's, called through
 *
 * The four LinkedList methods are NAMED CALLBACKS into the game binary
 * (linkedlist.h, shared with every other caller in karoo-hooks), per
 * the no-callback rule's "name every unavoidable callback".  They are not
 * replaced, and that is a deliberate scope decision rather than laziness:
 *
 *   - LinkedList is a generic container with 43 call sites spread across
 *     movie playback, level parsing, sound and D3D mode enumeration -- far
 *     outside GameTick's closure.  Replacing it would drag all of that into
 *     a simulation cycle.
 *   - CLAUDE.md's patch.py section says outright not to stub shared helpers
 *     that remain live for other callers, and direct3d.cpp already documents
 *     this exact policy for LinkedList::Clear and FactAlloc::Free2.
 *   - One reference is an `E9` TAIL-JUMP at 0x00425496, not an `E8`, so
 *     CALL_PATCHES would not even cover it.
 *
 * They were still named and typed in Ghidra this session (UnlinkAndFreeListNode
 * and FindListNodeByValue were FUN_ stubs) so the next decompile of any caller
 * reads properly.
 *
 * ─── Interception ────────────────────────────────────────────────────────
 *
 * __thiscall, `this` in ECX, no stack arguments.  ONE E8 call site at
 * 0x0041520a in GameTick, so one CALL_PATCHES entry and one SAFETY_STUB.
 *
 * ─── The return value ────────────────────────────────────────────────────
 *
 * The original returns `uVar8 & 0xffffff00` where uVar8 is whatever x87
 * status word or field load happened to land in EAX last -- genuine garbage
 * in the low-order sense, and it differs between paths.  The sole caller
 * discards EAX immediately.  This returns 0, which is a deliberate deviation
 * of the same shape as UpdateEntityMovement's, and safe for the same reason.
 */

#include <windows.h>
#include <string.h>
#include <math.h>

#include "static.h"
#include "tile.h"
#include "levelmap.h"
#include "linkedlist.h"
#include "crtrand.h"
#include "player.h"
#include "voicepool.h"
#include "log.h"

/* ─── Callees in this same DLL ────────────────────────────────────────── */
#include "movableentity.h"
extern "C" __declspec(dllexport) int __attribute__((thiscall))
CStatic_TriggerPlayback(CStaticSoundbuffer *self, DWORD dwLoopFlags);
extern "C" __declspec(dllexport) void __attribute__((thiscall))
CStatic_Set3DPosition(CStaticSoundbuffer *self,
                      float x, float y, float z, DWORD dwApply);

/* ─── Constants, read out of .rdata this session ──────────────────────────
 *
 *   0x0045d298  float  1.0      landing height
 *   0x0045d440  float  150.0    respawn ceiling
 *   0x0045d308  float  0.001    respawn rise rate, per ms
 *   0x0045d438  float  0.3      cell-centre tolerance
 *   0x0045d43c  float  3.0      tolerance weight (pre-sqrt)
 *   0x0045d2d8  double 5000.0   effect 8 duration
 *   0x0045d430  double 10000.0  duration of the other four
 */
static const float  K_LAND_Y      = 1.0f;      /* 0x0045d298 */
static const float  K_RESPAWN_TOP = 150.0f;    /* 0x0045d440 */
static const float  K_RISE_RATE   = 0.001f;    /* 0x0045d308 */
static const float  K_CENTRE_TOL  = 0.3f;      /* 0x0045d438 */
static const float  K_CENTRE_W    = 3.0f;      /* 0x0045d43c */
static const double K_EFFECT8_MS  = 5000.0;    /* 0x0045d2d8 */
static const double K_EFFECT_MS   = 10000.0;   /* 0x0045d430 */

/* ─── KAROO_SIM_FX, read by VALUE ─────────────────────────────────────────
 *
 * By value, never by presence: GetEnvironmentVariableA returns 0 for empty
 * and unset alike, and a bare presence test is what silently turned the TGA
 * acceptance test into a no-op (RENDER_PLAN.md, 2026-09-02).
 *
 * `nopickup` forces the standing-on-tile gate to fail, so no tile is ever
 * consumed: no crystals, no time bonuses, no timed effects.  That is a
 * MEASURED change (items_collected, elapsed_ms), not a colour. */
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

/* Copy eight bytes, the way the original copies a double as two dwords. */
static inline void copy8(void *dst, const void *src) { memcpy(dst, src, 8); }

/* The 8-byte record copy at +0x15, read as a double by the respawn rise. */
static inline double load_double(const void *p)
{
    double v;
    memcpy(&v, p, 8);
    return v;
}

/* The tile the player currently occupies, re-derived every time the original
 * re-derives it -- the cell indices can be written by UpdateEntityMovement
 * between blocks, so caching would not be equivalent. */
Tile *Player::curTile() const
{
    return Tile::at(tileBase_, cellU_, cellV_);
}

/* `(int)now % 3`, the static-sound variant selector.  Signed idiv in the
 * original; C's % matches its truncating semantics. */
int Player::soundVariant() const
{
    return (int)(long long)now_ % 3;
}

/* The Set3DPosition argument triple, identical at all eight call sites:
 * (u, h, -v) with the v axis negated. */
void Player::playAtCell(CStaticSoundbuffer *buf) const
{
    CStatic_Set3DPosition(buf,
                          (float)(int)cellU_,
                          (float)(int)heightCell_,
                          -(float)(int)cellV_,
                          1);
}

/* One static-sound pickup chime: pick the variant, position it, trigger it.
 * The variant index is computed TWICE, as the original does. */
void Player::pickupSound(const SoundRef *arr) const
{
    CStaticSoundbuffer *buf = arr[soundVariant()];

    if (buf != NULL) {
        playAtCell(buf);
        CStatic_TriggerPlayback(arr[soundVariant()], 0);
    }
}

void Player::appendEffect(int code)
{
    LinkedList_Append(effects(), (void *)(unsigned int)code);
}

void Player::clearEffects()
{
    LinkedList_Clear(effects());
}

/* Remove a finished effect's code from the active list. */
void Player::endEffect(int code)
{
    LinkedListNode *node = LinkedList_Find(effects(), (void *)(unsigned int)code, NULL);
    LinkedList_Unlink(effects(), node);
}

unsigned int Player::updateTileEffects()
{
    fx_init();

    /* ── 1. Latch the clock, then move ───────────────────────────────── */
    copy8(&tickStepCopy_, tickStep_);
    copy8(&now_, clock_);

    pendingMove_ = 0;

    if ((signed char)anim_ != 10)
        updateMovement();

    /* ── 2. Respawn ──────────────────────────────────────────────────── */
    if (moveState_ != 0) {
        curTile()->setOccupant(0);

        field_1da = 0;
        field_1e6 = 0;
        climbDir_ = 0;
        field_1fe = 0;
        field_20a = 0;

        clearEffects();

        copy8(&lastActive_, &now_);
        idleStarted_ = 0;

        if (posY_ <= K_LAND_Y) {
            anim_ = 8;
        } else {
            /* The `<` is evaluated BEFORE the state store in the original;
             * kept in that order although nothing here reads +0x9a. */
            int rising = (posY_ < K_RESPAWN_TOP);
            anim_ = 10;
            if (rising)
                posY_ = (float)(load_double(&tickStepCopy_) * (double)K_RISE_RATE
                                + (double)posY_);
        }
    }

    /* ── 3. The tile gate ────────────────────────────────────────────── */
    pickedUp_ = 0;

    {
        Tile *t = curTile();
        int tileH = (int)t->height();                     /* zero-extended */
        int y     = (int)(signed char)(int)posY_;         /* __ftol, then movsbl */
        int onTile;

        if (y != tileH)
            goto expire;

        /* Within tolerance of the cell centre on all three axes, OR the
         * +0x14e flag is clear.  Each axis: sqrt(d*d*3.0) < 0.3. */
        {
            /* x87 computes these in extended precision (fsubrs / fmulp /
             * fmuls / fsqrt / fcomps), so the intermediates are held in
             * DOUBLE here rather than float -- a float temporary would round
             * twice and can land on the other side of the 0.3 threshold. */
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

        /* ── The random roll for a -1 tile ───────────────────────────── */
        if ((signed char)t->contents() == -1) {
            int r = (int)crt_rand() * 8;
            /* 8-bit add, exactly as `add $0x5,%dl`. */
            signed char rolled = (signed char)((char)(r / 0x7FFF) + 5);

            field_230 = rolled;
            curTile()->setContents((unsigned char)rolled);
        }

        /* ── 6: time bonus ───────────────────────────────────────────── */
        if ((signed char)curTile()->contents() == 6) {
            /* Five more seconds on the map's time limit, through the tile
             * base (the original's `[tileBase+4]`). */
            LevelMap *map = LevelMap::fromTileBase(tileBase_);
            map->setTimeLimit(map->timeLimit() + 5);
            curTile()->setContents(0);
            itemsCollected_ += 1;
            field_1ca = (double)(map->timeLimit() + 1);

            pickupSound(pickupSounds_[SND_176]);
            pickedUp_ = 6;
        }

        /* ── 1: crystal (VoicePool, not a static buffer) ─────────────── */
        if ((signed char)curTile()->contents() == 1) {
            gemsCollected_ += 1;
            curTile()->setContents(0);
            itemsCollected_ += 1;

            if (pool_9f_ != NULL) {
                Sim_BroadcastPoolVoiceCoordinates(
                    pool_9f_,
                    (float)(int)cellU_,
                    (float)(int)heightCell_,
                    -(float)(int)cellV_,
                    1);
                Sim_VoicePoolCycle(pool_9f_, 0);
            }
            pickedUp_ = 1;
        }

        /* ── 7 ───────────────────────────────────────────────────────── */
        if ((signed char)curTile()->contents() == 7) {
            field_239 += 1;
            curTile()->setContents(0);
            itemsCollected_ += 1;

            pickupSound(pickupSounds_[SND_16A]);
            pickedUp_ = 7;
        }

        /* ── 5: gated on +0x120, and its sound is indexed by +0x15a ──── */
        if ((signed char)curTile()->contents() == 5 && falling_ == 0) {
            glides_ += 1;
            curTile()->setContents(0);
            itemsCollected_ += 1;

            {
                CStaticSoundbuffer *buf = pickupSounds_[SND_19A][field_15a];
                if (buf != NULL) {
                    playAtCell(buf);
                    CStatic_TriggerPlayback(pickupSounds_[SND_19A][field_15a], 0);
                }
            }
            pickedUp_ = 5;
        }

        /* ── 9 ───────────────────────────────────────────────────────── */
        if ((signed char)curTile()->contents() == 9) {
            field_e8 += 3;
            curTile()->setContents(0);
            itemsCollected_ += 1;

            pickupSound(pickupSounds_[SND_1A6]);
            pickedUp_ = 9;
        }

        /* Timed effects: push the code onto the active list only if the
         * effect is not already running, stamp the start time, raise the
         * flag. */

        /* ── 8: timed ────────────────────────────────────────────────── */
        if ((signed char)curTile()->contents() == 8) {
            if (field_1e6 == 0)
                appendEffect(8);
            copy8(&field_1de, &now_);
            field_1e6 = 1;
            curTile()->setContents(0);
            itemsCollected_ += 1;

            pickupSound(pickupSounds_[SND_15E]);
            pickedUp_ = 8;
        }

        /* ── 0xb: timed, reverses PlayerMoveForward ──────────────────── */
        if ((signed char)curTile()->contents() == 0xb) {
            if (field_1da == 0)
                appendEffect(0xb);
            copy8(&field_1d2, &now_);
            field_1da = 1;
            curTile()->setContents(0);
            itemsCollected_ += 1;

            pickupSound(pickupSounds_[SND_1B2]);
            pickedUp_ = 0xb;
        }

        /* ── 0xa: timed, speed change ────────────────────────────────── */
        if ((signed char)curTile()->contents() == 0xa) {
            if (field_20a == 0)
                appendEffect(0xa);
            copy8(&field_202, &now_);
            field_20a = 1;
            stepDuration_ = 100.0;                     /* two dwords: 0, 0x40590000 */
            curTile()->setContents(0);
            itemsCollected_ += 1;

            pickupSound(pickupSounds_[SND_182]);
            pickedUp_ = 0xa;
        }

        /* ── 0xc: timed, speed change ────────────────────────────────── */
        if ((signed char)curTile()->contents() == 0xc) {
            if (field_1fe == 0)
                appendEffect(0xc);
            copy8(&field_1f6, &now_);
            field_1fe = 1;
            stepDuration_ = 400.0;                     /* two dwords: 0, 0x40790000 */
            curTile()->setContents(0);
            itemsCollected_ += 1;

            pickupSound(pickupSounds_[SND_18E]);
            pickedUp_ = 0xc;
        }

        /* ── 0xd: timed, transforms the player and the tile occupant ─── */
        if ((signed char)curTile()->contents() == 0xd) {
            if (climbDir_ == 0)
                appendEffect(0xd);

            climbDir_ = 1;
            kind_     = 3;
            curTile()->setOccupant(3);

            /* NOTE the start time is stamped AFTER the flag, unlike the
             * other four. */
            copy8(&field_1ea, &now_);

            curTile()->setContents(0);
            itemsCollected_ += 1;

            pickupSound(pickupSounds_[SND_1BE]);
            pickedUp_ = 0xd;
        }
    }

expire:
    /* ── 4. Expiry ───────────────────────────────────────────────────────
     *
     * Effect 8 ends at >= 5000 ms; the other four end at > 10000 ms.  See
     * the header -- the asymmetry is in the original's `test` masks. */

    if (field_1e6 != 0) {
        if (now_ - field_1de >= K_EFFECT8_MS) {
            field_1e6 = 0;
            endEffect(8);
        }
    }

    if (field_1da != 0) {
        if (now_ - field_1d2 > K_EFFECT_MS) {
            field_1da = 0;
            endEffect(0xb);
        }
    }

    if (field_20a != 0) {
        if (now_ - field_202 > K_EFFECT_MS) {
            stepDuration_  = 200.0;                    /* two dwords: 0, 0x40690000 */
            field_20a = 0;
            endEffect(0xa);
        }
    }

    if (field_1fe != 0) {
        if (now_ - field_1f6 > K_EFFECT_MS) {
            stepDuration_  = 200.0;                    /* two dwords: 0, 0x40690000 */
            field_1fe = 0;
            endEffect(0xc);
        }
    }

    if (climbDir_ != 0) {
        if (now_ - field_1ea > K_EFFECT_MS) {
            climbDir_ = 0;
            kind_     = 4;
            curTile()->setOccupant(4);
            endEffect(0xd);
        }
    }

    /* Deliberate deviation: the original returns garbage in the high bytes
     * of EAX.  The sole caller discards it.  See the header. */
    return 0;
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_UpdatePlayerTileEffects(Player *self)
{
    return self->updateTileEffects();
}
