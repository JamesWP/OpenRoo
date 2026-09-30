/* LinkedList: an intrusive doubly-linked list of opaque values, with a
 * virtual destructor (one vtable slot).  It is embedded in many
 * objects, so its 16 bytes and its node's 12 are fixed.  pValue is compared as
 * a raw word and never dereferenced, so small integer codes (the Player's
 * timed-effect codes) are stored in it directly. */

#pragma once
#include <cstddef>
 

class LinkedList;

class LinkedListNode {
public:
     

    void           *value() const { return pValue; }
    LinkedListNode *next() const  { return pNextNode; }
    LinkedListNode *prev() const  { return pPrevNode; }

private:
    friend class LinkedList;

    void           *pValue;
    LinkedListNode *pNextNode;
    LinkedListNode *pPrevNode;

     
};

 
class LinkedList {
public:
     

    /* An empty list.  The destructor frees the nodes, not their values. */
    LinkedList();
    virtual ~LinkedList();
    LinkedList(const LinkedList &) = delete;
    LinkedList &operator=(const LinkedList &) = delete;

    /* Appends a new node holding pValue at the tail. */
    void append(void *pValue);
    /* Frees the nodes, not their values. */
    void clear();
    /* Unlinks and frees pNode and decrements the count.  A NULL pNode does
     * nothing.  Always returns 0. */
    int unlink(LinkedListNode *pNode);
    /* The first node after pAfterNode (NULL: from the head) whose value is
     * pValue, or NULL. */
    LinkedListNode *find(void *pValue, LinkedListNode *pAfterNode = NULL);

    LinkedListNode *head() const  { return pHead; }
    LinkedListNode *tail() const  { return pTail; }
    unsigned long   count() const { return dwCount; }

    /* Returns (*it)'s value and advances *it to the next node. */
    static void *nextValue(LinkedListNode **it)
    {
        LinkedListNode *n = *it;
        *it = n->pNextNode;
        return n->pValue;
    }

private:
    LinkedListNode  *pHead;    // +0x04
    LinkedListNode  *pTail;    // +0x08
    unsigned long    dwCount;  // +0x0c

     
};
