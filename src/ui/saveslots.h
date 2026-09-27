/* SaveSlots: the save-slot table, a sub-object of Game.  Each slot is one
 * 42-byte SaveSlot record, the bytes SavedGames\<name><N>.sav holds
 * (saveslots.cpp); tools/karoosave.py decodes the same format. */
#pragma once

/* FORMAT: one slot, as stored in its .sav file. */
struct __attribute__((packed)) SaveSlot {

    char          name[20];    // the menu text; ".........." when empty
    unsigned char levelIndex;  // the level to resume at
    unsigned char livesRemaining;
    unsigned int  totalScore;
    unsigned int  completionNumerator;  // the player's completion numerator
    unsigned int  elapsedGameTime;      // play time, whole seconds
    unsigned int  inUse;                // 1 = loadable
    unsigned int  unusedTail;           // never read or written

};

class __attribute__((packed)) SaveSlots {
public:
    static const int MAX_SLOTS = 255;

    unsigned char  count() const                   { return count_; }
    void           setCount(unsigned char n)       { count_ = n; }
    // By a byte index: the load menu forms it as node + 0x38, which wraps node
    // 200 to slot 0.  Do not widen it.
    SaveSlot      *slot(unsigned char i)           { return &slots_[i]; }
    // The record the save-name entry edits, and the slot it goes back to when
    // Return accepts it.
    SaveSlot      *edit()                          { return &edit_; }
    unsigned short editSlot() const                { return editSlot_; }
    void           setEditSlot(unsigned short i)   { editSlot_ = i; }
    // Blanks the first count() records.
    void           initialiseEmpty();

private:
    SaveSlots() = delete;  // only ever reached through the Game

    const void    *vtable_;  // our one-slot table
    unsigned short editSlot_;
    SaveSlot       edit_;
    unsigned char  count_;
    SaveSlot       slots_[MAX_SLOTS];
};

/* Loads and saves every slot's file, enciphered with key.  See saveslots.cpp.
 */
int Save_LoadAllSlotFiles(SaveSlots *self, const char *name, char key);
int Save_WriteAllSlotFiles(SaveSlots *self, const char *name, char key);

/* The constructor and destructor body both only install the vtable. */
void *SaveSlots_Vtable(void);

void SaveSlots_InstallVtable(SaveSlots *self);

void SaveSlots_RestoreVtable(SaveSlots *self);

/* The one slot of the vtable: the deleting destructor. */
void *SaveSlots_ScalarDtor(SaveSlots *self, unsigned int flags);

/* Called by the Game's construction. */
void SaveSlots_InitialiseEmpty(SaveSlots *self);
