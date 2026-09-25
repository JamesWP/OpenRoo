/* TextEntry -- the game's one-line text-entry widget (COHESION_PLAN.md
 * Band 3).  Two live in Game: the cheat-code entry at +0x13cdac and the
 * save-name / high-score-name entry at +0x170a6d.
 *
 * 0xf bytes, and both instances end exactly where the next object starts
 * (+0x13cdbb the high-score table, +0x170a7c the save slots).  The ctor
 * 0x420950 stores the vtable 0x45d444 and zeroes lastKey, cursor,
 * maxLength and active -- NOT buffer, which the owner points at its own
 * storage before activating the entry.  The dtor 0x420990 only restores
 * the vtable.  Both are still the game's, run by Game's Load/Destruct.
 * The vtable has one slot, 0x420970 (the next dword is 0).
 */
#pragma once

#include "layout.h"

class __attribute__((packed)) TextEntry {
public:
    static const int ORIGIN = 0;

    /* Poll the keyboard once (PollTextEntryKeys 0x4209a0, textentry.cpp):
     * `phase` drives the cursor blink. */
    void poll(unsigned int phase);

    char          *buffer() const                  { return buffer_; }
    void           setBuffer(char *b)              { buffer_ = b; }
    /* The last key accepted, kept for debounce.  After entry ends it holds
     * the key that ended it: 0x0d RETURN (accept) or 0x1b ESCAPE. */
    unsigned char  lastKey() const                 { return lastKey_; }
    void           setLastKey(unsigned char k)     { lastKey_ = k; }
    unsigned char  cursor() const                  { return cursor_; }
    void           setCursor(unsigned char c)      { cursor_ = c; }
    unsigned char  maxLength() const               { return maxLength_; }
    void           setMaxLength(unsigned char n)   { maxLength_ = n; }
    /* Nonzero while the entry takes keys; RETURN and ESCAPE clear it. */
    int            active() const                  { return active_; }
    void           setActive(int a)                { active_ = a; }


    /* Game-embedded lifecycle, called only by Game_Construct / Game_Destruct
     * (gamelife.cpp).  The vtable installed is ours (one slot, the scalar
     * dtor below); the game's is left as a tripwire. */
    void construct();   /* 0x420950 */
    void destruct();    /* 0x420990 */

private:
    TextEntry() = delete;   /* game-owned; only ever reached by pointer */
    KAROO_LAYOUT_REGISTER(TextEntry);

    const void    *vtable_;       /* +0x00  0x45d444 */
    char          *buffer_;       /* +0x04 */
    unsigned char  lastKey_;      /* +0x08 */
    unsigned char  cursor_;       /* +0x09 */
    unsigned char  maxLength_;    /* +0x0a */
    int            active_;       /* +0x0b */
};

KAROO_LAYOUT_CHECKS(TextEntry)
{
    KAROO_LAYOUT_AT(buffer_,    0x04);
    KAROO_LAYOUT_AT(lastKey_,   0x08);
    KAROO_LAYOUT_AT(cursor_,    0x09);
    KAROO_LAYOUT_AT(maxLength_, 0x0a);
    KAROO_LAYOUT_AT(active_,    0x0b);
    KAROO_LAYOUT_SIZE(0x0f);
}

/* The export patch.py binds; a shim onto TextEntry::poll. */
extern "C" __declspec(dllexport) void __attribute__((thiscall))
Sim_PollTextEntryKeys(TextEntry *self, unsigned int phase);

/* 0x420970, slot 0 of our TextEntry table. */
extern "C" __declspec(dllexport) TextEntry *__attribute__((thiscall))
TextEntry_ScalarDestructor(TextEntry *self, unsigned char flags);
