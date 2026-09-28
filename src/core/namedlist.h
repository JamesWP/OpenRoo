/* NamedEntryList: a doubly-linked list keyed by name, a 0x100-byte string,
 * each entry holding one opaque payload.  SoundManager keeps its two sound
 * lists in it (the payload is a doublesoundbuff asset entry), and
 * ProgableControl its five action tables.  A one-slot vtable, the scalar
 * deleting destructor. */

#pragma once

#include "layout.h"

class NamedEntryList;

class __attribute__((packed)) NamedEntry {
public:
    static const int ORIGIN = 0;

    const char *name() const    { return szName; }
    void       *payload() const { return pPayload; }
    NamedEntry *next() const    { return pNext; }
    NamedEntry *prev() const    { return pPrev; }

private:
    friend class NamedEntryList;

    char        szName[0x100];  // +0x000  the key, inline
    void       *pPayload;       // +0x100
    NamedEntry *pNext;          // +0x104
    NamedEntry *pPrev;          // +0x108

    KAROO_LAYOUT_REGISTER(NamedEntry);
};

KAROO_LAYOUT_CHECKS(NamedEntry)
{
    KAROO_LAYOUT_AT(szName,   0x000);
    KAROO_LAYOUT_AT(pPayload, 0x100);
    KAROO_LAYOUT_AT(pNext,    0x104);
    KAROO_LAYOUT_AT(pPrev,    0x108);
    KAROO_LAYOUT_SIZE(0x10c);
}

class __attribute__((packed)) NamedEntryList {
public:
    static const int ORIGIN = 0;

    /* Sets the vtable and zeroes the three fields. */
    void construct();
    /* Vtable slot 0: dtorBody, then frees self when bit 0 of bFreeSelf is
     * set.  Returns self. */
    static NamedEntryList * 
    scalarDtor(NamedEntryList *self, unsigned char bFreeSelf);
    /* Re-installs the vtable, then clear(). */
    void dtorBody();
    /* Makes an entry holding a copy of pszName and pPayload, and links it at
     * the tail.  Returns the entry, or NULL when the name is too long; no
     * caller reads it. */
    NamedEntry *insert(const char *pszName, void *pPayload);
    /* Frees every entry, not their payloads: an owner of the payloads
     * destroys them first. */
    void clear();
    /* Unlinks and frees pEntry and decrements the count.  A NULL pEntry does
     * nothing.  Always returns 0. */
    int remove(NamedEntry *pEntry);
    /* The first entry whose name equals pszName (case-sensitive), or NULL. */
    NamedEntry *find(const char *pszName);

    NamedEntry   *head() const  { return pHead; }
    NamedEntry   *tail() const  { return pTail; }
    unsigned long count() const { return dwCount; }

private:
    void        **vtable;   // +0x00
    NamedEntry   *pHead;    // +0x04
    NamedEntry   *pTail;    // +0x08
    unsigned long dwCount;  // +0x0c

    KAROO_LAYOUT_REGISTER(NamedEntryList);
};

/* 16 bytes: ProgableControl embeds five in an array of that stride.  The same
 * shape as LinkedList, but a separate class with its own vtable and entry
 * layout. */
KAROO_LAYOUT_CHECKS(NamedEntryList)
{
    KAROO_LAYOUT_AT(vtable,  0x00);
    KAROO_LAYOUT_AT(pHead,   0x04);
    KAROO_LAYOUT_AT(pTail,   0x08);
    KAROO_LAYOUT_AT(dwCount, 0x0c);
    KAROO_LAYOUT_SIZE(16);
}
