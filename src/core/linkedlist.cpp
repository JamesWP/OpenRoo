/* LinkedList (linkedlist.h).
 *
 * PRESERVED, all harmless as the game uses the list:
 *   - Append stores through the new node without checking it, so an
 *     exhausted heap faults inside Append;
 *   - Unlink repairs head and tail from the node's own links and never checks
 *     that the node is in this list: a foreign node corrupts both lists;
 *   - Unlink always returns 0, and no caller reads it. */

#include <windows.h>
#include <stdint.h>
#include "linkedlist.h"
#include "sysdev.h"
#include <stddef.h>
#include "logger.h"

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
        uint32_t n = sysdev::getEnv("KAROO_LIST_DIAG", buf, sizeof(buf));
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
    g_logger.write("linkedlist: first call to %s\n", fn);
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
    g_logger.write("linkedlist: census init=%lu sdtor=%lu destruct=%lu append=%lu "
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
    uint32_t n = sysdev::getEnv("KAROO_LIST_FX", buf, sizeof(buf));
    ListFx fx = LIST_FX_OFF;
    if (n > 0 && n < sizeof(buf)) {
        if (lstrcmpiA(buf, "lifo") == 0) fx = LIST_FX_LIFO;
    }
    g_logger.write("linkedlist: FX mode = %s\n", fx == LIST_FX_LIFO ? "lifo" : "off");
    cached = (int)fx;
    return fx;
}

LinkedList::LinkedList()
{
    ++g_nInit;
    { static unsigned long seen; list_first("Init", &seen); }
    pHead   = NULL;
    pTail   = NULL;
    dwCount = 0;
}

void LinkedList::clear()
{
    ++g_nClear;
    { static unsigned long seen; list_first("Clear", &seen); }
    LinkedListNode *p = pHead;
    while (p != NULL) {
        LinkedListNode *next = p->pNextNode;
        delete p;
        p = next;
    }
    pHead   = NULL;
    pTail   = NULL;
    dwCount = 0;
}

LinkedList::~LinkedList()
{
    ++g_nDestruct;
    { static unsigned long seen; list_first("Destruct", &seen); }
    clear();
}

void LinkedList::append(void *pValue)
{
    ++g_nAppend;
    { static unsigned long seen; list_first("Append", &seen); }
    list_census();

    // Plain new: an exhausted heap throws std::bad_alloc, which nothing
    // catches, where the game faulted on the NULL.  Either way it ends.
    LinkedListNode *node = new LinkedListNode;
    node->pValue    = pValue;
    node->pNextNode = NULL;
    node->pPrevNode = NULL;

    if (list_fx() == LIST_FX_LIFO && pHead != NULL) {
        // The lifo control: link at the head.
        node->pNextNode        = pHead;
        pHead->pPrevNode = node;
        pHead            = node;
        dwCount          = dwCount + 1;
        return;
    }

    if (pTail != NULL) {
        pTail->pNextNode = node;
        node->pPrevNode        = pTail;
        pTail            = node;
    } else {
        pHead = node;
        pTail = node;
    }
    dwCount = dwCount + 1;
}

int LinkedList::unlink(LinkedListNode *pNode)
{
    ++g_nUnlink;
    { static unsigned long seen; list_first("Unlink", &seen); }
    if (pNode == NULL)
        ++g_nUnlinkNull;
    if (pNode != NULL) {
        if (pNode->pPrevNode == NULL)
            pHead = pNode->pNextNode;
        else
            pNode->pPrevNode->pNextNode = pNode->pNextNode;

        if (pNode->pNextNode == NULL)
            pTail = pNode->pPrevNode;
        else
            pNode->pNextNode->pPrevNode = pNode->pPrevNode;

        delete pNode;
        dwCount = dwCount - 1;
    }
    return 0;
}

LinkedListNode *LinkedList::find(void *pValue, LinkedListNode *pAfterNode)
{
    LinkedListNode *p = (pAfterNode != NULL) ? pAfterNode->pNextNode
                                             : pHead;
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

