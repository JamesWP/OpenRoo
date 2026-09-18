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

struct LinkedListNode {
    void           *pValue;    // +0x00
    LinkedListNode *pNextNode; // +0x04
    LinkedListNode *pPrevNode; // +0x08
};
static_assert(sizeof(LinkedListNode) == 12, "LinkedListNode size mismatch");

struct LinkedList {
    void           **vtable;   // +0x00
    LinkedListNode  *pHead;    // +0x04
    LinkedListNode  *pTail;    // +0x08
    unsigned long    dwCount;  // +0x0c
};
static_assert(sizeof(LinkedList) == 16, "LinkedList size mismatch");

/* The game's one-slot vtable at 0x0045d460; slot 0 is the scalar deleting
 * destructor.  Our Init and Destruct install this same address, because game
 * code still holds these objects and may compare the pointer. */
#define LINKEDLIST_VTABLE 0x0045d460

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

/* Names the rest of karoo-hooks/ already uses.  Kept as thin aliases so this
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
