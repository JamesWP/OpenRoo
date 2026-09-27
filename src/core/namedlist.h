/* NamedEntryList: a doubly-linked list keyed by name, a 0x100-byte string,
 * each entry holding one opaque payload.  SoundManager keeps its two sound
 * lists in it (the payload is a doublesoundbuff asset entry), and
 * ProgableControl its five action tables.  A one-slot vtable, the scalar
 * deleting destructor. */

#pragma once

struct __attribute__((packed)) NamedEntry {

    char        szName[0x100];  // +0x000  the key, inline
    void       *pPayload;       // +0x100
    NamedEntry *pNext;          // +0x104
    NamedEntry *pPrev;          // +0x108

private:
};

struct __attribute__((packed)) NamedEntryList {

    void        **vtable;   // +0x00
    NamedEntry   *pHead;    // +0x04
    NamedEntry   *pTail;    // +0x08
    unsigned long dwCount;  // +0x0c

private:
};

/* 16 bytes: ProgableControl embeds five in an array of that stride.  The same
 * shape as LinkedList, but a separate class with its own vtable and entry
 * layout. */

/* Sets the vtable and zeroes the three fields. */
void NamedList_Construct(NamedEntryList *self);

/* DtorBody, then frees self when bit 0 of bFreeSelf is set.  Returns self. */
NamedEntryList *
NamedList_ScalarDtor(NamedEntryList *self, unsigned char bFreeSelf);

/* Re-installs the vtable, then Clear. */
void NamedList_DtorBody(NamedEntryList *self);

/* Makes an entry holding a copy of pszName and pPayload, and links it at the
 * tail.  Returns the entry, or NULL when the name is too long; no caller reads
 * it. */
NamedEntry *
NamedList_Insert(NamedEntryList *self, const char *pszName, void *pPayload);

/* Frees every entry, not their payloads: an owner of the payloads destroys
 * them first. */
void NamedList_Clear(NamedEntryList *self);

/* Unlinks and frees pEntry and decrements the count.  A NULL pEntry does
 * nothing.  Always returns 0. */
int NamedList_Remove(NamedEntryList *self, NamedEntry *pEntry);

/* The first entry whose name equals pszName (case-sensitive), or NULL. */
NamedEntry *NamedList_Find(NamedEntryList *self, const char *pszName);
