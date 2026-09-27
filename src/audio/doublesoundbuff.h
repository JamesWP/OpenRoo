/* A sound manager asset entry: one loaded sound file, held twice (a master
 * buffer and a spare with the other 2D/3D flags), and the lists of duplicates
 * and voice pools currently borrowed from it.  The sound manager
 * (soundmanager.h) hands these out and takes them back. */
#pragma once

#include "static.h"
#include "linkedlist.h"

struct VoicePool;

/* Allocated by the sound manager with a fixed size of 0x58. */
struct __attribute__((packed)) doublesoundbuff {

    CStaticSoundbuffer masterBuf;      // the file as first loaded
    CStaticSoundbuffer spareBuf;       // the same file, the other flag set
    unsigned long      dwMasterTaken;  // non-zero while the master itself is lent out
    unsigned long      dwSpareTaken;   // the same for the spare
    LinkedList         cloneList;      // CStaticSoundbuffer* duplicates lent out
    LinkedList         voicePoolList;  // VoicePool* built from it

/* The sub-objects' addresses, behind accessors so the packed-member warning is
 * suppressed once; all four offsets are 4-aligned. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    CStaticSoundbuffer *master() { return &masterBuf; }
    CStaticSoundbuffer *spare()  { return &spareBuf; }
    LinkedList         *clones() { return &cloneList; }
    LinkedList         *pools()  { return &voicePoolList; }
#pragma GCC diagnostic pop

private:
};

/* The constructor.  Returns self. */
doublesoundbuff *Dsb_Init(doublesoundbuff *self);

/* The destructor body; does not free self. */
void Dsb_Destruct(doublesoundbuff *self);

/* Purges both borrower lists, releases both buffers and clears both taken
 * flags. */
void Dsb_Clear(doublesoundbuff *self);

/* Deletes every duplicate through its vtable, then empties the list. */
void __attribute__((stdcall))
Dsb_PurgeCloneList(LinkedList *list);

/* Wipes and frees every pool, then empties the list. */
void __attribute__((stdcall))
Dsb_PurgeVoicePoolList(LinkedList *list);

/* Gives back one static buffer: 1 if it belonged to this entry, else 0. */
int Dsb_ReleaseStatic(doublesoundbuff *self, CStaticSoundbuffer *buf);

/* Gives back one voice pool: 1 if it belonged to this entry, else 0. */
int Dsb_ReleasePool(doublesoundbuff *self, VoicePool *pool);

/* The number of duplicates and pools lent out. */
int Dsb_BorrowerCount(doublesoundbuff *self);

/* True with no borrowers and neither buffer lent out. */
int Dsb_IsFullyReleased(doublesoundbuff *self);

