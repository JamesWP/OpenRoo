/* MovableEntity -- the base of Bomb, Foe and Player (see movableentity.h).
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
