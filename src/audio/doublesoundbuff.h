/* A sound manager asset entry: one loaded sound file, held twice (a master
 * buffer and a spare with the other 2D/3D flags), and the lists of duplicates
 * and voice pools currently borrowed from it.  The sound manager
 * (soundmanager.h) hands these out and takes them back. */
#pragma once

 
#include "audiodev.h"
#include "linkedlist.h"

class VoicePool;

/* Allocated by the sound manager with a fixed size of 0x58. */
class doublesoundbuff {
public:
     

/* The sub-objects' addresses, behind accessors so the packed-member warning is
 * suppressed once; all four offsets are 4-aligned. */
 
 
    audiodev::Buffer *master() { return &masterBuf_; }
    audiodev::Buffer *spare()  { return &spareBuf_; }
    LinkedList         *clones() { return &cloneList_; }
    LinkedList         *pools()  { return &voicePoolList_; }
 

    /* Empty: both buffers blank, both lists empty, neither lent out. */
    doublesoundbuff();
    /* Clears (purging every borrower), then the lists and buffers go. */
    virtual ~doublesoundbuff();
    doublesoundbuff(const doublesoundbuff &) = delete;
    doublesoundbuff &operator=(const doublesoundbuff &) = delete;

    /* Purges both borrower lists, releases both buffers and clears both taken
     * flags. */
    void clear();

    /* Deletes every duplicate through its vtable, then empties the list. */
    static void __attribute__((stdcall))
    purgeCloneList(LinkedList *list);

    /* Wipes and frees every pool, then empties the list. */
    static void __attribute__((stdcall))
    purgeVoicePoolList(LinkedList *list);

    /* Gives back one static buffer: 1 if it belonged to this entry, else 0. */
    int releaseStatic(audiodev::Buffer *buf);

    /* Gives back one voice pool: 1 if it belonged to this entry, else 0. */
    int releasePool(VoicePool *pool);

    /* The number of duplicates and pools lent out. */
    int borrowerCount();

    /* True with no borrowers and neither buffer lent out. */
    int isFullyReleased();

    unsigned long masterTaken() const               { return dwMasterTaken_; }
    void          setMasterTaken(unsigned long t)  { dwMasterTaken_ = t; }

private:
    audiodev::Buffer masterBuf_;      // the file as first loaded
    audiodev::Buffer spareBuf_;       // the same file, the other flag set
    unsigned long      dwMasterTaken_;  // non-zero while the master itself is lent out
    unsigned long      dwSpareTaken_;   // the same for the spare
    LinkedList         cloneList_;      // audiodev::Buffer* duplicates lent out
    LinkedList         voicePoolList_;  // VoicePool* built from it
     
};
