/* TextEntry: the one-line text entry used for cheat codes and for save and
 * high-score names.  Two live in the Game.  The owner points buffer at its own
 * storage before activating the entry; construction leaves it alone. */
#pragma once

 

class TextEntry {
public:
     

    // Polls the keyboard once.  phase drives the cursor blink.
    void poll(unsigned int phase);

    char          *buffer() const                  { return buffer_; }
    void           setBuffer(char *b)              { buffer_ = b; }
    // The last key accepted, for debounce.  When entry ends it holds the key
    // that ended it: 0x0d Return (accept) or 0x1b Escape.
    unsigned char  lastKey() const                 { return lastKey_; }
    void           setLastKey(unsigned char k)     { lastKey_ = k; }
    unsigned char  cursor() const                  { return cursor_; }
    void           setCursor(unsigned char c)      { cursor_ = c; }
    unsigned char  maxLength() const               { return maxLength_; }
    void           setMaxLength(unsigned char n)   { maxLength_ = n; }
    // Non-zero while the entry takes keys; Return and Escape clear it.
    int            active() const                  { return active_; }
    void           setActive(int a)                { active_ = a; }

    // Called only by the Game's construction and destruction.
    void construct();
    void destruct();

    /* The one slot of TextEntry's vtable. */
    static TextEntry * 
    scalarDeletingDtor(TextEntry *self, unsigned char flags);

private:
    TextEntry() = delete;  // only ever reached through the Game
     

    const void    *vtable_;  // our one-slot table
    char          *buffer_;  // the owner's storage
    unsigned char  lastKey_;
    unsigned char  cursor_;
    unsigned char  maxLength_;  // characters, not counting the terminator
    int            active_;
};

 