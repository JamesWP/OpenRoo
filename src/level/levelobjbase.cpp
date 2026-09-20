/* LevelObjectBase's two destructors -- see levelobjbase.h for the TU, the
 * vtable argument and why these are replaced rather than dead.
 *
 * ─── From the listings ───────────────────────────────────────────────────
 *
 *   00401040  PUSH ESI; MOV ESI,ECX; CALL 0x401060; TEST byte [ESP+8],1;
 *             JZ ..; PUSH ESI; CALL FactAlloc::Free2; ADD ESP,4;
 *             MOV EAX,ESI; POP ESI; RET 4
 *   00401060  MOV [ECX],0x45d290; RET
 *
 * Both are reproduced exactly, including 0x401060 not setting EAX -- the
 * declaration returns void rather than inventing a value nobody reads.
 */
#include <windows.h>
#include "levelobjbase.h"
#include "alloc.h"

extern "C" {

static void *const g_LevelObjBaseVtable[1] = { (void *)&LevelObjBase_ScalarDtor };

__declspec(dllexport) void *LevelObjBase_Vtable(void)
{
    return (void *)g_LevelObjBaseVtable;
}

__declspec(dllexport) void __attribute__((thiscall))
LevelObjBase_DtorBody(void *self)
{
    *(void **)self = LevelObjBase_Vtable();
}

/* The free is the GAME heap's: every level object was allocated by the
 * game's `operator new`, so the other side of the lifetime is still theirs
 * (alloc.h's rule).  It retires with those allocations.
 *
 * Unverified by test, and said plainly: nothing calls it, our table's slot 0
 * is the only way in, and no live object carries that table for longer than
 * the straight-line stores in MovableEntity's base ctor and dtor.  Same
 * position as LinkedList::ScalarDestructor. */
__declspec(dllexport) void *__attribute__((thiscall))
LevelObjBase_ScalarDtor(void *self, unsigned int flags)
{
    LevelObjBase_DtorBody(self);
    if (flags & 1)
        game_free2(self);
    return self;
}

} // extern "C"
