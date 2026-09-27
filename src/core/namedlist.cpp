/* NamedEntryList (namedlist.h).
 *
 * PRESERVED, all harmless as the game uses the list:
 *   - Insert stores through the new entry without checking it, so an
 *     exhausted heap faults inside Insert;
 *   - Insert rejects a name longer than 0x100, so a name of exactly 0x100 is
 *     accepted and copied with its terminator, 0x101 bytes into a 0x100-byte
 *     field.  The stray NUL lands on the payload, which the next store
 *     overwrites;
 *   - Remove repairs head and tail from the entry's own links and never checks
 *     that the entry is in this list: a foreign entry corrupts both lists;
 *   - Remove always returns 0;
 *   - DtorBody re-installs the vtable before it clears. */

#include "namedlist.h"
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <windows.h>
#include "log.h"

/* KAROO_NAMEDLIST_FX, a negative control (CONTROLS.md):
 *   nofind  Find always returns NULL.
 * Finding by name is the one thing this class decides, so breaking it changes
 * an outcome rather than a value.  Every acquire misses, so the SoundManager
 * loads a fresh buffer and inserts a fresh entry instead of sharing one; every
 * release misses, so entries leak until the level's purge, which walks the
 * list rather than searching it.  It moves no coordinate or tile index, so it
 * cannot reach the unbounded spawn scans that crash levelreport.py; it could
 * in principle run out of DirectSound buffers. */
enum NamedListFx { NL_FX_OFF = 0, NL_FX_NOFIND = 1 };

static NamedListFx namedlist_fx(void)
{
    static int cached = -1;
    if (cached >= 0)
        return (NamedListFx)cached;
    char buf[32];
    DWORD n = GetEnvironmentVariableA("KAROO_NAMEDLIST_FX", buf, sizeof(buf));
    NamedListFx fx = NL_FX_OFF;
    if (n > 0 && n < sizeof(buf)) {
        if (lstrcmpiA(buf, "nofind") == 0) fx = NL_FX_NOFIND;
    }
    log_write("namedlist: FX mode = %s\n", fx == NL_FX_NOFIND ? "nofind" : "off");
    cached = (int)fx;
    return fx;
}

/* KAROO_NAMEDLIST_DIAG, a census: tells "the suite never calls Remove" from
 * "the suite calls Remove and cannot see it", and reports what the control
 * should move.  maxLen is the longest list reached: with sharing it stays at
 * the number of distinct sounds a level loads; under nofind it climbs past it.
 */
static bool namedlist_diag(void)
{
    static int cached = -1;
    if (cached < 0) {
        char buf[32];
        DWORD n = GetEnvironmentVariableA("KAROO_NAMEDLIST_DIAG", buf, sizeof(buf));
        cached = (n > 0 && n < sizeof(buf) && lstrcmpiA(buf, "0") != 0) ? 1 : 0;
    }
    return cached != 0;
}

static unsigned long g_nConstruct, g_nScalarDtor, g_nDtorBody, g_nInsert;
static unsigned long g_nInsertReject, g_nClear, g_nClearFreed, g_nRemove;
static unsigned long g_nRemoveNull, g_nFind, g_nFindHit, g_nMaxLen;

/* Each function logs its own first call, since a sample taken at thresholds
 * reads zero for anything first reached after the last one. */
static void namedlist_first(const char *fn, unsigned long *pSeen)
{
    if (!namedlist_diag() || *pSeen != 0)
        return;
    *pSeen = 1;
    log_write("namedlist: first call to %s\n", fn);
}

/* The tally is logged from Clear, the cold path, rather than at Insert
 * thresholds: a level loads only a few dozen sounds, so a threshold count
 * would stop at its first line. */
static void namedlist_census(void)
{
    if (!namedlist_diag())
        return;
    log_write("namedlist: census construct=%lu sdtor=%lu dtorbody=%lu "
              "insert=%lu (rejected %lu) clear=%lu (freed %lu) remove=%lu "
              "(null %lu) find=%lu (hit %lu) maxLen=%lu\n",
              g_nConstruct, g_nScalarDtor, g_nDtorBody, g_nInsert,
              g_nInsertReject, g_nClear, g_nClearFreed, g_nRemove,
              g_nRemoveNull, g_nFind, g_nFindHit, g_nMaxLen);
}

extern "C" {

/* The vtable below needs its address. */
__declspec(dllexport) NamedEntryList *__attribute__((thiscall))
NamedList_ScalarDtor(NamedEntryList *self, unsigned char bFreeSelf);

/* The one-slot vtable. */
static void *const g_NamedListVtable[1] = { (void *)&NamedList_ScalarDtor };

__declspec(dllexport) void __attribute__((thiscall))
NamedList_Construct(NamedEntryList *self)
{
    ++g_nConstruct;
    { static unsigned long seen; namedlist_first("Construct", &seen); }
    self->vtable  = (void **)g_NamedListVtable;
    self->pHead   = NULL;
    self->pTail   = NULL;
    self->dwCount = 0;
}

__declspec(dllexport) void __attribute__((thiscall))
NamedList_Clear(NamedEntryList *self)
{
    ++g_nClear;
    { static unsigned long seen; namedlist_first("Clear", &seen); }
    NamedEntry *p = self->pHead;
    while (p != NULL) {
        NamedEntry *next = p->pNext;
        ++g_nClearFreed;
        free(p);
        p = next;
    }
    self->pHead   = NULL;
    self->pTail   = NULL;
    self->dwCount = 0;
    // After the frees, so the tally is the end state.  Clear runs once per
    // list teardown and once per level purge, and DtorBody reaches it too.
    namedlist_census();
}

__declspec(dllexport) void __attribute__((thiscall))
NamedList_DtorBody(NamedEntryList *self)
{
    ++g_nDtorBody;
    { static unsigned long seen; namedlist_first("DtorBody", &seen); }
    self->vtable = (void **)g_NamedListVtable;
    NamedList_Clear(self);
}

__declspec(dllexport) NamedEntryList *__attribute__((thiscall))
NamedList_ScalarDtor(NamedEntryList *self, unsigned char bFreeSelf)
{
    ++g_nScalarDtor;
    { static unsigned long seen; namedlist_first("ScalarDtor", &seen); }
    NamedList_DtorBody(self);
    if ((bFreeSelf & 1) != 0)
        // No list is allocated on its own today; free() matches the malloc the
        // rest of the code uses.
        free(self);
    return self;
}

__declspec(dllexport) NamedEntry *__attribute__((thiscall))
NamedList_Insert(NamedEntryList *self, const char *pszName, void *pPayload)
{
    ++g_nInsert;
    { static unsigned long seen; namedlist_first("Insert", &seen); }
    // Also sampled here, on the hot path: under nofind the lists grow until
    // the run dies before the next Clear, just when the cold-path tally cannot
    // report.
    if (namedlist_diag()) {
        unsigned long n = g_nInsert;
        if (n == 1 || n == 100 || n == 1000 || n == 10000 || n % 20000 == 0)
            namedlist_census();
    }

    size_t len = strlen(pszName);
    if (len > 0x100) {  // greater than, not at least: see the file comment
        ++g_nInsertReject;
        return NULL;
    }

    // No NULL check, and the copy is len+1, both PRESERVED.
    NamedEntry *entry = (NamedEntry *)malloc(sizeof(NamedEntry));
    memcpy(entry->szName, pszName, len + 1);
    entry->pPayload = pPayload;
    entry->pNext    = NULL;
    entry->pPrev    = NULL;

    if (self->pTail != NULL) {
        self->pTail->pNext = entry;
        entry->pPrev       = self->pTail;
        self->pTail        = entry;
    } else {
        self->pHead = entry;
        self->pTail = entry;
    }
    self->dwCount = self->dwCount + 1;
    if (self->dwCount > g_nMaxLen)
        g_nMaxLen = self->dwCount;
    return entry;
}

__declspec(dllexport) int __attribute__((thiscall))
NamedList_Remove(NamedEntryList *self, NamedEntry *pEntry)
{
    ++g_nRemove;
    { static unsigned long seen; namedlist_first("Remove", &seen); }
    if (pEntry == NULL)
        ++g_nRemoveNull;
    if (pEntry != NULL) {
        if (pEntry->pPrev == NULL)
            self->pHead = pEntry->pNext;
        else
            pEntry->pPrev->pNext = pEntry->pNext;

        if (pEntry->pNext == NULL)
            self->pTail = pEntry->pPrev;
        else
            pEntry->pNext->pPrev = pEntry->pPrev;

        free(pEntry);
        self->dwCount = self->dwCount - 1;
    }
    return 0;
}

__declspec(dllexport) NamedEntry *__attribute__((thiscall))
NamedList_Find(NamedEntryList *self, const char *pszName)
{
    ++g_nFind;
    { static unsigned long seen; namedlist_first("Find", &seen); }

    if (namedlist_fx() == NL_FX_NOFIND)
        return NULL;  // the nofind control

    for (NamedEntry *p = self->pHead; p != NULL; p = p->pNext) {
        if (strcmp(p->szName, pszName) == 0) {
            ++g_nFindHit;
            return p;
        }
    }
    return NULL;
}

}
