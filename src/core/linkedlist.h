/* LinkedList / LinkedListNode -- the game's intrusive doubly-linked list
 * (Ghidra structs, 16 and 12 bytes), reimplemented in full.
 *
 * ── Why these stopped being callbacks (ENDGAME_PLAN.md E1) ────────────────
 *
 * This header used to say the four methods were NAMED CALLBACKS and would
 * stay that way, on two grounds: 43 call sites across unrelated subsystems,
 * and "one reference is an E9 tail-jump that CALL_PATCHES cannot rewrite".
 *
 * The second ground dissolved once the whole class came into view rather than
 * the four methods our code happened to call.  The lone E9 is at 0x00425496,
 * inside `Destruct` (0x00425490), which sets the vtable pointer and tail-jumps
 * into `Clear` -- it is *internal to LinkedList*, not an external caller, so
 * replacing the class removes it rather than having to rewrite it.  (patch.py
 * has JMP_PATCHES now in any case; it is not needed here.)
 *
 * The first ground was never an objection to replacing, only to stubbing a
 * shared helper while game callers remain.  All 44 references are redirected
 * together, so no caller is left reaching a UD2:
 *
 *   Init      13    Append 18    Clear                12
 *   Destruct  29    Find    7    UnlinkAndFreeListNode  7
 *   ScalarDestructor 1 -- and that one is the vtable slot, not a code
 *   reference: xref.py reports *no* CALL or JMP to 0x00425470 at all, so an
 *   E8/E9 scan alone would have left the game's vtable pointing at a UD2.
 *
 * Destruct's 29 break down as 11 CALL and 18 E9 -- three compiler thunks and
 * fifteen SEH unwind funclets -- so the E9 problem was in fact much larger
 * than the "one reference" this header used to claim, just not an obstacle:
 * JMP_PATCHES rewrites tail-jumps the same way CALL_PATCHES rewrites calls.
 *
 * ── The nodes stay on the game heap, deliberately ─────────────────────────
 *
 * `Append` allocates each node with the game's `operator new` and `Clear` /
 * `UnlinkAndFreeListNode` free it with `FactAlloc::Free2`.  All three are ours
 * now, which by COHESION_PLAN template 6 would allow plain new/delete -- but
 * that rule needs *both* sides proven ours, and "no game code anywhere frees a
 * 12-byte list node" is not something the xref of a generic allocator can
 * show.  ENDGAME_PLAN is explicit that the switch happens in the cycle that
 * proves the other side, never ahead of it, because a mismatched free is heap
 * corruption rather than a test failure.  So the nodes keep using alloc.h and
 * this is listed as an open follow-up, not an oversight.
 *
 * pValue is compared as a raw word, never dereferenced, so small integer codes
 * (the Player's timed-effect codes 8, 0xa, 0xb, 0xc, 0xd) pass straight
 * through it.
 */
#pragma once

#include "layout.h"

struct __attribute__((packed)) LinkedListNode {
    static const int ORIGIN = 0;

    void           *pValue;    // +0x00
    LinkedListNode *pNextNode; // +0x04
    LinkedListNode *pPrevNode; // +0x08

private:
    KAROO_LAYOUT_REGISTER(LinkedListNode);
};

/* Size matters here: Append allocates a node with the game's operator new,
 * which asks for 12. */
KAROO_LAYOUT_CHECKS(LinkedListNode)
{
    KAROO_LAYOUT_AT(pValue,    0x00);
    KAROO_LAYOUT_AT(pNextNode, 0x04);
    KAROO_LAYOUT_AT(pPrevNode, 0x08);
    KAROO_LAYOUT_SIZE(12);
}

struct __attribute__((packed)) LinkedList {
    static const int ORIGIN = 0;

    void           **vtable;   // +0x00
    LinkedListNode  *pHead;    // +0x04
    LinkedListNode  *pTail;    // +0x08
    unsigned long    dwCount;  // +0x0c

private:
    KAROO_LAYOUT_REGISTER(LinkedList);
};

/* 16 is what every embedder tiles around -- doublesoundbuff's two lists sit
 * at +0x38 and +0x48. */
KAROO_LAYOUT_CHECKS(LinkedList)
{
    KAROO_LAYOUT_AT(vtable,  0x00);
    KAROO_LAYOUT_AT(pHead,   0x04);
    KAROO_LAYOUT_AT(pTail,   0x08);
    KAROO_LAYOUT_AT(dwCount, 0x0c);
    KAROO_LAYOUT_SIZE(16);
}

/* The vtable is OURS.  The game does not require the table address to stay
 * stable, so `Init` and `Destruct` install a one-slot table defined in
 * linkedlist.cpp rather than writing the game's 0x0045d460 back into the
 * object.  Slot 0 is the scalar deleting destructor, and it is ours, so the
 * table needs nothing from the game at all.
 *
 * That leaves the game's table at 0x0045d460 still holding the original
 * 0x00425470 -- which is UD2-stubbed.  Deliberately: it is now a tripwire.
 * Nothing should ever read that table again, and if something does it faults
 * as c000001d rather than quietly working, which is the positive proof
 * CLAUDE.md asks a stub to provide.  There is correspondingly no
 * VTABLE_PATCHES entry for LinkedList.
 *
 * (A partially-replaced class could not do this so cheaply: its own table
 * would have to carry the game's pointers for the slots still theirs.
 * LinkedList's table is one slot and that slot is ours.) */

/* 0x00425450 Init -- vtable + three zeroed fields.  RET 0. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
List_Init(LinkedList *self);

/* 0x00425470 ScalarDestructor -- vtable[0].  Destruct, then free `self`
 * itself when bit 0 of the flag is set.  Returns `self`.  RET 4. */
extern "C" __declspec(dllexport) LinkedList *__attribute__((thiscall))
List_ScalarDestructor(LinkedList *self, unsigned char bFreeSelf);

/* 0x00425490 Destruct -- re-install the vtable, then Clear.  RET 0. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
List_Destruct(LinkedList *self);

/* 0x004254a0 Append -- new node at the tail.  RET 4. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
List_Append(LinkedList *self, void *pValue);

/* 0x004254f0 Clear -- frees the nodes, not their pValues.  RET 0. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
List_Clear(LinkedList *self);

/* 0x00425530 UnlinkAndFreeListNode -- unlink and free pNode, decrementing
 * dwCount.  A NULL pNode is a no-op.  Always returns 0.  RET 4. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
List_Unlink(LinkedList *self, LinkedListNode *pNode);

/* 0x00425580 FindListNodeByValue -- the first node after pAfterNode (NULL =
 * from the head) whose pValue equals pValue, or NULL.  RET 8. */
extern "C" __declspec(dllexport) LinkedListNode *__attribute__((thiscall))
List_Find(LinkedList *self, void *pValue, LinkedListNode *pAfterNode);

/* Names the rest of src/ already uses.  Kept as thin aliases so this
 * cycle does not also churn direct3d.cpp and player.cpp; they now resolve to
 * our own code rather than to an absolute address in the game binary. */
static inline void LinkedList_Append(LinkedList *self, void *pValue)
{ List_Append(self, pValue); }

static inline void LinkedList_Clear(LinkedList *self)
{ List_Clear(self); }

static inline int LinkedList_Unlink(LinkedList *self, LinkedListNode *pNode)
{ return List_Unlink(self, pNode); }

static inline LinkedListNode *LinkedList_Find(LinkedList *self, void *pValue,
                                              LinkedListNode *pAfterNode)
{ return List_Find(self, pValue, pAfterNode); }

/* 0x0042ce00 GetHead -- returns pHead.  RET 0.  One E8, RenderGameFrame. */
extern "C" __declspec(dllexport) LinkedListNode *__attribute__((thiscall))
List_GetHead(LinkedList *self);

/* 0x0042ce10 NextValue -- thiscall(list, LinkedListNode **it), RET 4: returns
 * (*it)->pValue and advances *it to the next node; the list is not read.
 * One E8, RenderGameFrame. */
extern "C" __declspec(dllexport) void *__attribute__((thiscall))
List_NextValue(LinkedList *self, LinkedListNode **it);
