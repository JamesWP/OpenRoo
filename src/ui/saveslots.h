/* SaveSlots: the save-slot table, a sub-object of Game.  Each slot is one
 * 42-byte SaveSlot record, the bytes SavedGames\<name><N>.sav holds
 * (saveslots.cpp); tools/karoosave.py decodes the same format. */
#pragma once

enum { SAVE_SLOT_BYTES = 42 };

/* One slot.  FORMAT: its .sav file holds these fields in this order,
 * little-endian and unpadded, SAVE_SLOT_BYTES in all (saveslots.cpp). */
struct SaveSlot {

    char          name[20]{};    // the menu text; ".........." when empty
    unsigned char levelIndex{};  // the level to resume at
    unsigned char livesRemaining{};
    unsigned int  totalScore{};
    unsigned int  completionNumerator{};  // the player's completion numerator
    unsigned int  elapsedGameTime{};      // play time, whole seconds
    unsigned int  inUse{};                // 1 = loadable
    unsigned int  unusedTail{};           // never read or written

    /* The record to and from its file bytes. */
    void encode(unsigned char out[SAVE_SLOT_BYTES]) const;
    void decode(const unsigned char in[SAVE_SLOT_BYTES]);
};


class SaveSlots {
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

    SaveSlots();
    virtual ~SaveSlots();
    SaveSlots(const SaveSlots &) = delete;
    SaveSlots &operator=(const SaveSlots &) = delete;

    /* Loads and saves every slot's file, enciphered with key.  See
     * saveslots.cpp. */
    int loadAllSlotFiles(const char *dir, const char *name, char key);

    int writeAllSlotFiles(const char *dir, const char *name, char key);

private:
     

    unsigned short editSlot_{};
    SaveSlot       edit_;
    unsigned char  count_{};
    SaveSlot       slots_[MAX_SLOTS];
};

 
