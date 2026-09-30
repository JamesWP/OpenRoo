/* NamedEntryList: a doubly-linked list keyed by name, a 0x100-byte string,
 * each entry holding one opaque payload.  SoundManager keeps its two sound
 * lists in it (the payload is a doublesoundbuff asset entry), and
 * ProgableControl its five action tables.  It has a virtual
 * destructor. */

#pragma once

 

class NamedEntryList;

class NamedEntry {
public:
    const char *name() const    { return szName; }
    void       *payload() const { return pPayload; }
    NamedEntry *next() const    { return pNext; }
    NamedEntry *prev() const    { return pPrev; }

private:
    friend class NamedEntryList;

    char        szName[0x100];  // the key, inline
    void       *pPayload;
    NamedEntry *pNext;
    NamedEntry *pPrev;
};

 
class NamedEntryList {
public:
     

    /* An empty list.  The destructor frees the entries, not their payloads. */
    NamedEntryList();
    virtual ~NamedEntryList();
    NamedEntryList(const NamedEntryList &) = delete;
    NamedEntryList &operator=(const NamedEntryList &) = delete;
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
    NamedEntry   *pHead;    // +0x04
    NamedEntry   *pTail;    // +0x08
    unsigned long dwCount;  // +0x0c

     
};