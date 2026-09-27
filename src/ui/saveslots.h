/* SaveSlots: the save-slot table, a sub-object of Game.  Each slot is one
 * 42-byte SaveSlot record, the bytes SavedGames\<name><N>.sav holds
 * (saveslots.cpp); tools/karoosave.py decodes the same format. */
#pragma once

#include "layout.h"

/* FORMAT: one slot, as stored in its .sav file. */
struct __attribute__((packed)) SaveSlot {
    static const int ORIGIN = 0;

    char          name[20];    // the menu text; ".........." when empty
    unsigned char levelIndex;  // the level to resume at
    unsigned char livesRemaining;
    unsigned int  totalScore;
    unsigned int  completionNumerator;  // the player's completion numerator
    unsigned int  elapsedGameTime;      // play time, whole seconds
    unsigned int  inUse;                // 1 = loadable
    unsigned int  unusedTail;           // never read or written

    KAROO_LAYOUT_REGISTER(SaveSlot);
};

KAROO_LAYOUT_CHECKS(SaveSlot)
{
    KAROO_LAYOUT_AT(levelIndex,          0x14);
    KAROO_LAYOUT_AT(livesRemaining,      0x15);
    KAROO_LAYOUT_AT(totalScore,          0x16);
    KAROO_LAYOUT_AT(completionNumerator, 0x1a);
    KAROO_LAYOUT_AT(elapsedGameTime,     0x1e);
    KAROO_LAYOUT_AT(inUse,               0x22);
    KAROO_LAYOUT_AT(unusedTail,          0x26);
    KAROO_LAYOUT_SIZE(0x2a);
}

class __attribute__((packed)) SaveSlots {
public:
    static const int ORIGIN = 0;
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

    /* The one slot of the vtable: the deleting destructor. */
    static void *__attribute__((thiscall))
    scalarDeletingDtor(SaveSlots *self, unsigned int flags);

private:
    SaveSlots() = delete;  // only ever reached through the Game
    KAROO_LAYOUT_REGISTER(SaveSlots);

    const void    *vtable_;  // our one-slot table
    unsigned short editSlot_;
    SaveSlot       edit_;
    unsigned char  count_;
    SaveSlot       slots_[MAX_SLOTS];
};

KAROO_LAYOUT_CHECKS(SaveSlots)
{
    KAROO_LAYOUT_AT(editSlot_, 0x04);
    KAROO_LAYOUT_AT(edit_,     0x06);
    KAROO_LAYOUT_AT(count_,    0x30);
    KAROO_LAYOUT_AT(slots_,    0x31);
    KAROO_LAYOUT_SIZE(0x2a07);
}

/* Loads and saves every slot's file, enciphered with key.  See saveslots.cpp.
 */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Save_LoadAllSlotFiles(SaveSlots *self, const char *name, char key);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Save_WriteAllSlotFiles(SaveSlots *self, const char *name, char key);

/* The constructor and destructor body both only install the vtable. */
extern "C" __declspec(dllexport) void *SaveSlots_Vtable(void);

extern "C" __declspec(dllexport) void __attribute__((thiscall))
SaveSlots_InstallVtable(SaveSlots *self);

extern "C" __declspec(dllexport) void __attribute__((thiscall))
SaveSlots_RestoreVtable(SaveSlots *self);

