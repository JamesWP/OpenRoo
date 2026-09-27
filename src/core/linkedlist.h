/* LinkedList: an intrusive doubly-linked list of opaque values, with a
 * one-slot vtable (the scalar deleting destructor).  It is embedded in many
 * objects, so its 16 bytes and its node's 12 are fixed.  pValue is compared as
 * a raw word and never dereferenced, so small integer codes (the Player's
 * timed-effect codes) are stored in it directly. */

#pragma once

struct LinkedListNode {

    void           *pValue;
    LinkedListNode *pNextNode;
    LinkedListNode *pPrevNode;

private:
};

struct LinkedList {

    void           **vtable;
    LinkedListNode  *pHead;
    LinkedListNode  *pTail;
    unsigned long    dwCount;

private:
};

/* Sets the vtable and zeroes the three fields. */
void List_Init(LinkedList *self);

/* Destruct, then frees self when bit 0 of flags is set.  Returns self. */
LinkedList *List_ScalarDestructor(LinkedList *self, unsigned char bFreeSelf);

/* Re-installs the vtable, then Clear. */
void List_Destruct(LinkedList *self);

/* Appends a new node holding pValue at the tail. */
void List_Append(LinkedList *self, void *pValue);

/* Frees the nodes, not their values. */
void List_Clear(LinkedList *self);

/* Unlinks and frees pNode and decrements the count.  A NULL pNode does
 * nothing.  Always returns 0. */
int List_Unlink(LinkedList *self, LinkedListNode *pNode);

/* The first node after pAfterNode (NULL: from the head) whose value is pValue,
 * or NULL. */
LinkedListNode *
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
LinkedListNode *List_GetHead(LinkedList *self);

/* Returns (*it)'s value and advances *it to the next node; the list itself is
 * not read. */
void *List_NextValue(LinkedList *self, LinkedListNode **it);
