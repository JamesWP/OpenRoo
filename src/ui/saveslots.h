/* SaveSlots -- the save-slot table embedded in Game at +0x170a7c
 * (COHESION_PLAN.md Band 3).  Each slot is one 42-byte SaveSlot record,
 * the same bytes SavedGames\jjN.sav holds (enciphered with key 0x37);
 * tools/karoosave.py decodes every field and names the Game field and
 * routine behind each.
 *
 * 0x2a07 bytes: 255 records end exactly at Game+0x173483 (levelName_).
 * The ctor 0x43b390 and dtor 0x43b3c0 only store the vtable 0x45d6f4,
 * whose one slot is the deleting dtor 0x43b3a0.  All three are ours as of
 * ENDGAME_PLAN E2, and the table is ours too: the byte scan for the literal
 * 0x0045d6f4 finds exactly two occurrences, 0x43b394 and 0x43b3c2, i.e. the
 * ctor and the dtor themselves, so nothing else installs it.  Game's
 * Load/Destruct still drive them, through the three exports below.
 */
#pragma once

#include "layout.h"

struct __attribute__((packed)) SaveSlot {
    static const int ORIGIN = 0;

    char          name[20];             /* +0x00  menu text; ".........." empty */
    unsigned char levelIndex;           /* +0x14  the level to resume at */
    unsigned char livesRemaining;       /* +0x15 */
    unsigned int  totalScore;           /* +0x16 */
    unsigned int  completionNumerator;  /* +0x1a  Player::completionNumerator */
    unsigned int  elapsedGameTime;      /* +0x1e  (int)Game::totalPlayTime */
    unsigned int  inUse;                /* +0x22  1 = loadable */
    unsigned int  unusedTail;           /* +0x26  never read or written */

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
    /* By a BYTE index: the load menu forms it as (node + 0x38), which wraps
     * node 200 to slot 0 -- do not widen it. */
    SaveSlot      *slot(unsigned char i)           { return &slots_[i]; }
    /* The record the save-name entry edits, and which slot it goes back
     * to when RETURN accepts it. */
    SaveSlot      *edit()                          { return &edit_; }
    unsigned short editSlot() const                { return editSlot_; }
    void           setEditSlot(unsigned short i)   { editSlot_ = i; }
    /* 0x0043b560 -- blank the first count() records (saveslots.cpp). */
    void           initialiseEmpty();

private:
    SaveSlots() = delete;   /* game-owned; only ever reached by pointer */
    KAROO_LAYOUT_REGISTER(SaveSlots);

    const void    *vtable_;                 /* +0x00  our one-slot table */
    unsigned short editSlot_;               /* +0x04 */
    SaveSlot       edit_;                   /* +0x06 */
    unsigned char  count_;                  /* +0x30 */
    SaveSlot       slots_[MAX_SLOTS];       /* +0x31 */
};

KAROO_LAYOUT_CHECKS(SaveSlots)
{
    KAROO_LAYOUT_AT(editSlot_, 0x04);
    KAROO_LAYOUT_AT(edit_,     0x06);
    KAROO_LAYOUT_AT(count_,    0x30);
    KAROO_LAYOUT_AT(slots_,    0x31);
    KAROO_LAYOUT_SIZE(0x2a07);
}

/* The two file routines (saveslots.cpp) patch.py binds by name. */
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Save_LoadAllSlotFiles(SaveSlots *self, const char *name, char key);
extern "C" __declspec(dllexport) int __attribute__((thiscall))
Save_WriteAllSlotFiles(SaveSlots *self, const char *name, char key);

/* ─── The three the game calls, and the table they install ───────────────
 *
 * 0x43b390 ctor and 0x43b3c0 dtor body are byte-identical -- both are the
 * single store `MOV [ECX],0x45d6f4; RET` -- and both are kept, because the
 * two CALL sites (0x414664 in Game::Load, 0x414d5e in Game's teardown) and
 * the two SEH unwind funclets at 0x45bdb6 / 0x45be86 name them separately.
 * Collapsing them into one export would make the stubs ambiguous.
 */
extern "C" __declspec(dllexport) void *SaveSlots_Vtable(void);

extern "C" __declspec(dllexport) void __attribute__((thiscall))
SaveSlots_InstallVtable(SaveSlots *self);       /* 0x0043b390 */

extern "C" __declspec(dllexport) void __attribute__((thiscall))
SaveSlots_RestoreVtable(SaveSlots *self);       /* 0x0043b3c0 */

/* 0x0043b3a0 -- vtable slot 0.  Returns `this`; bit 0 of `flags` frees on
 * the GAME heap, since Game owns this object. */
extern "C" __declspec(dllexport) void *__attribute__((thiscall))
SaveSlots_ScalarDtor(SaveSlots *self, unsigned int flags);

/* 0x0043b560 InitialiseEmptySaveSlotTable -- one E8, Game::Load 0x4148c8. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
SaveSlots_InitialiseEmpty(SaveSlots *self);
