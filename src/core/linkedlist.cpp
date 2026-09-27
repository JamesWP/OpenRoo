/* LinkedList (linkedlist.h).
 *
 * PRESERVED, all harmless as the game uses the list:
 *   - Append stores through the new node without checking it, so an
 *     exhausted heap faults inside Append;
 *   - Unlink repairs head and tail from the node's own links and never checks
 *     that the node is in this list: a foreign node corrupts both lists;
 *   - Unlink always returns 0, and no caller reads it;
 *   - Destruct re-installs the vtable before it clears. */

#include "linkedlist.h"
#include <stdlib.h>  // free() of the list object
#include <stddef.h>
#include <stdlib.h>
#include <windows.h>
#include "log.h"

/* KAROO_LIST_FX, a negative control (CONTROLS.md):
 *   lifo  Append links the new node at the head instead of the tail, so every
 *         list iterates in reverse.  A direction change, so it proves Append's
 *         links, not just that Append ran.  It moves no coordinate or tile
 *         index, so it cannot reach the unbounded spawn scans that crash
 *         levelreport.py. */
enum ListFx { LIST_FX_OFF = 0, LIST_FX_LIFO = 1 };

/* KAROO_LIST_DIAG, a census: counts each of the seven functions and logs the
 * tally, so "the suite never calls Unlink" can be told from "the suite calls
 * Unlink and cannot see it". */
static bool list_diag(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[32];
        DWORD n = GetEnvironmentVariableA("KAROO_LIST_DIAG", buf, sizeof(buf));
        cached = (n > 0 && n < sizeof(buf) && lstrcmpiA(buf, "0") != 0) ? 1 : 0;
    }
    return cached != 0;
}

static unsigned long g_nInit, g_nScalarDtor, g_nDestruct, g_nAppend;
static unsigned long g_nClear, g_nUnlink, g_nUnlinkNull, g_nFind, g_nFindHit;

/* The census samples only at Append thresholds, so a function first reached
 * after the last one would read as zero; each function also logs its own first
 * call. */
static void list_first(const char *fn, unsigned long *pSeen)
{
    if (!list_diag() || *pSeen != 0)
        return;
    *pSeen = 1;
    log_write("linkedlist: first call to %s\n", fn);
}

static void list_census(void)
{
    if (!list_diag())
        return;
    // 1, 100, 1000, 10000, then every 20000: a short run still reports, a long
    // one does not flood.
    unsigned long n = g_nAppend;
    if (!(n == 1 || n == 100 || n == 1000 || n == 10000 || n % 20000 == 0))
        return;
    log_write("linkedlist: census init=%lu sdtor=%lu destruct=%lu append=%lu "
              "clear=%lu unlink=%lu (null %lu) find=%lu (hit %lu)\n",
              g_nInit, g_nScalarDtor, g_nDestruct, g_nAppend,
              g_nClear, g_nUnlink, g_nUnlinkNull, g_nFind, g_nFindHit);
}

static ListFx list_fx(void)
{
    static int cached = -1;
    if (cached >= 0)
        return (ListFx)cached;
    char buf[32];
    DWORD n = GetEnvironmentVariableA("KAROO_LIST_FX", buf, sizeof(buf));
    ListFx fx = LIST_FX_OFF;
    if (n > 0 && n < sizeof(buf)) {
        if (lstrcmpiA(buf, "lifo") == 0) fx = LIST_FX_LIFO;
    }
    log_write("linkedlist: FX mode = %s\n", fx == LIST_FX_LIFO ? "lifo" : "off");
    cached = (int)fx;
    return fx;
}


/* The vtable below needs its address. */
LinkedList *List_ScalarDestructor(LinkedList *self, unsigned char bFreeSelf);

/* The one-slot vtable. */
static void *const g_ListVtable[1] = { (void *)&List_ScalarDestructor };

void List_Init(LinkedList *self)
{
    ++g_nInit;
    { static unsigned long seen; list_first("Init", &seen); }
    self->vtable  = (void **)g_ListVtable;
    self->pHead   = NULL;
    self->pTail   = NULL;
    self->dwCount = 0;
}

void List_Clear(LinkedList *self)
{
    ++g_nClear;
    { static unsigned long seen; list_first("Clear", &seen); }
    LinkedListNode *p = self->pHead;
    while (p != NULL) {
        LinkedListNode *next = p->pNextNode;
        free(p);
        p = next;
    }
    self->pHead   = NULL;
    self->pTail   = NULL;
    self->dwCount = 0;
}

void List_Destruct(LinkedList *self)
{
    ++g_nDestruct;
    { static unsigned long seen; list_first("Destruct", &seen); }
    self->vtable = (void **)g_ListVtable;
    List_Clear(self);
}

LinkedList *List_ScalarDestructor(LinkedList *self, unsigned char bFreeSelf)
{
    ++g_nScalarDtor;
    { static unsigned long seen; list_first("ScalarDestructor", &seen); }
    List_Destruct(self);
    if ((bFreeSelf & 1) != 0)
        // No list is allocated on its own today; free() matches the malloc the
        // rest of the code uses.
        free(self);
    return self;
}

void List_Append(LinkedList *self, void *pValue)
{
    ++g_nAppend;
    { static unsigned long seen; list_first("Append", &seen); }
    list_census();

    // malloc, not new.  The nothrow new costs about 7x across the suite (a
    // try/catch per node on a hot path), and the throwing new would unwind
    // where the game faults.  malloc returns NULL, and Append then faults on
    // it, as PRESERVED above.
    LinkedListNode *node = (LinkedListNode *)malloc(sizeof(LinkedListNode));
    node->pValue    = pValue;
    node->pNextNode = NULL;
    node->pPrevNode = NULL;

    if (list_fx() == LIST_FX_LIFO && self->pHead != NULL) {
        // The lifo control: link at the head.
        node->pNextNode        = self->pHead;
        self->pHead->pPrevNode = node;
        self->pHead            = node;
        self->dwCount          = self->dwCount + 1;
        return;
    }

    if (self->pTail != NULL) {
        self->pTail->pNextNode = node;
        node->pPrevNode        = self->pTail;
        self->pTail            = node;
    } else {
        self->pHead = node;
        self->pTail = node;
    }
    self->dwCount = self->dwCount + 1;
}

int List_Unlink(LinkedList *self, LinkedListNode *pNode)
{
    ++g_nUnlink;
    { static unsigned long seen; list_first("Unlink", &seen); }
    if (pNode == NULL)
        ++g_nUnlinkNull;
    if (pNode != NULL) {
        if (pNode->pPrevNode == NULL)
            self->pHead = pNode->pNextNode;
        else
            pNode->pPrevNode->pNextNode = pNode->pNextNode;

        if (pNode->pNextNode == NULL)
            self->pTail = pNode->pPrevNode;
        else
            pNode->pNextNode->pPrevNode = pNode->pPrevNode;

        free(pNode);
        self->dwCount = self->dwCount - 1;
    }
    return 0;
}

LinkedListNode *
List_Find(LinkedList *self, void *pValue, LinkedListNode *pAfterNode)
{
    LinkedListNode *p = (pAfterNode != NULL) ? pAfterNode->pNextNode
                                             : self->pHead;
    ++g_nFind;
    { static unsigned long seen; list_first("Find", &seen); }
    while (p != NULL) {
        if (p->pValue == pValue) {
            ++g_nFindHit;
            return p;
        }
        p = p->pNextNode;
    }
    return NULL;
}


LinkedListNode *List_GetHead(LinkedList *self)
{
    return self->pHead;
}

void *List_NextValue(LinkedList * , LinkedListNode **it)
{
    LinkedListNode *n = *it;
    *it = n->pNextNode;
    return n->pValue;
}
