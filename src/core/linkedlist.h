/* LinkedList / LinkedListNode -- the game's intrusive list (Ghidra structs,
 * 16 and 12 bytes), and the four methods our code calls on it.
 *
 * The methods are NAMED CALLBACKS into the game binary and stay that way:
 * LinkedList is a generic container with 43 call sites across movie
 * playback, level parsing, sound and D3D mode enumeration, one reference is
 * an E9 tail-jump that CALL_PATCHES cannot rewrite, and CLAUDE.md says not
 * to stub shared helpers that remain live for other callers.  Every caller
 * in karoo-hooks goes through these four wrappers rather than defining its
 * own function-pointer macro.
 *
 * pValue is compared as a raw word, so small integer codes (the Player's
 * timed-effect codes) are passed straight through it.
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

namespace linkedlist_detail {
typedef void (__attribute__((thiscall)) *append_fn)(LinkedList *self, void *pValue);
typedef void (__attribute__((thiscall)) *clear_fn) (LinkedList *self);
typedef int  (__attribute__((thiscall)) *unlink_fn)(LinkedList *self, LinkedListNode *pNode);
typedef LinkedListNode *(__attribute__((thiscall)) *find_fn)(LinkedList *self, void *pValue,
                                                            LinkedListNode *pAfterNode);
}

/* LinkedList::Append 0x004254a0 */
static inline void LinkedList_Append(LinkedList *self, void *pValue)
{
    ((linkedlist_detail::append_fn)0x004254a0)(self, pValue);
}

/* LinkedList::Clear 0x004254f0 -- frees the nodes, not their pValues. */
static inline void LinkedList_Clear(LinkedList *self)
{
    ((linkedlist_detail::clear_fn)0x004254f0)(self);
}

/* LinkedList::UnlinkAndFreeListNode 0x00425530 */
static inline int LinkedList_Unlink(LinkedList *self, LinkedListNode *pNode)
{
    return ((linkedlist_detail::unlink_fn)0x00425530)(self, pNode);
}

/* LinkedList::FindListNodeByValue 0x00425580 -- first node after
 * pAfterNode (NULL = from the head) whose pValue equals pValue, or NULL. */
static inline LinkedListNode *LinkedList_Find(LinkedList *self, void *pValue,
                                              LinkedListNode *pAfterNode)
{
    return ((linkedlist_detail::find_fn)0x00425580)(self, pValue, pAfterNode);
}
