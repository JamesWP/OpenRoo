/* LinkedList reimplementation -- the whole class, seven functions.
 *
 *   0x00425450 Init                   __thiscall(this)                  RET 0
 *   0x00425470 ScalarDestructor       __thiscall(this, byte) -> this    RET 4
 *   0x00425490 Destruct               __thiscall(this)                  RET 0
 *   0x004254a0 Append                 __thiscall(this, pValue)          RET 4
 *   0x004254f0 Clear                  __thiscall(this)                  RET 0
 *   0x00425530 UnlinkAndFreeListNode  __thiscall(this, pNode) -> 0      RET 4
 *   0x00425580 FindListNodeByValue    __thiscall(this, v, after)        RET 8
 *
 * Every signature was read off the original's `RET n` rather than taken from
 * the decompiler's parameter list -- scenelight.cpp records why that
 * distinction is worth the extra minute.  Note Find's argument order on the
 * stack: [ESP+4] is pValue and [ESP+8] is pAfterNode.
 *
 * Bugs and quirks preserved deliberately:
 *
 *   - Append does NOT null-check `operator new`.  0x004254a5 calls it and
 *     0x004254b0 stores through the result unconditionally, so an exhausted
 *     heap faults inside Append.  Reproduced: we store through `node`
 *     without checking it either.
 *   - UnlinkAndFreeListNode repairs head/tail from the node's own links and
 *     never verifies the node belongs to *this* list.  Passing a foreign node
 *     corrupts both lists; that is the original's contract.
 *   - It always returns 0, whatever happened, and every caller ignores it.
 *   - Find compares pValue as a raw word and never dereferences it.
 *   - Destruct re-installs the vtable *before* clearing, which matters only
 *     if a node's destruction could re-enter -- it cannot, but the order is
 *     the original's and is kept.
 *
 * The nodes stay on the game heap; linkedlist.h explains why that is a
 * deliberate hold rather than an oversight.
 */
#include "linkedlist.h"
#include "alloc.h"        /* still needed: the object itself, not its nodes */
#include <stddef.h>
#include <stdlib.h>
#include <windows.h>
#include "log.h"

/* ─── KAROO_LIST_FX — the negative control (CONTROLS.md) ──────────────────
 *
 * Read by value, never by presence: GetEnvironmentVariableA returns 0 for
 * both unset and empty, which is what we want.
 *
 *   lifo  — Append links the new node at the *head* instead of the tail.
 *           A direction change, not a value perturbation: every list in the
 *           game iterates in the opposite order, and only this function can
 *           produce that.  It proves Append's pointer arithmetic, not merely
 *           that Append ran.
 *
 * Blast radius, chosen against the gate that hosts it: this reorders list
 * traversal but moves no coordinate, axis or tile index, so it cannot reach
 * the unbounded bridge/slide spawn scans that crash levelreport.py.
 */
enum ListFx { LIST_FX_OFF = 0, LIST_FX_LIFO = 1 };

/* ─── KAROO_LIST_DIAG — the census ────────────────────────────────────────
 *
 * The lifo control above passes the suite 16/16, so it is either dead or
 * live-but-unobserved.  The FX log line already shows Append runs, which
 * settles *that* -- but not how much of the class the gates reach.  This
 * counts each of the seven and dumps the tally at exit, so "the suite never
 * calls Unlink" can be told from "the suite calls Unlink and cannot see it".
 */
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

/* The periodic census samples only at Append thresholds, so a function first
 * reached after the last threshold would read as zero forever -- which is the
 * exact mistake this census exists to avoid.  Each function therefore also
 * announces its own first call. */
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
    /* 1 / 100 / 1000 / 10000 then every 20000, the house pattern: a short
     * run still reports, a long one does not flood. */
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

extern "C" {

/* Forward declaration: the vtable below needs its address. */
__declspec(dllexport) LinkedList *__attribute__((thiscall))
List_ScalarDestructor(LinkedList *self, unsigned char bFreeSelf);

/* Our own one-slot vtable -- see linkedlist.h for why it is ours and why the
 * game's table at 0x0045d460 is left pointing at a UD2. */
static void *const g_ListVtable[1] = { (void *)&List_ScalarDestructor };

__declspec(dllexport) void __attribute__((thiscall))
List_Init(LinkedList *self)
{
    ++g_nInit;
    { static unsigned long seen; list_first("Init", &seen); }
    self->vtable  = (void **)g_ListVtable;
    self->pHead   = NULL;
    self->pTail   = NULL;
    self->dwCount = 0;
}

__declspec(dllexport) void __attribute__((thiscall))
List_Clear(LinkedList *self)
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

__declspec(dllexport) void __attribute__((thiscall))
List_Destruct(LinkedList *self)
{
    ++g_nDestruct;
    { static unsigned long seen; list_first("Destruct", &seen); }
    self->vtable = (void **)g_ListVtable;
    List_Clear(self);
}

__declspec(dllexport) LinkedList *__attribute__((thiscall))
List_ScalarDestructor(LinkedList *self, unsigned char bFreeSelf)
{
    ++g_nScalarDtor;
    { static unsigned long seen; list_first("ScalarDestructor", &seen); }
    List_Destruct(self);
    if ((bFreeSelf & 1) != 0)
        /* NOT `delete`.  The nodes above are ours end to end, but the
         * LinkedList *object* is not: whatever allocated it with bFreeSelf
         * set did so on the game's heap, and those allocators are still
         * game code.  Freeing it with our `delete` would be a mismatched
         * free -- heap corruption, not a test failure. */
        game_free2(self);
    return self;
}

__declspec(dllexport) void __attribute__((thiscall))
List_Append(LinkedList *self, void *pValue)
{
    ++g_nAppend;
    { static unsigned long seen; list_first("Append", &seen); }
    list_census();

    /* Ours on both sides now, so our own heap rather than alloc.h -- but
     * malloc/free, not new/delete, and the reason is measured rather than
     * stylistic:
     *
     *   new (std::nothrow)   ~67 s per recording
     *   new (throwing)       ~8 s
     *   malloc               ~8 s      (baseline is ~8 s)
     *
     * libstdc++ implements the nothrow form as a try/catch around the
     * throwing one, and a try/catch per node on a path this hot costs ~7x
     * across the whole suite.  Plain `new` is fast but throws on exhaustion,
     * and the original returned NULL and then stored through it -- so an
     * out-of-memory Append FAULTED, and bit-exactness (CLAUDE.md) means
     * reproducing that rather than unwinding a bad_alloc out through game
     * frames.  malloc gives both: NULL on failure, no exception machinery. */
    LinkedListNode *node = (LinkedListNode *)malloc(sizeof(LinkedListNode));
    node->pValue    = pValue;
    node->pNextNode = NULL;
    node->pPrevNode = NULL;

    if (list_fx() == LIST_FX_LIFO && self->pHead != NULL) {
        /* Negative control only -- link at the head instead. */
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

__declspec(dllexport) int __attribute__((thiscall))
List_Unlink(LinkedList *self, LinkedListNode *pNode)
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

__declspec(dllexport) LinkedListNode *__attribute__((thiscall))
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

} // extern "C"

extern "C" __declspec(dllexport) LinkedListNode *__attribute__((thiscall))
List_GetHead(LinkedList *self)
{
    return self->pHead;
}

extern "C" __declspec(dllexport) void *__attribute__((thiscall))
List_NextValue(LinkedList * /*self*/, LinkedListNode **it)
{
    LinkedListNode *n = *it;
    *it = n->pNextNode;
    return n->pValue;
}
