/* MovableEntity -- the base of Bomb, Foe and Player (see movableentity.h):
 * its construction, destruction and sound-slot reset here, and the shared
 * movement step, updateMovement(), in the second half (was entitymove.cpp).
 *
 *     PopulateMovableEntityBase     0x00438720
 *     ZeroEntitySoundSlotPointers   0x0043ad60
 *     DestroyMovableEntityBase      0x00438760
 *
 * ─── From the listings ───────────────────────────────────────────────────
 *
 *   00438720  PUSH ESI; MOV ESI,ECX; CALL 0x401000; MOV [ESI],0x45d6a4;
 *             MOV EAX,ESI; POP ESI; RET           -- returns `this`
 *   00401000  [EAX] = 0x45d290, then +0x25/+0x29/+0x2d from three zero
 *             dwords staged on the stack          -- 0.0f is all-zero bits
 *   00438760  MOV [ECX],0x45d6a4; JMP 0x401060
 *   00401060  MOV [ECX],0x45d290; RET             -- EAX is not set; no
 *                                                    caller reads it
 *   0043ad60  zero +0xa7,+0xab,+0xaf,+0xb3,+0xb7,+0xbb,+0xbf,+0xc3,+0xc7,
 *             then +0xa3, +0xcb, +0xcf            -- that order
 *
 * The level-object base 0x401000 / 0x401060 is NOT taken: its other callers
 * are the four level-object ctors and dtors, all ours and UD2-stubbed, plus
 * 0x401043 inside its own scalar deleting dtor.  The movable base simply does
 * its field work itself, as those four classes already do.
 *
 * ─── Who calls what ──────────────────────────────────────────────────────
 *
 * xref.py over Karoo.exe.orig:
 *
 *   0x438720  E8  0x0040271E (Bomb ctor)  0x0041200E (Foe ctor)
 *                 0x0041F91E (Player ctor)
 *   0x43ad60  E8  0x0040273D (Bomb)  0x00412021 (Foe)
 *                 0x0041F972 0x0041F9C8 (Player, twice)
 *   0x438760  E8  0x00412228 (Foe dtor body 0x412160)
 *                 0x0041FA70 (Player dtor body 0x41fa10)
 *                 0x00438743 (DeleteMovableEntityWithFlags 0x438740)
 *             E9  0x00402866 (Bomb dtor body, now stubbed)
 *                 0x0045BC83 0x0045BCE3 0x0045BD03 0x0045C013 0x0045C033 --
 *                 EH unwind funclets, `mov ecx,[ebp-0x10]; jmp`, run only if
 *                 a derived ctor throws part-way, which never happens (the
 *                 allocator returns NULL rather than throwing)
 *
 * CALL_PATCHES rewrites the E8s and JMP_PATCHES the E9s, so all three
 * originals are UD2-stubbed.  The funclets jump in with `this` in ECX and
 * the unwinder's return address on the stack -- exactly a __thiscall call
 * with no arguments, which is what the export is.
 *
 * DeleteMovableEntityWithFlags 0x438740 (vtable 0x45d6a4, one slot) is OURS
 * as of ENDGAME_PLAN E2, and it closes the TU.  It would run only for a bare
 * MovableEntity, which nothing creates -- but "nothing creates one" is an
 * argument for a stub, not for leaving the game's table installed, and the
 * table is the only thing that can reach it.  So the base installs our own
 * one-slot table instead (the licence in ENDGAME_PLAN.md), the game's table
 * at 0x45d6a4 keeps pointing at the UD2, and a reader we failed to find
 * faults instead of quietly working.
 *
 * A byte scan of Karoo.exe.orig for the literal 0x0045d6a4 finds exactly two
 * occurrences, 0x3872a and 0x38762 -- inside 0x438720 and 0x438760, the two
 * functions this file already owns.  Nothing else in the binary installs that
 * table, which is what makes swapping it ours a local decision.
 */

#include "movableentity.h"
#include "ani.h"
#include "alloc.h"
#include "levelobjbase.h"

/* The game's vtables these functions store.  Both stores are transient --
 * a derived ctor overwrites the pointer next, and a derived dtor's free
 * follows -- but they are stores, so they are reproduced. */
/* The level-object base table is ours too now; see levelobject.h. */
#define GAME_LEVELOBJECT_VTBL ((const void *)LevelObjBase_Vtable())

/* Our own one-slot table, replacing the game's 0x45d6a4 in the two stores
 * below.  Slot 0 is the scalar deleting destructor, 0x438740 (declared in
 * the header, with the rest of this file's exports). */
static void *const g_MovableVtable[1] =
    { (void *)&Sim_DeleteMovableEntityWithFlags };

#define GAME_MOVABLE_VTBL     ((const void *)g_MovableVtable)

MovableEntity::MovableEntity()
{
    /* 0x401000's field work; the vtable is the subclass's to set. */
    posU_ = 0.0f;
    posY_ = 0.0f;
    posV_ = 0.0f;
}

void MovableEntity::populateBaseForGame()
{
    vtable_ = GAME_LEVELOBJECT_VTBL;       /* 0x401000 */
    posU_   = 0.0f;
    posY_   = 0.0f;
    posV_   = 0.0f;
    vtable_ = GAME_MOVABLE_VTBL;           /* 0x438728 */
}

void MovableEntity::destroyBaseForGame()
{
    vtable_ = GAME_MOVABLE_VTBL;           /* 0x438760 */
    vtable_ = GAME_LEVELOBJECT_VTBL;       /* 0x401060 */
}

/* See the header.  The typedef is __thiscall because the game's slot 0 is;
 * `self` is typed MovableEntity * because every object that reaches here is
 * one -- Object_DestroyAndCompactId's only callers hold Foe ** and Bomb **,
 * and both derive from this class. */
typedef void (__attribute__((thiscall)) *scalar_dtor_fn)(MovableEntity *self,
                                                         int flags);

void MovableEntity::destroyViaVtable(int flags)
{
    /* Through a void * first: the class is __attribute__((packed)), so
     * casting `this` straight to a function-pointer pointer trips
     * -Waddress-of-packed-member.  The vtable pointer is the object's first
     * dword either way. */
    void *raw = this;

    scalar_dtor_fn *vtbl = *(scalar_dtor_fn **)raw;
    vtbl[0](this, flags);
}

void MovableEntity::zeroSoundSlots()
{
    sound_a7_ = 0;
    sound_ab_ = 0;
    sound_af_ = 0;
    sound_b3_ = 0;
    sound_b7_ = 0;
    sound_bb_ = 0;
    sound_bf_ = 0;
    sound_c3_ = 0;
    sound_c7_ = 0;
    sound_a3_ = 0;
    sound_cb_ = 0;
    sound_cf_ = 0;
}

/* ═══ Exports -- thin ABI shims ═══════════════════════════════════════════ */

extern "C" __declspec(dllexport) MovableEntity *__attribute__((thiscall))
Sim_PopulateMovableEntityBase(MovableEntity *self)
{
    self->populateBaseForGame();
    return self;
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_ZeroEntitySoundSlotPointers(MovableEntity *self)
{
    self->zeroSoundSlots();
}

extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_DestroyMovableEntityBase(MovableEntity *self)
{
    self->destroyBaseForGame();
}

/* ─── DeleteMovableEntityWithFlags 0x00438740 ─────────────────────────────
 *
 * Vtable slot 0, and the TU's last function.  Three statements in the
 * original: the dtor body, then FactAlloc::Free2 when bit 0 of the flag word
 * is set, returning `this` either way.  There is no array form -- bit 1 is
 * not tested, unlike CStaticSoundbuffer's combined scalar/vector dtor.
 *
 * The free stays on the GAME heap through alloc.h: a MovableEntity is only
 * ever the base of a Foe, a Player or a Bomb, and the game's `operator new`
 * allocated all three, so the other side of the lifetime is still theirs.
 *
 * Nothing calls it: xref.py reports no reference of any kind, our table's
 * slot 0 is the only way in, and no derived object carries our table for
 * longer than the two straight-line stores in the ctor and dtor above.  It
 * is therefore reimplemented but unverified by test -- exactly the position
 * LinkedList::ScalarDestructor is in, and recorded rather than papered over.
 */
extern "C" __declspec(dllexport) MovableEntity *__attribute__((thiscall))
Sim_DeleteMovableEntityWithFlags(MovableEntity *self, unsigned int flags)
{
    self->destroyBaseForGame();
    if (flags & 1)
        game_free2(self);
    return self;
}

/* ═══ UpdateEntityMovement 0x00438770 (was entitymove.cpp) ═══════════════ */

/* GAMETICK_PLAN.md Band A — Game::UpdateEntityMovement 0x00438770.
 *
 * ─── What this is ────────────────────────────────────────────────────────
 *
 * The movement integrator every entity tick ends in, and what the plan calls
 * "Band A's centre of gravity".  The player and every foe run this same code:
 * `this` is the entity, and the player's is Game+0x1751c9.
 *
 * It is the gate the rest of Band A stands behind.  UpdateBombFuseAndBlast,
 * UpdatePlayerTileEffects and SetFoeChaseTarget all call it, and the
 * no-callback rule forbids a replacement calling the original — so this had
 * to land before any of them.
 *
 * ─── Why it was takeable only now ────────────────────────────────────────
 *
 * Its callee list (read this session, per the plan's standing rule that "the
 * next target" is a hypothesis until the list is read) is:
 *
 *   CheckTileIsRamp               0x41f8a0   ours, entitymath.cpp
 *   GetTurnedDirection            0x43ad40   ours, entitymath.cpp
 *   VoicePoolCycle                0x442d90   ours, voicepool.cpp
 *   BroadcastPoolVoiceCoordinates 0x442df0   ours, voicepool.cpp
 *   Set3DPosition/TriggerPlayback/HaltPlayback   ours, static.cpp
 *   __ftol                        0x451134   CRT
 *
 * Every one is already ours, so this replacement calls nothing in the game
 * binary.  That is the whole reason the four leaves were taken first.
 *
 * ─── Interception ────────────────────────────────────────────────────────
 *
 * __thiscall, `this` in ECX, no stack arguments (the epilogue is a bare RET).
 * FOURTEEN E8 call sites and nothing else -- tools/xref.py reports all 14 as
 * CALL, no DATA reference and no `68 imm32`, so CALL_PATCHES alone covers it:
 *
 *   0x402B7C 0x402B95 0x402BAD                 UpdateBombFuseAndBlast
 *   0x4123F4 0x41251D                          UpdateFoeObjectStep
 *   0x41FABB 0x41FADE                          PlayerMoveForward
 *   0x41FB25 0x41FB3E                          PlayerMoveBack
 *   0x41FBB8 0x41FBDD                          PlayerTurnLeft
 *   0x41FC58 0x41FC7D                          PlayerTurnRight
 *   0x41FCED                                   UpdatePlayerTileEffects
 *
 * A `grep -rn 438770 karoo-hooks/` found only comments, so unlike
 * CalculateLevelScore there is no absolute-address call in our own DLL to
 * redirect (GAMETICK_PLAN.md's "xref.py over the exe is only half the search"
 * hazard -- checked, and clear).
 *
 * ─── The return value: a DELIBERATE DEVIATION ────────────────────────────
 *
 * The epilogue is `XOR AL,AL` then RET, and the two early-out paths return
 * with AL = 1.  So the result is a byte in AL and the upper three bytes of
 * EAX are whatever happened to be there -- which is why Ghidra renders the
 * returns as CONCAT31(garbage, 1) and `uVar18 & 0xffffff00`.
 *
 * THIS REPLACEMENT ZEROES THOSE UPPER BYTES; the original does not.  Every
 * call site was compiled against a byte-returning function, and the one
 * caller that propagates EAX further (UpdateBombFuseAndBlast tail-returns it)
 * feeds callers that test AL.  Recorded here as a checked deviation rather
 * than an oversight -- if a replay ever diverges, this is on the short list.
 *
 * ─── Field access ────────────────────────────────────────────────────────
 *
 * The body is MovableEntity::updateMovement() (COHESION_PLAN.md Band 4b,
 * pass 1): every entity offset is a MovableEntity field, `field_<off>` where
 * the meaning is unknown.  Where the original reads a byte with the other
 * signedness from its declaration, the read is an explicit cast.  The tile
 * side goes through Tile (tile.h), with the same casts for its signed reads.
 * The offsets are the decompile's, and the tile
 * offsets cross-check exactly against worldstate.h's absolute addresses:
 * entity+0x34 points at Game+0x2ab58d, so tile field +0x19d lands on
 * Game+0x2ab72a (kind), +0x19c on 0x2ab729 (height), +0x1a5 on 0x2ab732
 * (occupant), +0x1a6 on 0x2ab733 (height_f) and +0x217 on 0x2ab7a4 (spent).
 * All five agree with worldstate.h, which was derived independently.
 *
 * Timestamps are 8-byte doubles at ODD offsets (+0x04, +0x50, +0x112,
 * +0x146, ...), so every access is unaligned; the packed classes say so,
 * MovableEntity for the entity and Tile for the tile.
 *
 * Where the original copies a double as two dwords, this copies eight bytes,
 * so a stored NaN payload survives identically.
 *
 * ─── Precision ───────────────────────────────────────────────────────────
 *
 * Ghidra renders the FPU intermediates as `float10` because the original is
 * x87 code holding 80-bit temporaries.  This replacement uses plain
 * float/double throughout, at the user's direction; the build is -O0 32-bit
 * mingw, which is x87 anyway.
 *
 * Three regions WERE read from the disassembly, but to recover arguments the
 * decompiler DROPPED, not to chase precision: `__ftol()` appears in the
 * pseudocode with empty parentheses at all three of its call sites, so what
 * it truncates had to be read from the listing.  They are:
 *
 *   0x439811   __ftol(now - fallStart)          elapsed ms
 *   0x4398c5   __ftol(the glide height)         via FST -- store AND keep
 *   0x439955   __ftol(the fall height)          via FST -- store AND keep
 *
 * ─── Bugs preserved deliberately ─────────────────────────────────────────
 *
 * 1. THE HEIGHT TRUNCATION IS `INC AL` ON A BYTE.  gh = (byte)trunc(h) + 1
 *    wraps at 256 rather than saturating.  Reproduced as a byte add.
 * 2. __ftol's HIGH DWORD IS FORCED TO ZERO at 0x439816 (the compiler stores a
 *    known-zero register there), so a negative elapsed time reads as a huge
 *    positive one.  Reproduced by taking the low 32 bits unsigned.
 * 3. THE GLUE PAD FREEZES FOES TOO, not just the player -- same code path,
 *    as worldstate.h already notes.
 * 4. Several `!= 9` guards on entity+0x152 skip the occupant bookkeeping for
 *    one entity kind, leaving stale occupant bytes behind.  Left as-is.
 * 5. The cell-being-left index is built by SUBTRACTING the step deltas, with
 *    the *100 applied to the U delta only.  Not "cleaned up" into the
 *    obvious symmetric form -- CLAUDE.md's rule about index arithmetic.
 *
 * ─── Visual proof (acceptance point 5) ───────────────────────────────────
 *
 * KAROO_SIM_FX=floaty  divides the fall acceleration by five, so a drop that
 *   normally kills takes visibly longer and the arc is shallow.  5.405 is a
 *   constant only this function owns.
 * KAROO_SIM_FX=hop     quadruples the jump-pad arc constant (7.2 -> 28.8), so
 *   a pad throws the entity far higher than it should.
 *
 * Both are measurements rather than colours, and both are read by VALUE via
 * GetEnvironmentVariableA, never by presence -- RENDER_PLAN.md 2026-09-02 is
 * why that distinction matters.
 */
#include <windows.h>
#include <string.h>
#include <math.h>
#include "static.h"
#include "log.h"
#include "entitymath.h"
#include "voicepool.h"
#include "movableentity.h"
#include "tile.h"
#include "levelmap.h"

/* ─── The constants, read out of .rdata this session ──────────────────────
 *
 * Named for what they do, with the address they came from, so a future
 * re-read can check them without re-deriving which is a float and which a
 * double.  That split is the original's and it is load-bearing: a double
 * 0.25 compared against a float expression rounds differently from a float
 * 0.25 would.
 */
static const double K_ZERO         = 0.0;      /* 0045d390  double */
static const double K_ONE          = 1.0;      /* 0045d2e8  double */
static const double K_HALF_D       = 0.5;      /* 0045d6e0  double */
static const float  K_HALF_F       = 0.5f;     /* 0045d318  float  */
static const double K_ONE_HALF     = 1.5;      /* 0045d6a8  double */
static const double K_MS_TO_S_D    = 0.001;    /* 0045d368  double */
static const float  K_MS_TO_S_F    = 0.001f;   /* 0045d308  float  */
static const double K_TWO_MS       = 0.002;    /* 0045d6b0  double */
static const float  K_GRAVITY_HALF = 4.905f;   /* 0045d6b8  float  */
static const float  K_GRAVITY_TWO  = 19.62f;   /* 0045d6bc  float  */
static const float  K_ARC_BIAS     = 7.2f;     /* 0045d6c0  float  */
static const float  K_FALL_ACCEL   = 5.405f;   /* 0045d6c4  float  */
static const float  K_GLIDE_RATE   = 0.004f;   /* 0045d6c8  float  */
static const float  K_SNAP_EPS     = 0.15f;    /* 0045d6cc  float  */
static const double K_LINK_EPS     = 0.25;     /* 0045d6d0  double */
static const double K_GLUE_MS      = 3000.0;   /* 0045d6d8  double */
static const double K_HALF_SEC_MS  = 500.0;    /* 0045d6e8  double */
static const double K_IDLE_MS      = 5000.0;   /* 0045d2d8  double */
static const float  K_GLIDE_DROP   = 2.0f;     /* 0045d3bc  float  */
static const double K_RAMP_EPS     = 0.2;      /* 0045d3e0  double */

/* ─── KAROO_SIM_FX, read by value ─────────────────────────────────────── */
static int s_fx = -1;                 /* 0 none, 1 floaty, 2 hop */
static void fx_init(void)
{
    if (s_fx >= 0)
        return;
    char buf[32];
    DWORD n = GetEnvironmentVariableA("KAROO_SIM_FX", buf, sizeof(buf));
    s_fx = 0;
    if (n > 0 && n < sizeof(buf)) {
        if (lstrcmpiA(buf, "floaty") == 0) {
            s_fx = 1;
            log_write("entitymove: KAROO_SIM_FX=floaty -- fall accel %.3f, not %.3f\n",
                      (double)(K_FALL_ACCEL / 5.0f), (double)K_FALL_ACCEL);
        } else if (lstrcmpiA(buf, "hop") == 0) {
            s_fx = 2;
            log_write("entitymove: KAROO_SIM_FX=hop -- arc bias %.1f, not %.1f\n",
                      (double)(K_ARC_BIAS * 4.0f), (double)K_ARC_BIAS);
        }
    }
}
static inline float fall_accel(void) { return s_fx == 1 ? K_FALL_ACCEL / 5.0f : K_FALL_ACCEL; }
static inline float arc_bias(void)   { return s_fx == 2 ? K_ARC_BIAS * 4.0f  : K_ARC_BIAS; }

/* Copy eight bytes, the way the original copies a double as two dwords. */
static inline void copy8(void *dst, const void *src) { memcpy(dst, src, 8); }

/* The tick reads the 8-byte record copy at +0x15 as a double (the glide). */
static inline double load_double(const void *p)
{
    double v;
    memcpy(&v, p, 8);
    return v;
}

/* Tile addressing: base + (v + u*100) * 0x7f, both axes read signed. */
#define TILE(u, v)   Tile::at(tileBase_, (u), (v))
#define GU           cellU_
#define GV           cellV_
#define GH           heightCell_
#define CUR          TILE(GU, GV)


/* Play a positioned one-shot; the original open-codes this eight times. */
static inline void snd_at(CStaticSoundbuffer *s, float x, float y, float z, DWORD loop)
{
    if (s == 0)
        return;
    CStatic_Set3DPosition(s, x, y, z, 1);
    CStatic_TriggerPlayback(s, loop);
}

/* Is direction `d` the entity's facing, or its reverse?  The original
 * open-codes this more than a dozen times as
 * `d == facing || d == GetTurnedDirection(facing, 2)`.  The second call is
 * made only when the first compare fails; GetTurnedDirection is ours and
 * pure, so the short-circuit is unobservable, but it is what the original
 * does. */
static inline bool facing_or_reverse(unsigned d, unsigned char facing)
{
    if (d == (unsigned)facing)
        return true;
    return d == (unsigned)Sim_GetTurnedDirection(facing, 2);
}

/* __ftol as the call sites use it: truncate toward zero into a 64-bit
 * result, of which only the low dword is kept -- the high dword is written
 * from a known-zero register at 0x439816.  See "Bugs preserved", item 2. */
static inline unsigned ftol32(double v)
{
    return (unsigned)(long long)v;
}

unsigned int MovableEntity::updateMovement()
{
    fx_init();

    if (moveDir_ == 0 && ((signed char)moveState_) == 0)
        anim_ = 0;

    if (idleStarted_ == 0 && climbing_ == 0)
        copy8(&animDuration_, &stepDuration_);                    /* move duration <- default */

    /* ─── The two early outs.  Both return 1 in AL. ─────────────────── */
    if (dying_ != 0) {
        if (dyingStarted_ == 0) {
            copy8(&dyingSince_, &now_);
            dyingStarted_ = 1;
            field_7a = (void *)1;
            pendingMove_ = 0;
            moveDir_ = 0;
            return 1;
        }
        if (!(now_ - dyingSince_ < K_HALF_SEC_MS))
            removeRequested_ = 1;
        pendingMove_ = 0;
        moveDir_ = 0;
        return 1;
    }

    if (held_ != 0)                      /* frozen: drop the queued move */
        pendingMove_ = 0;

    bool turned = false;

    if (moveDir_ != 0) {
        /* ─── A move is in progress ─────────────────────────────────── */
        if (((signed char)kind_) != 9 && ((signed char)kind_) != 3) {
            unsigned crush = CUR->blastHeight();
            if ((int)GH - 1 <= (int)crush && (int)crush <= (int)GH + 1 &&
                animDuration_ * K_MS_TO_S_D < now_ - animStart_)
                moveState_ = 4;
        }

        if (animDuration_ <= now_ - animStart_) {
            /* the step has run its time: commit it */
            movingBackwards_ = 0;
            stepEnd_ = animDuration_ + animStart_;

            if (facing_or_reverse(((unsigned)moveDir_), facing_)) {
                if (((signed char)kind_) != 9) {
                    /* clear the occupant of the cell being left -- see
                     * "Bugs preserved", item 5, for the index arithmetic. */
                    Tile *from = Tile::at(tileBase_,
                        (int)GU - (int)stepU_, (int)GV - (int)stepV_);
                    from->setOccupant(0);
                }
                if (Sim_CheckTileIsRamp(CUR->objectMarker()) != 0) {
                    posY_ = (float)(int)GH + K_HALF_F;
                } else if (gliding_ == 0) {
                    posY_ = (float)(int)GH;
                }
                if ((unsigned)lastMoveDir_ != ((unsigned)moveDir_)) {
                    posU_ = (float)(int)GU;
                    posV_ = (float)(int)GV;
                }
                if (climbing_ != 0)
                    posY_ = (float)(int)GH;
                field_d8 = field_d8 + 1;
                lastMoveDir_ = (unsigned char)moveDir_;
            } else {
                turned = true;
            }

            if (((signed char)field_124) == 0) {
                facing_ = (unsigned char)moveDir_;
            } else {
                int d = moveDir_;
                if (d > 10 && d < 0x14)
                    facing_ = (signed char)(d - 10);
            }

            if (((signed char)kind_) != 9) {
                unsigned char a = anim_;
                if (((a > 0x13 && a < 0x1c && moveDir_ != 0) || turned) &&
                    ((VoicePool *)sound_cf_) != 0 && a != 3 && a != 5) {
                    Sim_BroadcastPoolVoiceCoordinates(((VoicePool *)sound_cf_),
                        (float)(int)GU, (float)(int)GH, -(float)(int)GV, 1);
                    Sim_VoicePoolCycle(((VoicePool *)sound_cf_), 0);
                }
            }

            moveDir_ = 0;

            /* kind 4 re-queues whatever the input left in +0x60/+0x61 */
            Tile *t = CUR;
            signed char k = (signed char)t->objectMarker();
            if (((signed char)kind_) == 4) {
                if (k == 0x15 || k == 0x0f || k == 0x10) {
                    if ((unsigned)t->height() != (unsigned)(int)GH && k != 0x0e) {
                        pendingMove_ = queuedMove_;
                        turnKind_ = queuedTurn_;
                    }
                } else {
                    pendingMove_ = queuedMove_;
                    turnKind_ = queuedTurn_;
                }
            }
            queuedMove_ = 0;
            queuedTurn_ = 0;
        }
    }

    if (moveDir_ == 0) {
        /* ─── LAB_00438b45: idle, standing on a tile ────────────────── */
        if (((signed char)kind_) != 9 && ((signed char)kind_) != 3) {
            unsigned crush = CUR->blastHeight();
            if ((int)GH - 1 <= (int)crush && (int)crush <= (int)GH + 1)
                moveState_ = 4;
        }

        Tile *t = CUR;
        if ((signed char)t->objectMarker() == TILE_SWITCH && (unsigned)t->height() == (unsigned)(int)GH &&
            ((signed char)kind_) != 9 && field_d3 == 0) {
            field_d7 = t->field1f3();
            snd_at(sound_cb_, (float)(int)GU, (float)(int)GH, -(float)(int)GV, 0);
            field_d3 = 1;
        }

        t = CUR;
        if ((unsigned)(int)GH == (unsigned)t->height()) {
            /* --- glue pad (kind 2) --- */
            if ((signed char)t->objectMarker() == TILE_GLUE && t->busy() == 0) {
                if (field_126 == K_ZERO) {
                    copy8(&field_126, &now_);
                    if (field_156 != 0)
                        snd_at(sound_bf_, (float)(int)GU, (float)(int)GH,
                               -(float)(int)GV, 0);
                    anim_ = 9;
                }
                if (now_ - field_126 <= K_GLUE_MS) {
                    pendingMove_ = 0;               /* stuck: drop the queued move */
                } else {
                    memset(&field_126, 0, 8);
                    CUR->setBusy(1);        /* pad spent */
                }
            } else {
                memset(&field_126, 0, 8);
            }

            /* --- climb (kind 0x10) --- */
            Tile *c = CUR;
            if ((signed char)c->objectMarker() == TILE_CLIMB) {
                unsigned char dir = c->climbDir();
                if (climbing_ == 0)
                    snd_at(sound_af_, (float)(int)GU, (float)(int)GH,
                           -(float)(int)GV, 1);
                facing_  = dir;
                climbing_ = 1;
                pendingMove_ = dir;
                animDuration_ = 150.0;               /* two dwords: 0, 0x4062c000 */
            } else {
                copy8(&animDuration_, &stepDuration_);
                climbing_ = 0;
                if (sound_af_ != 0)
                    CStatic_HaltPlayback(sound_af_);
            }

            /* --- teleporter (kind 0x0f) --- */
            if ((signed char)CUR->objectMarker() == TILE_TELEPORTER) {
                pendingMove_ = 0;
                if (((signed char)teleportPhase_) == 1 && K_HALF_SEC_MS <= now_ - teleportSince_) {
                    copy8(&teleportSince_, &now_);
                    teleportPhase_ = 2;
                    CUR->setBusy(0);
                    unsigned char *base = tileBase_;
                    Tile *here = CUR;
                    signed char du = (signed char)here->teleportU();
                    signed char dv = (signed char)here->teleportV();
                    signed char dh = (signed char)Tile::at(base, du, dv)->height();
                    if (((signed char)kind_) != 9)
                        here->setOccupant(0);
                    cellU_ = du;
                    cellV_ = dv;
                    heightCell_ = dh;
                    posU_ = (float)(int)du;
                    posY_ = (float)(int)dh;
                    posV_ = (float)(int)dv;
                    CUR->setBusy(1);
                    snd_at(sound_bb_, (float)(int)GU, (float)(int)GH,
                           -(float)(int)GV, 0);
                }
                if (((signed char)teleportPhase_) == 0) {
                    copy8(&teleportSince_, &now_);
                    teleportPhase_ = 1;
                    CUR->setBusy(1);
                    snd_at(sound_b7_, (float)(int)GU, (float)(int)GH,
                           -(float)(int)GV, 0);
                }
                if (((signed char)teleportPhase_) == 2 && K_HALF_SEC_MS <= now_ - teleportSince_) {
                    teleportPhase_ = 0;
                    CUR->setBusy(0);
                    pendingMove_ = lastMoveDir_;
                }
            }

            /* --- attach to a moving platform (kind 0x0c) --- */
            if (((signed char)slideSlot_) == -1) {
                unsigned char *base = tileBase_;
                Tile *here = CUR;
                if ((signed char)here->objectMarker() == TILE_SLIDE_TRACK) {
                    unsigned pu = here->slideOriginU();
                    unsigned pv = here->slideOriginV();
                    Tile *p = Tile::at(base, pu, pv);
                    if (fabsf((p->slidePosU() + (float)K_HALF_D) -
                              (posU_ + K_HALF_F)) <= (float)K_LINK_EPS &&
                        fabsf((p->slidePosV() + (float)K_HALF_D) -
                              (posV_ + K_HALF_F)) <= (float)K_LINK_EPS &&
                        (float)here->height() <= posY_) {
                        slideSlot_ = here->slideSlot();
                    }
                }
            }

            /* --- conveyor (kind 0x15) --- */
            if ((signed char)CUR->objectMarker() == TILE_CONVEYOR) {
                if (conveyorDir_ == 0) {
                    pendingMove_ = lastMoveDir_;
                    turnKind_ = 0;
                    snd_at(sound_ab_, (float)(int)GU, (float)(int)GH,
                           -(float)(int)GV, 1);
                } else {
                    unsigned char d = (unsigned char)conveyorDir_;
                    pendingMove_ = d;
                    lastMoveDir_ = d;
                    turnKind_ = 0;
                }
                anim_ = 3;
                conveyorDir_ = (unsigned)lastMoveDir_;
            } else {
                conveyorDir_ = 0;
            }
        }

        Tile *t2 = CUR;
        if ((signed char)t2->objectMarker() != TILE_CONVEYOR ||
            (unsigned)t2->height() != (unsigned)(int)GH) {
            if (sound_ab_ != 0)
                CStatic_HaltPlayback(sound_ab_);
            conveyorDir_ = 0;
        }
    }

    /* ─── Ladder / lift tile (kind 9) ───────────────────────────────── */
    {
        Tile *t = CUR;
        if ((signed char)t->objectMarker() == TILE_LIFT && (unsigned)t->height() == (unsigned)(int)GH)
            onLift_ = 1;
        if (onLift_ != 0 && ((signed char)moveState_) == 0) {
            heightCell_ = t->height();
            posY_ = CUR->liftLiveHeight();
        }
    }

    /* ─── Riding a platform ─────────────────────────────────────────── */
    if (((signed char)slideSlot_) != -1 && ((signed char)moveState_) == 0) {
        unsigned char *base = tileBase_;
        Tile *here = CUR;
        unsigned pu = here->slideOriginU();
        unsigned pv = here->slideOriginV();
        Tile *p = Tile::at(base, pu, pv);
        cellU_ = p->slideCellU();
        cellV_ = p->slideCellV();
        posU_  = p->slidePosU();
        posY_  = p->slidePosY();
        posV_  = p->slidePosV();
        if (((signed char)kind_) != 9)
            CUR->setOccupant(((signed char)kind_));
        if (facing_or_reverse((unsigned)pendingMove_, facing_)) {
            if (K_SNAP_EPS < posU_ - (float)(int)GU ||
                K_SNAP_EPS < posV_ - (float)(int)GV)
                pendingMove_ = 0;
            else
                slideSlot_ = 0xff;
        }
    }

    if (falling_ != 0 && gliding_ == 0) {
        pendingMove_ = 0;
        turnKind_ = 0;
    }

    /* ─── Ground contact, falling and landing ───────────────────────── */
    if (moveDir_ == 0 || falling_ != 0) {
        signed char restore = 0;
        Tile *t = CUR;

        if ((signed char)t->objectMarker() == TILE_EMPTY && (signed char)t->height() != 0) {
            restore = (signed char)t->height();
            t->setHeight(0);
        }
        if (((signed char)moveState_) == 0 && GH < 0)
            moveState_ = 2;                       /* fell below the floor */

        t = CUR;
        bool go_fall;
        if ((signed char)t->objectMarker() == TILE_EMPTY && GH > -100) {
            go_fall = true;
        } else {
            signed char h    = GH;
            unsigned char th = t->height();
            go_fall = ((int)th < (int)h) ||
                      (gliding_ != 0 && moveDir_ != 0) ||
                      ((int)h < (int)th && th != 0 && h > -100);

            if (!go_fall && falling_ != 0) {
                /* ─── LANDED ─────────────────────────────────────── */
                if (((signed char)kind_) != 9) {
                    climbing_ = 0;
                    if ((signed char)CUR->occupant() != 0)
                        dying_ = 1;           /* landed on someone */
                    CUR->setOccupant(kind_);
                }
                if (gliding_ == 0) {
                    signed char h2 = GH;
                    if ((signed char)CUR->objectMarker() == TILE_JUMP_PAD ||
                        ((int)((unsigned)fallStartH_ - (int)h2) < 3 && h2 > 1)) {
                        moveState_ = 0;           /* survived */
                        if (((VoicePool *)sound_cf_) != 0 && ((signed char)kind_) == 4) {
                            Sim_BroadcastPoolVoiceCoordinates(((VoicePool *)sound_cf_),
                                (float)(int)GU, (float)(int)GH,
                                -(float)(int)GV, 1);
                            Sim_VoicePoolCycle(((VoicePool *)sound_cf_), 0);
                        }
                    } else if (((signed char)kind_) == 9 && h2 > 1) {
                        moveState_ = 0;
                        pendingMove_ = 0;
                    } else {
                        moveState_ = 2;           /* killed by the drop */
                        if (field_156 != 0 || ((signed char)kind_) == 2 || ((signed char)kind_) == 3)
                            snd_at(sound_a7_, (float)(int)GU, (float)(int)GH,
                                   -(float)(int)GV, 0);
                        if (sound_c3_ != 0)
                            CStatic_HaltPlayback(sound_c3_);
                        pendingMove_ = 0;
                    }
                }
                if (sound_c7_ != 0)
                    CStatic_HaltPlayback(sound_c7_);
                gliding_  = 0;
                anim_   = 0;
                falling_ = 0;
                posU_ = (float)(int)GU;
                posV_ = (float)(int)GV;
                if (Sim_CheckTileIsRamp(CUR->objectMarker()) != 0)
                    posY_ = (float)(int)GH + K_HALF_F;
                else
                    posY_ = (float)(int)GH;
            }
        }

        if (go_fall) {
            /* ─── LAB_004397ec ──────────────────────────────────── */
            if (falling_ == 0) {
                /* not falling yet: start the fall */
                copy8(&animDuration_, &stepDuration_);
                climbing_ = 0;
                if (sound_af_ != 0)
                    CStatic_HaltPlayback(sound_af_);
                if (onLift_ == 0) {
                    fallSpeed_   = -3.0f;          /* the bits 0xc0400000 */
                    fallStartH_  = ((unsigned char)heightCell_);       /* fall start height */
                    falling_ = 1;
                    posY_    = (float)((unsigned char)heightCell_);
                    copy8(&fallStart_, &now_);
                    pendingMove_  = 0;
                }
            } else {
                if (((signed char)kind_) != 9)
                    CUR->setOccupant(0);

                unsigned elapsed = ftol32(now_ - fallStart_);
                double   ms      = (double)elapsed;

                if (gliding_ == 0) {
                    /* --- free fall --- */
                    if (sound_c3_ != 0)
                        CStatic_Set3DPosition(sound_c3_, posU_, posY_,
                                              -posV_, 1);
                    if (facing_or_reverse((unsigned)pendingMove_, facing_))
                        pendingMove_ = 0;

                    double ts = ms * (double)K_MS_TO_S_F;
                    double h  = ((double)fallSpeed_ - ts * (double)fall_accel()) * ts
                                + (double)(unsigned)fallStartH_;
                    posY_ = (float)h;
                    heightCell_ = (signed char)((unsigned char)(unsigned)(long long)h + 1);

                    if (gliding_ == 0 &&
                        (int)((unsigned)fallStartH_ - (int)GH) > 2 &&
                        ((signed char)glides_) != 0) {
                        /* the paraglider opens: costs one charge */
                        glides_ = (signed char)(((signed char)glides_) - 1);
                        gliding_ = 1;
                        snd_at(sound_a3_, posU_, posY_, -posV_, 0);
                        snd_at(sound_c7_, posU_, posY_, -posV_, 1);
                    }

                    int drop = (int)((unsigned)fallStartH_ - (int)GH);
                    if (drop > 2 && gliding_ == 0 && drop < 6 && ((signed char)kind_) != 9) {
                        anim_ = 8;
                        snd_at(sound_c3_, posU_, posY_, -posV_, 0);
                    }

                    if (gliding_ != 0 ||
                        (K_GLIDE_DROP < (float)fallStartH_ - (float)(int)GH &&
                         ((signed char)glides_) != 0)) {
                        anim_ = 5;            /* LAB_00439a8f */
                    }
                } else {
                    /* --- gliding --- */
                    if (sound_c7_ != 0)
                        CStatic_Set3DPosition(sound_c7_, posU_, posY_,
                                              -posV_, 1);
                    unsigned char th = CUR->height();
                    if ((float)th < posY_ || posY_ < (float)th - K_HALF_F) {
                        double h = (double)posY_ - load_double(&tickStepCopy_) * (double)K_GLIDE_RATE;
                        posY_ = (float)h;
                        heightCell_ = (signed char)((unsigned char)(unsigned)(long long)h + 1);
                    } else {
                        heightCell_ = th;
                        posY_  = (float)(int)(signed char)th;
                    }
                    if (GH < 2)
                        gliding_ = 0;
                    anim_ = 5;
                }

                /* a ramp lets you settle half a step lower */
                if (Sim_CheckTileIsRamp(CUR->objectMarker()) != 0) {
                    unsigned char th = CUR->height();
                    if ((float)th < posY_ &&
                        posY_ <= (float)th + (float)K_HALF_D)
                        heightCell_ = (signed char)(GH - 1);
                }
            }
        }

        if (restore != 0)
            CUR->setHeight(restore);
        if (falling_ != 0 && ((signed char)kind_) != 9)
            CUR->setOccupant(0);
    }

    /* ─── Idle: jump pad (kind 0x0e), then start a queued move ──────── */
    if (moveDir_ == 0) {
        Tile *t = CUR;
        if ((signed char)t->objectMarker() == TILE_JUMP_PAD) {
            if ((unsigned)(int)GH == (unsigned)t->height())
                pendingMove_ = 0;

            if (field_11a == 0) {
                if ((unsigned)CUR->height() == (unsigned)(int)GH) {
                    snd_at(sound_b3_, (float)(int)GU, (float)(int)GH,
                           -(float)(int)GV, 0);
                    copy8(&fallStart_, &now_);        /* two dword copies */
                    field_11a = 1;
                    posY_ = (float)(int)GH;
                }
            } else {
                double ts = (now_ - fallStart_) * (double)K_MS_TO_S_F;
                Tile *c = CUR;
                unsigned char top = c->field1f1();
                if (posY_ < (float)top) {
                    double v0 = sqrt(((double)(unsigned char)(top - (unsigned char)GH)
                                      + (double)arc_bias()) * (double)K_GRAVITY_TWO);
                    posY_ = (float)((v0 - ts * (double)K_GRAVITY_HALF) * ts
                                      + (double)(int)GH);
                    if (sound_b3_ != 0)
                        CStatic_Set3DPosition(sound_b3_, (float)(int)GU,
                                              (float)(int)GH, -(float)(int)GV, 1);
                    anim_ = 0x0b;
                } else {
                    if (((signed char)kind_) != 9)
                        c->setOccupant(0);
                    signed char nh = (signed char)CUR->field1f1();
                    field_11a = 0;
                    heightCell_   = nh;
                    posY_    = (float)(int)nh;
                    pendingMove_  = lastMoveDir_;
                    anim_   = 0x0c;
                }
            }
        }

        if (((signed char)moveState_) != 0)
            pendingMove_ = 0;

        if (moveDir_ == 0 && pendingMove_ != 0) {
            /* ─── Start the queued move ─────────────────────────── */
            field_141 = 0;
            moveDir_ = (unsigned)pendingMove_;
            pendingMove_ = 0;
            stepU_ = 0;
            stepV_ = 0;
            if (climbing_ != 0)
                field_141 = 0xff;
            if (moveDir_ > 0x14)
                moveDir_ = moveDir_ - 0x14;   /* subtraction, not a modulo */
            if (moveDir_ == 1) stepV_ = 0xff;
            if (moveDir_ == 2) stepU_ = 1;
            if (moveDir_ == 3) stepV_ = 1;
            if (moveDir_ == 4) stepU_ = 0xff;

            Tile          *here    = CUR;
            unsigned char  h_here  = here->height();
            unsigned char  k_here  = here->objectMarker();
            Tile          *dest    = Tile::at(tileBase_,
                (int)stepU_ + (int)GU, (int)GV + (int)stepV_);
            unsigned char  h_dest  = dest->height();
            unsigned char  k_dest  = dest->objectMarker();
            field_12e = 0;

            if (gliding_ != 0 && k_dest != 0 && (int)GH == (int)h_dest - 1)
                moveDir_ = 0;
            if (k_dest == 0x0f)
                field_ee = (unsigned char)moveDir_;

            /* occupied destination blocks, with per-kind exceptions */
            if (facing_or_reverse(((unsigned)moveDir_), facing_)) {
                signed char occ = (signed char)dest->occupant();
                if (occ != 0) {
                    signed char me = ((signed char)kind_);
                    if (me == 3 || (me == 4 && occ == 3) || (me == 2 && occ != 4))
                        moveDir_ = 0;
                }
            }
            if (field_d3 != 0 && facing_or_reverse(((unsigned)moveDir_), facing_))
                field_d3 = 0;

            if (k_dest == 0x10 && k_here == 0x10) {
                anim_ = 4;
            } else {
                if (k_here == 9 && facing_or_reverse(((unsigned)moveDir_), facing_)) {
                    if ((float)K_RAMP_EPS < fabsf(posY_ - (float)(int)GH) ||
                        (int)GH == (int)h_dest - 1)
                        moveDir_ = 0;
                    else
                        onLift_ = 0;
                }
                if (k_dest == 0x16 ||
                    (k_dest == 0x17 && dest->busy() == 0))
                    moveDir_ = 0;
            }

            if (moveDir_ != 0 && anim_ > 0xf9)
                anim_ = 0;

            /* ─── Ramp bookkeeping: which climb animation, and may we ── */
            if (Sim_CheckTileIsRamp(k_here) != 0) {
                if (Sim_CheckTileIsRamp(k_dest) == 0 && h_here == h_dest)
                    anim_ = 0x1b;
                if (((unsigned)moveDir_) == (unsigned)(k_here - 4) ||
                    ((unsigned)moveDir_) == (unsigned)Sim_GetTurnedDirection(
                                      (unsigned char)(k_here - 4), 2)) {
                    if (h_here < h_dest) {
                        field_141  = 1;
                        field_12e = 1;
                        if (((signed char)anim_) == 0)
                            anim_ = (unsigned char)
                                ((-(Sim_CheckTileIsRamp(k_dest) != 0) & 0xfeU) + 0x1a);
                    }
                    if (h_dest == h_here) {
                        if (((signed char)anim_) == 0)
                            anim_ = (unsigned char)
                                ((-(Sim_CheckTileIsRamp(k_dest) != 0) & 0xfeU) + 0x1b);
                        field_12e = 2;
                    }
                } else if ((unsigned)h_dest == (unsigned)h_here + 1) {
                    moveDir_ = 0;
                }
            }

            if (Sim_CheckTileIsRamp(k_dest) == 0) {
                if (gliding_ == 0 && Sim_CheckTileIsRamp(k_here) == 0 &&
                    Sim_CheckTileIsRamp(k_dest) == 0) {
                    if (((signed char)anim_) == 0) {
                        if (((unsigned)moveDir_) == (unsigned)facing_)
                            anim_ = 0x14;
                        if (((unsigned)moveDir_) ==
                            (unsigned)Sim_GetTurnedDirection(facing_, 2))
                            anim_ = 0x15;
                    }
                    if ((unsigned)h_dest == (unsigned)h_here + 1)
                        moveDir_ = 0;
                }
            } else {
                if (h_dest < h_here) {
                    if (((signed char)anim_) == 0)
                        anim_ = (unsigned char)
                            ((-(Sim_CheckTileIsRamp(k_here) != 0) & 2U) + 0x17);
                    field_141  = 0xff;
                    field_12e = 2;
                }
                if (h_dest == h_here) {
                    if (((signed char)anim_) == 0)
                        anim_ = (unsigned char)
                            ((-(Sim_CheckTileIsRamp(k_here) != 0) & 2U) + 0x16);
                    field_12e = 1;
                }
            }

            /* a second occupancy test, this one on the real step only */
            {
                signed char occ = (signed char)dest->occupant();
                if (occ != 0 && (stepU_ != 0 || stepV_ != 0) &&
                    ((signed char)kind_) != 9) {
                    if (field_156 == 0) {
                        if (occ != 4)
                            moveDir_ = 0;
                    } else if (occ == 3) {
                        moveDir_ = 0;
                    }
                }
            }

            if (climbing_ != 0 && k_dest != 0x10)
                anim_ = 0x14;

            if (moveDir_ != 0) {
                /* commit: pick the step's start time and move the grid cell */
                double since = now_ - stepEnd_;
                if (since <= K_ZERO || stepGrace_ <= since)
                    copy8(&animStart_, &now_);
                else
                    copy8(&animStart_, &stepEnd_);

                if (((signed char)slideSlot_) == -1) {
                    if ((unsigned)lastMoveDir_ != ((unsigned)moveDir_)) {
                        posU_ = (float)(int)GU;
                        posV_ = (float)(int)GV;
                    }
                    int nu = (int)GU + (int)stepU_;
                    int nv = (int)GV + (int)stepV_;
                    if (nu < 0 || (int)(unsigned)LevelMap::fromTileBase(tileBase_)->extentU() <= nu ||
                        nv < 0 || (int)(unsigned)LevelMap::fromTileBase(tileBase_)->extentV() <= nv) {
                        moveDir_ = 0;          /* off the edge of the map */
                    } else {
                        cellU_ = (signed char)(GU + stepU_);
                        cellV_ = (signed char)(GV + stepV_);
                        heightCell_ = (signed char)(GH + field_141);
                    }
                }

                movingBackwards_ = (((signed char)turnKind_) == 3) ? 1 : 0;

                if (gliding_ == 0) {
                    if (((signed char)turnKind_) == 2) anim_ = 0x1e;
                    if (((signed char)turnKind_) == 4) anim_ = 0x1f;
                } else {
                    anim_ = 5;
                }

                if (((signed char)kind_) != 9)
                    CUR->setOccupant(((signed char)kind_));

                idleStarted_ = 0;
                copy8(&lastActive_, &now_);
            }
        }
    }

    /* ─── Height curves for the animation states ────────────────────── */
    onStairOrSlide_ = 0;
    if (((unsigned)moveDir_) == 0) {
        if (stepGrace_ <= now_ - stepEnd_ && ((signed char)slideSlot_) == -1) {
            posU_ = (float)(int)GU;
            posV_ = (float)(int)GV;
        }
    } else if (facing_or_reverse(((unsigned)moveDir_), facing_)) {
        double dur  = animDuration_ * K_TWO_MS;
        double inv  = K_ONE / dur;
        double tsec = (now_ - animStart_) * K_MS_TO_S_D;
        double bias = K_ZERO;
        double frac = now_ - animStart_;
        double gh   = (double)(int)GH;

        if (((signed char)kind_) == 9 && field_d8 == 0) {
            bias = tsec * inv - (inv / dur) * tsec * tsec * K_HALF_D;
            posY_ = (float)(gh + bias);
        }
        switch (((signed char)anim_)) {
        case 0x16:
            posY_ = (float)(gh + (K_ONE / animDuration_) * frac * K_HALF_D + bias);
            break;
        case 0x17:
            posY_ = (float)((gh + bias + K_ONE)
                              - (K_ONE / animDuration_) * frac * K_HALF_D);
            break;
        case 0x1a:
            posY_ = (float)((gh + bias) - K_HALF_D
                              + (K_HALF_D / animDuration_) * frac);
            break;
        case 0x1b:
            posY_ = (float)(((gh + bias) - K_HALF_D + K_ONE)
                              - (K_ONE / animDuration_) * frac * K_HALF_D);
            break;
        case 0x18:
            posY_ = (float)((gh + bias) - K_HALF_D
                              + (K_ONE / animDuration_) * frac);
            break;
        case 0x19:
            posY_ = (float)((gh + bias + K_ONE_HALF)
                              - (K_ONE / animDuration_) * frac);
            break;
        default:
            break;
        }
    }

    /* ─── Interpolate the horizontal position across the step ───────── */
    {
        double frac = (now_ - animStart_) / animDuration_;
        switch (moveDir_ - 1) {
        case 0: posV_ = (float)((double)(GV + 1) - frac); break;
        case 1: posU_ = (float)(frac + (double)(GU - 1)); break;
        case 2: posV_ = (float)(frac + (double)(GV - 1)); break;
        case 3: posU_ = (float)((double)(GU + 1) - frac); break;
        default: break;
        }
        if (climbing_ != 0)                      /* climbing: interpolate H */
            posY_ = (float)((double)(GH + 1) - frac);
    }

    /* ─── Footstep / idle bookkeeping ───────────────────────────────── */
    if (movingBackwards_ != 0) {
        signed char a = ((signed char)anim_);
        if (a == ANIM_STAIR_STAIR_DOWN || a == ANIM_FIELD_STAIR_DOWN ||
            a == ANIM_STAIR_FIELD_UP || a == ANIM_FIELD_STAIR_UP ||
            a == ANIM_STAIR_STAIR_UP)
            onStairOrSlide_ = 1;
    }
    {
        unsigned char a = anim_;
        if (a == ANIM_FIELD_STAIR_DOWN) onStairOrSlide_ = 1;
        if (a == ANIM_SLIDE)            onStairOrSlide_ = 1;
        if (a < 0xfa && a != 0) {
            copy8(&lastActive_, &now_);
            idleStarted_ = 0;
        }
    }

    /* ─── The idle timeout: 5 s standing still starts the idle anim ──
     *
     * The control flow is the original's, gotos and all: the 0xfa block is
     * reached either by falling out of the `anim == 0` branch or through
     * LAB_0043a90d, and is skipped entirely when +0x6e is clear. */
    bool run_idle;
    if (anim_ == 0) {
        double dv = now_ - lastActive_;
        if (dv < K_IDLE_MS) {
            run_idle = (idleStarted_ != 0);          /* LAB_0043a90d */
        } else if (idleStarted_ == 0) {
            copy8(&animStart_, &now_);
            idleStarted_ = 1;
            run_idle = true;                      /* LAB_0043a90d, now set */
        } else {
            run_idle = true;                      /* falls into the block */
        }
    } else {
        run_idle = (idleStarted_ != 0);              /* LAB_0043a90d */
    }

    if (run_idle) {
        double dur = idleDuration_;
        anim_   = 0xfa;
        animDuration_   = dur;
        if (now_ - animStart_ >= dur)
            copy8(&animStart_, &now_);
    }

    return 0;
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_UpdateEntityMovement(MovableEntity *self)
{
    return self->updateMovement();
}
