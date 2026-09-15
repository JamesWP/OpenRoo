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
 * DeleteMovableEntityWithFlags 0x438740 (vtable 0x45d6a4, one slot) is left
 * alone: it would run only for a bare MovableEntity, which nothing creates;
 * its one E8 into the dtor is rewritten with the rest.
 */

#include "movableentity.h"

/* The game's vtables these functions store.  Both stores are transient --
 * a derived ctor overwrites the pointer next, and a derived dtor's free
 * follows -- but they are stores, so they are reproduced. */
#define GAME_LEVELOBJECT_VTBL ((const void *)0x0045d290)
#define GAME_MOVABLE_VTBL     ((const void *)0x0045d6a4)

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

    if (field_14e == 0 && ((signed char)moveState_) == 0)
        field_9a = 0;

    if (field_6e == 0 && field_fb == 0)
        copy8(&field_132, &field_66);                    /* move duration <- default */

    /* ─── The two early outs.  Both return 1 in AL. ─────────────────── */
    if (field_86 != 0) {
        if (field_82 == 0) {
            copy8(&field_8a, &now_);
            field_82 = 1;
            field_7a = (void *)1;
            pendingMove_ = 0;
            field_14e = 0;
            return 1;
        }
        if (!(now_ - field_8a < K_HALF_SEC_MS))
            removeRequested_ = 1;
        pendingMove_ = 0;
        field_14e = 0;
        return 1;
    }

    if (field_ef != 0)                      /* frozen: drop the queued move */
        pendingMove_ = 0;

    bool turned = false;

    if (field_14e != 0) {
        /* ─── A move is in progress ─────────────────────────────────── */
        if (((signed char)kind_) != 9 && ((signed char)kind_) != 3) {
            unsigned crush = CUR->blastHeight();
            if ((int)GH - 1 <= (int)crush && (int)crush <= (int)GH + 1 &&
                field_132 * K_MS_TO_S_D < now_ - field_146)
                moveState_ = 4;
        }

        if (field_132 <= now_ - field_146) {
            /* the step has run its time: commit it */
            field_44 = 0;
            field_50 = field_132 + field_146;

            if (facing_or_reverse(((unsigned)field_14e), facing_)) {
                if (((signed char)kind_) != 9) {
                    /* clear the occupant of the cell being left -- see
                     * "Bugs preserved", item 5, for the index arithmetic. */
                    Tile *from = Tile::at(tileBase_,
                        (int)GU - (int)field_13f, (int)GV - (int)field_140);
                    from->setField1a5(0);
                }
                if (Sim_CheckTileIsRamp(CUR->objectMarker()) != 0) {
                    posY_ = (float)(int)GH + K_HALF_F;
                } else if (field_ea == 0) {
                    posY_ = (float)(int)GH;
                }
                if ((unsigned)field_108 != ((unsigned)field_14e)) {
                    posU_ = (float)(int)GU;
                    posV_ = (float)(int)GV;
                }
                if (field_fb != 0)
                    posY_ = (float)(int)GH;
                field_d8 = field_d8 + 1;
                field_108 = (unsigned char)field_14e;
            } else {
                turned = true;
            }

            if (((signed char)field_124) == 0) {
                facing_ = (unsigned char)field_14e;
            } else {
                int d = field_14e;
                if (d > 10 && d < 0x14)
                    facing_ = (signed char)(d - 10);
            }

            if (((signed char)kind_) != 9) {
                unsigned char a = field_9a;
                if (((a > 0x13 && a < 0x1c && field_14e != 0) || turned) &&
                    ((VoicePool *)sound_cf_) != 0 && a != 3 && a != 5) {
                    Sim_BroadcastPoolVoiceCoordinates(((VoicePool *)sound_cf_),
                        (float)(int)GU, (float)(int)GH, -(float)(int)GV, 1);
                    Sim_VoicePoolCycle(((VoicePool *)sound_cf_), 0);
                }
            }

            field_14e = 0;

            /* kind 4 re-queues whatever the input left in +0x60/+0x61 */
            Tile *t = CUR;
            signed char k = (signed char)t->objectMarker();
            if (((signed char)kind_) == 4) {
                if (k == 0x15 || k == 0x0f || k == 0x10) {
                    if ((unsigned)t->height() != (unsigned)(int)GH && k != 0x0e) {
                        pendingMove_ = field_60;
                        field_125 = field_61;
                    }
                } else {
                    pendingMove_ = field_60;
                    field_125 = field_61;
                }
            }
            field_60 = 0;
            field_61 = 0;
        }
    }

    if (field_14e == 0) {
        /* ─── LAB_00438b45: idle, standing on a tile ────────────────── */
        if (((signed char)kind_) != 9 && ((signed char)kind_) != 3) {
            unsigned crush = CUR->blastHeight();
            if ((int)GH - 1 <= (int)crush && (int)crush <= (int)GH + 1)
                moveState_ = 4;
        }

        Tile *t = CUR;
        if ((signed char)t->objectMarker() == 0x11 && (unsigned)t->height() == (unsigned)(int)GH &&
            ((signed char)kind_) != 9 && field_d3 == 0) {
            field_d7 = t->field1f3();
            snd_at(sound_cb_, (float)(int)GU, (float)(int)GH, -(float)(int)GV, 0);
            field_d3 = 1;
        }

        t = CUR;
        if ((unsigned)(int)GH == (unsigned)t->height()) {
            /* --- glue pad (kind 2) --- */
            if ((signed char)t->objectMarker() == 2 && t->field217() == 0) {
                if (field_126 == K_ZERO) {
                    copy8(&field_126, &now_);
                    if (field_156 != 0)
                        snd_at(sound_bf_, (float)(int)GU, (float)(int)GH,
                               -(float)(int)GV, 0);
                    field_9a = 9;
                }
                if (now_ - field_126 <= K_GLUE_MS) {
                    pendingMove_ = 0;               /* stuck: drop the queued move */
                } else {
                    memset(&field_126, 0, 8);
                    CUR->setField217(1);        /* pad spent */
                }
            } else {
                memset(&field_126, 0, 8);
            }

            /* --- climb (kind 0x10) --- */
            Tile *c = CUR;
            if ((signed char)c->objectMarker() == 0x10) {
                unsigned char dir = c->field1f2();
                if (field_fb == 0)
                    snd_at(sound_af_, (float)(int)GU, (float)(int)GH,
                           -(float)(int)GV, 1);
                facing_  = dir;
                field_fb = 1;
                pendingMove_ = dir;
                field_132 = 150.0;               /* two dwords: 0, 0x4062c000 */
            } else {
                copy8(&field_132, &field_66);
                field_fb = 0;
                if (sound_af_ != 0)
                    CStatic_HaltPlayback(sound_af_);
            }

            /* --- teleporter (kind 0x0f) --- */
            if ((signed char)CUR->objectMarker() == 0x0f) {
                pendingMove_ = 0;
                if (((signed char)field_ff) == 1 && K_HALF_SEC_MS <= now_ - field_100) {
                    copy8(&field_100, &now_);
                    field_ff = 2;
                    CUR->setField217(0);
                    unsigned char *base = tileBase_;
                    Tile *here = CUR;
                    signed char du = (signed char)here->field1ee();
                    signed char dv = (signed char)here->field1ef();
                    signed char dh = (signed char)Tile::at(base, du, dv)->height();
                    if (((signed char)kind_) != 9)
                        here->setField1a5(0);
                    cellU_ = du;
                    cellV_ = dv;
                    heightCell_ = dh;
                    posU_ = (float)(int)du;
                    posY_ = (float)(int)dh;
                    posV_ = (float)(int)dv;
                    CUR->setField217(1);
                    snd_at(sound_bb_, (float)(int)GU, (float)(int)GH,
                           -(float)(int)GV, 0);
                }
                if (((signed char)field_ff) == 0) {
                    copy8(&field_100, &now_);
                    field_ff = 1;
                    CUR->setField217(1);
                    snd_at(sound_b7_, (float)(int)GU, (float)(int)GH,
                           -(float)(int)GV, 0);
                }
                if (((signed char)field_ff) == 2 && K_HALF_SEC_MS <= now_ - field_100) {
                    field_ff = 0;
                    CUR->setField217(0);
                    pendingMove_ = field_108;
                }
            }

            /* --- attach to a moving platform (kind 0x0c) --- */
            if (((signed char)field_11e) == -1) {
                unsigned char *base = tileBase_;
                Tile *here = CUR;
                if ((signed char)here->objectMarker() == 0x0c) {
                    unsigned pu = here->slideOriginU();
                    unsigned pv = here->slideOriginV();
                    Tile *p = Tile::at(base, pu, pv);
                    if (fabsf((p->slidePosU() + (float)K_HALF_D) -
                              (posU_ + K_HALF_F)) <= (float)K_LINK_EPS &&
                        fabsf((p->slidePosV() + (float)K_HALF_D) -
                              (posV_ + K_HALF_F)) <= (float)K_LINK_EPS &&
                        (float)here->height() <= posY_) {
                        field_11e = here->slideSlot();
                    }
                }
            }

            /* --- conveyor (kind 0x15) --- */
            if ((signed char)CUR->objectMarker() == 0x15) {
                if (field_58 == 0) {
                    pendingMove_ = field_108;
                    field_125 = 0;
                    snd_at(sound_ab_, (float)(int)GU, (float)(int)GH,
                           -(float)(int)GV, 1);
                } else {
                    unsigned char d = (unsigned char)field_58;
                    pendingMove_ = d;
                    field_108 = d;
                    field_125 = 0;
                }
                field_9a = 3;
                field_58 = (unsigned)field_108;
            } else {
                field_58 = 0;
            }
        }

        Tile *t2 = CUR;
        if ((signed char)t2->objectMarker() != 0x15 ||
            (unsigned)t2->height() != (unsigned)(int)GH) {
            if (sound_ab_ != 0)
                CStatic_HaltPlayback(sound_ab_);
            field_58 = 0;
        }
    }

    /* ─── Ladder / lift tile (kind 9) ───────────────────────────────── */
    {
        Tile *t = CUR;
        if ((signed char)t->objectMarker() == 9 && (unsigned)t->height() == (unsigned)(int)GH)
            field_9b = 1;
        if (field_9b != 0 && ((signed char)moveState_) == 0) {
            heightCell_ = t->height();
            posY_ = CUR->liftLiveHeight();
        }
    }

    /* ─── Riding a platform ─────────────────────────────────────────── */
    if (((signed char)field_11e) != -1 && ((signed char)moveState_) == 0) {
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
            CUR->setField1a5(((signed char)kind_));
        if (facing_or_reverse((unsigned)pendingMove_, facing_)) {
            if (K_SNAP_EPS < posU_ - (float)(int)GU ||
                K_SNAP_EPS < posV_ - (float)(int)GV)
                pendingMove_ = 0;
            else
                field_11e = 0xff;
        }
    }

    if (field_120 != 0 && field_ea == 0) {
        pendingMove_ = 0;
        field_125 = 0;
    }

    /* ─── Ground contact, falling and landing ───────────────────────── */
    if (field_14e == 0 || field_120 != 0) {
        signed char restore = 0;
        Tile *t = CUR;

        if ((signed char)t->objectMarker() == 0 && (signed char)t->height() != 0) {
            restore = (signed char)t->height();
            t->setHeight(0);
        }
        if (((signed char)moveState_) == 0 && GH < 0)
            moveState_ = 2;                       /* fell below the floor */

        t = CUR;
        bool go_fall;
        if ((signed char)t->objectMarker() == 0 && GH > -100) {
            go_fall = true;
        } else {
            signed char h    = GH;
            unsigned char th = t->height();
            go_fall = ((int)th < (int)h) ||
                      (field_ea != 0 && field_14e != 0) ||
                      ((int)h < (int)th && th != 0 && h > -100);

            if (!go_fall && field_120 != 0) {
                /* ─── LANDED ─────────────────────────────────────── */
                if (((signed char)kind_) != 9) {
                    field_fb = 0;
                    if ((signed char)CUR->field1a5() != 0)
                        field_86 = 1;           /* landed on someone */
                    CUR->setField1a5(kind_);
                }
                if (field_ea == 0) {
                    signed char h2 = GH;
                    if ((signed char)CUR->objectMarker() == 0x0e ||
                        ((int)((unsigned)field_111 - (int)h2) < 3 && h2 > 1)) {
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
                field_ea  = 0;
                field_9a   = 0;
                field_120 = 0;
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
            if (field_120 == 0) {
                /* not falling yet: start the fall */
                copy8(&field_132, &field_66);
                field_fb = 0;
                if (sound_af_ != 0)
                    CStatic_HaltPlayback(sound_af_);
                if (field_9b == 0) {
                    field_5c   = -3.0f;          /* the bits 0xc0400000 */
                    field_111  = ((unsigned char)heightCell_);       /* fall start height */
                    field_120 = 1;
                    posY_    = (float)((unsigned char)heightCell_);
                    copy8(&field_112, &now_);
                    pendingMove_  = 0;
                }
            } else {
                if (((signed char)kind_) != 9)
                    CUR->setField1a5(0);

                unsigned elapsed = ftol32(now_ - field_112);
                double   ms      = (double)elapsed;

                if (field_ea == 0) {
                    /* --- free fall --- */
                    if (sound_c3_ != 0)
                        CStatic_Set3DPosition(sound_c3_, posU_, posY_,
                                              -posV_, 1);
                    if (facing_or_reverse((unsigned)pendingMove_, facing_))
                        pendingMove_ = 0;

                    double ts = ms * (double)K_MS_TO_S_F;
                    double h  = ((double)field_5c - ts * (double)fall_accel()) * ts
                                + (double)(unsigned)field_111;
                    posY_ = (float)h;
                    heightCell_ = (signed char)((unsigned char)(unsigned)(long long)h + 1);

                    if (field_ea == 0 &&
                        (int)((unsigned)field_111 - (int)GH) > 2 &&
                        ((signed char)field_e9) != 0) {
                        /* the paraglider opens: costs one charge */
                        field_e9 = (signed char)(((signed char)field_e9) - 1);
                        field_ea = 1;
                        snd_at(sound_a3_, posU_, posY_, -posV_, 0);
                        snd_at(sound_c7_, posU_, posY_, -posV_, 1);
                    }

                    int drop = (int)((unsigned)field_111 - (int)GH);
                    if (drop > 2 && field_ea == 0 && drop < 6 && ((signed char)kind_) != 9) {
                        field_9a = 8;
                        snd_at(sound_c3_, posU_, posY_, -posV_, 0);
                    }

                    if (field_ea != 0 ||
                        (K_GLIDE_DROP < (float)field_111 - (float)(int)GH &&
                         ((signed char)field_e9) != 0)) {
                        field_9a = 5;            /* LAB_00439a8f */
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
                        field_ea = 0;
                    field_9a = 5;
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
        if (field_120 != 0 && ((signed char)kind_) != 9)
            CUR->setField1a5(0);
    }

    /* ─── Idle: jump pad (kind 0x0e), then start a queued move ──────── */
    if (field_14e == 0) {
        Tile *t = CUR;
        if ((signed char)t->objectMarker() == 0x0e) {
            if ((unsigned)(int)GH == (unsigned)t->height())
                pendingMove_ = 0;

            if (field_11a == 0) {
                if ((unsigned)CUR->height() == (unsigned)(int)GH) {
                    snd_at(sound_b3_, (float)(int)GU, (float)(int)GH,
                           -(float)(int)GV, 0);
                    copy8(&field_112, &now_);        /* two dword copies */
                    field_11a = 1;
                    posY_ = (float)(int)GH;
                }
            } else {
                double ts = (now_ - field_112) * (double)K_MS_TO_S_F;
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
                    field_9a = 0x0b;
                } else {
                    if (((signed char)kind_) != 9)
                        c->setField1a5(0);
                    signed char nh = (signed char)CUR->field1f1();
                    field_11a = 0;
                    heightCell_   = nh;
                    posY_    = (float)(int)nh;
                    pendingMove_  = field_108;
                    field_9a   = 0x0c;
                }
            }
        }

        if (((signed char)moveState_) != 0)
            pendingMove_ = 0;

        if (field_14e == 0 && pendingMove_ != 0) {
            /* ─── Start the queued move ─────────────────────────── */
            field_141 = 0;
            field_14e = (unsigned)pendingMove_;
            pendingMove_ = 0;
            field_13f = 0;
            field_140 = 0;
            if (field_fb != 0)
                field_141 = 0xff;
            if (field_14e > 0x14)
                field_14e = field_14e - 0x14;   /* subtraction, not a modulo */
            if (field_14e == 1) field_140 = 0xff;
            if (field_14e == 2) field_13f = 1;
            if (field_14e == 3) field_140 = 1;
            if (field_14e == 4) field_13f = 0xff;

            Tile          *here    = CUR;
            unsigned char  h_here  = here->height();
            unsigned char  k_here  = here->objectMarker();
            Tile          *dest    = Tile::at(tileBase_,
                (int)field_13f + (int)GU, (int)GV + (int)field_140);
            unsigned char  h_dest  = dest->height();
            unsigned char  k_dest  = dest->objectMarker();
            field_12e = 0;

            if (field_ea != 0 && k_dest != 0 && (int)GH == (int)h_dest - 1)
                field_14e = 0;
            if (k_dest == 0x0f)
                field_ee = (unsigned char)field_14e;

            /* occupied destination blocks, with per-kind exceptions */
            if (facing_or_reverse(((unsigned)field_14e), facing_)) {
                signed char occ = (signed char)dest->field1a5();
                if (occ != 0) {
                    signed char me = ((signed char)kind_);
                    if (me == 3 || (me == 4 && occ == 3) || (me == 2 && occ != 4))
                        field_14e = 0;
                }
            }
            if (field_d3 != 0 && facing_or_reverse(((unsigned)field_14e), facing_))
                field_d3 = 0;

            if (k_dest == 0x10 && k_here == 0x10) {
                field_9a = 4;
            } else {
                if (k_here == 9 && facing_or_reverse(((unsigned)field_14e), facing_)) {
                    if ((float)K_RAMP_EPS < fabsf(posY_ - (float)(int)GH) ||
                        (int)GH == (int)h_dest - 1)
                        field_14e = 0;
                    else
                        field_9b = 0;
                }
                if (k_dest == 0x16 ||
                    (k_dest == 0x17 && dest->field217() == 0))
                    field_14e = 0;
            }

            if (field_14e != 0 && field_9a > 0xf9)
                field_9a = 0;

            /* ─── Ramp bookkeeping: which climb animation, and may we ── */
            if (Sim_CheckTileIsRamp(k_here) != 0) {
                if (Sim_CheckTileIsRamp(k_dest) == 0 && h_here == h_dest)
                    field_9a = 0x1b;
                if (((unsigned)field_14e) == (unsigned)(k_here - 4) ||
                    ((unsigned)field_14e) == (unsigned)Sim_GetTurnedDirection(
                                      (unsigned char)(k_here - 4), 2)) {
                    if (h_here < h_dest) {
                        field_141  = 1;
                        field_12e = 1;
                        if (((signed char)field_9a) == 0)
                            field_9a = (unsigned char)
                                ((-(Sim_CheckTileIsRamp(k_dest) != 0) & 0xfeU) + 0x1a);
                    }
                    if (h_dest == h_here) {
                        if (((signed char)field_9a) == 0)
                            field_9a = (unsigned char)
                                ((-(Sim_CheckTileIsRamp(k_dest) != 0) & 0xfeU) + 0x1b);
                        field_12e = 2;
                    }
                } else if ((unsigned)h_dest == (unsigned)h_here + 1) {
                    field_14e = 0;
                }
            }

            if (Sim_CheckTileIsRamp(k_dest) == 0) {
                if (field_ea == 0 && Sim_CheckTileIsRamp(k_here) == 0 &&
                    Sim_CheckTileIsRamp(k_dest) == 0) {
                    if (((signed char)field_9a) == 0) {
                        if (((unsigned)field_14e) == (unsigned)facing_)
                            field_9a = 0x14;
                        if (((unsigned)field_14e) ==
                            (unsigned)Sim_GetTurnedDirection(facing_, 2))
                            field_9a = 0x15;
                    }
                    if ((unsigned)h_dest == (unsigned)h_here + 1)
                        field_14e = 0;
                }
            } else {
                if (h_dest < h_here) {
                    if (((signed char)field_9a) == 0)
                        field_9a = (unsigned char)
                            ((-(Sim_CheckTileIsRamp(k_here) != 0) & 2U) + 0x17);
                    field_141  = 0xff;
                    field_12e = 2;
                }
                if (h_dest == h_here) {
                    if (((signed char)field_9a) == 0)
                        field_9a = (unsigned char)
                            ((-(Sim_CheckTileIsRamp(k_here) != 0) & 2U) + 0x16);
                    field_12e = 1;
                }
            }

            /* a second occupancy test, this one on the real step only */
            {
                signed char occ = (signed char)dest->field1a5();
                if (occ != 0 && (field_13f != 0 || field_140 != 0) &&
                    ((signed char)kind_) != 9) {
                    if (field_156 == 0) {
                        if (occ != 4)
                            field_14e = 0;
                    } else if (occ == 3) {
                        field_14e = 0;
                    }
                }
            }

            if (field_fb != 0 && k_dest != 0x10)
                field_9a = 0x14;

            if (field_14e != 0) {
                /* commit: pick the step's start time and move the grid cell */
                double since = now_ - field_50;
                if (since <= K_ZERO || field_48 <= since)
                    copy8(&field_146, &now_);
                else
                    copy8(&field_146, &field_50);

                if (((signed char)field_11e) == -1) {
                    if ((unsigned)field_108 != ((unsigned)field_14e)) {
                        posU_ = (float)(int)GU;
                        posV_ = (float)(int)GV;
                    }
                    int nu = (int)GU + (int)field_13f;
                    int nv = (int)GV + (int)field_140;
                    if (nu < 0 || (int)(unsigned)LevelMap::fromTileBase(tileBase_)->extentU() <= nu ||
                        nv < 0 || (int)(unsigned)LevelMap::fromTileBase(tileBase_)->extentV() <= nv) {
                        field_14e = 0;          /* off the edge of the map */
                    } else {
                        cellU_ = (signed char)(GU + field_13f);
                        cellV_ = (signed char)(GV + field_140);
                        heightCell_ = (signed char)(GH + field_141);
                    }
                }

                field_44 = (((signed char)field_125) == 3) ? 1 : 0;

                if (field_ea == 0) {
                    if (((signed char)field_125) == 2) field_9a = 0x1e;
                    if (((signed char)field_125) == 4) field_9a = 0x1f;
                } else {
                    field_9a = 5;
                }

                if (((signed char)kind_) != 9)
                    CUR->setField1a5(((signed char)kind_));

                field_6e = 0;
                copy8(&field_72, &now_);
            }
        }
    }

    /* ─── Height curves for the animation states ────────────────────── */
    field_40 = 0;
    if (((unsigned)field_14e) == 0) {
        if (field_48 <= now_ - field_50 && ((signed char)field_11e) == -1) {
            posU_ = (float)(int)GU;
            posV_ = (float)(int)GV;
        }
    } else if (facing_or_reverse(((unsigned)field_14e), facing_)) {
        double dur  = field_132 * K_TWO_MS;
        double inv  = K_ONE / dur;
        double tsec = (now_ - field_146) * K_MS_TO_S_D;
        double bias = K_ZERO;
        double frac = now_ - field_146;
        double gh   = (double)(int)GH;

        if (((signed char)kind_) == 9 && field_d8 == 0) {
            bias = tsec * inv - (inv / dur) * tsec * tsec * K_HALF_D;
            posY_ = (float)(gh + bias);
        }
        switch (((signed char)field_9a)) {
        case 0x16:
            posY_ = (float)(gh + (K_ONE / field_132) * frac * K_HALF_D + bias);
            break;
        case 0x17:
            posY_ = (float)((gh + bias + K_ONE)
                              - (K_ONE / field_132) * frac * K_HALF_D);
            break;
        case 0x1a:
            posY_ = (float)((gh + bias) - K_HALF_D
                              + (K_HALF_D / field_132) * frac);
            break;
        case 0x1b:
            posY_ = (float)(((gh + bias) - K_HALF_D + K_ONE)
                              - (K_ONE / field_132) * frac * K_HALF_D);
            break;
        case 0x18:
            posY_ = (float)((gh + bias) - K_HALF_D
                              + (K_ONE / field_132) * frac);
            break;
        case 0x19:
            posY_ = (float)((gh + bias + K_ONE_HALF)
                              - (K_ONE / field_132) * frac);
            break;
        default:
            break;
        }
    }

    /* ─── Interpolate the horizontal position across the step ───────── */
    {
        double frac = (now_ - field_146) / field_132;
        switch (field_14e - 1) {
        case 0: posV_ = (float)((double)(GV + 1) - frac); break;
        case 1: posU_ = (float)(frac + (double)(GU - 1)); break;
        case 2: posV_ = (float)(frac + (double)(GV - 1)); break;
        case 3: posU_ = (float)((double)(GU + 1) - frac); break;
        default: break;
        }
        if (field_fb != 0)                      /* climbing: interpolate H */
            posY_ = (float)((double)(GH + 1) - frac);
    }

    /* ─── Footstep / idle bookkeeping ───────────────────────────────── */
    if (field_44 != 0) {
        signed char a = ((signed char)field_9a);
        if (a == 0x19 || a == 0x17 || a == 0x1a || a == 0x16 || a == 0x18)
            field_40 = 1;
    }
    {
        unsigned char a = field_9a;
        if (a == 0x17) field_40 = 1;
        if (a == 4)    field_40 = 1;
        if (a < 0xfa && a != 0) {
            copy8(&field_72, &now_);
            field_6e = 0;
        }
    }

    /* ─── The idle timeout: 5 s standing still starts the idle anim ──
     *
     * The control flow is the original's, gotos and all: the 0xfa block is
     * reached either by falling out of the `anim == 0` branch or through
     * LAB_0043a90d, and is skipped entirely when +0x6e is clear. */
    bool run_idle;
    if (field_9a == 0) {
        double dv = now_ - field_72;
        if (dv < K_IDLE_MS) {
            run_idle = (field_6e != 0);          /* LAB_0043a90d */
        } else if (field_6e == 0) {
            copy8(&field_146, &now_);
            field_6e = 1;
            run_idle = true;                      /* LAB_0043a90d, now set */
        } else {
            run_idle = true;                      /* falls into the block */
        }
    } else {
        run_idle = (field_6e != 0);              /* LAB_0043a90d */
    }

    if (run_idle) {
        double dur = field_38;
        field_9a   = 0xfa;
        field_132   = dur;
        if (now_ - field_146 >= dur)
            copy8(&field_146, &now_);
    }

    return 0;
}

extern "C" __declspec(dllexport) unsigned int __attribute__((thiscall))
Sim_UpdateEntityMovement(MovableEntity *self)
{
    return self->updateMovement();
}
