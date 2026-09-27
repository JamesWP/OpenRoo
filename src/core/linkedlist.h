/* LinkedList: an intrusive doubly-linked list of opaque values, with a
 * one-slot vtable (the scalar deleting destructor).  It is embedded in many
 * objects, so its 16 bytes and its node's 12 are fixed.  pValue is compared as
 * a raw word and never dereferenced, so small integer codes (the Player's
 * timed-effect codes) are stored in it directly. */

#pragma once

#include "layout.h"

struct __attribute__((packed)) LinkedListNode {
    static const int ORIGIN = 0;

    void           *pValue;     // +0x00
    LinkedListNode *pNextNode;  // +0x04
    LinkedListNode *pPrevNode;  // +0x08

private:
    KAROO_LAYOUT_REGISTER(LinkedListNode);
};

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

/* Every embedder tiles around 16 bytes; doublesoundbuff's two lists sit at
 * +0x38 and +0x48. */
KAROO_LAYOUT_CHECKS(LinkedList)
{
    KAROO_LAYOUT_AT(vtable,  0x00);
    KAROO_LAYOUT_AT(pHead,   0x04);
    KAROO_LAYOUT_AT(pTail,   0x08);
    KAROO_LAYOUT_AT(dwCount, 0x0c);
    KAROO_LAYOUT_SIZE(16);
}

/* Sets the vtable and zeroes the three fields. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
List_Init(LinkedList *self);

/* Destruct, then frees self when bit 0 of flags is set.  Returns self. */
extern "C" __declspec(dllexport) LinkedList *__attribute__((thiscall))
List_ScalarDestructor(LinkedList *self, unsigned char bFreeSelf);

/* Re-installs the vtable, then Clear. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
List_Destruct(LinkedList *self);

/* Appends a new node holding pValue at the tail. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
List_Append(LinkedList *self, void *pValue);

/* Frees the nodes, not their values. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
List_Clear(LinkedList *self);

/* Unlinks and frees pNode and decrements the count.  A NULL pNode does
 * nothing.  Always returns 0. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
List_Unlink(LinkedList *self, LinkedListNode *pNode);

/* The first node after pAfterNode (NULL: from the head) whose value is pValue,
 * or NULL. */
extern "C" __declspec(dllexport) LinkedListNode *__attribute__((thiscall))
List_Find(LinkedList *self, void *pValue, LinkedListNode *pAfterNode);

/* Shorter names for the calls above. */
static inline void LinkedList_Append(LinkedList *self, void *pValue)
{ List_Append(self, pValue); }

static inline void LinkedList_Clear(LinkedList *self)
{ List_Clear(self); }

static inline int LinkedList_Unlink(LinkedList *self, LinkedListNode *pNode)
{ return List_Unlink(self, pNode); }

static inline LinkedListNode *LinkedList_Find(LinkedList *self, void *pValue,
                                              LinkedListNode *pAfterNode)
{ return List_Find(self, pValue, pAfterNode); }

/* The head node. */
extern "C" __declspec(dllexport) LinkedListNode *__attribute__((thiscall))
List_GetHead(LinkedList *self);

/* Returns (*it)'s value and advances *it to the next node; the list itself is
 * not read. */
extern "C" __declspec(dllexport) void *__attribute__((thiscall))
List_NextValue(LinkedList *self, LinkedListNode **it);
