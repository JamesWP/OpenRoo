/* TextEntry: the one-line text entry used for cheat codes and for save and
 * high-score names.  Two live in the Game.  The owner points buffer at its own
 * storage before activating the entry; construction leaves it alone. */
#pragma once

 

class TextEntry {
public:
     

    // Takes the keys pressed this tick.  phase drives the cursor blink.
    void poll(unsigned int phase);

    char          *buffer() const                  { return buffer_; }
    void           setBuffer(char *b)              { buffer_ = b; }
    // The key that ended the entry: Return (accept) or Escape.
    unsigned char  endKey() const                  { return endKey_; }
    unsigned char  cursor() const                  { return cursor_; }
    void           setCursor(unsigned char c)      { cursor_ = c; }
    unsigned char  maxLength() const               { return maxLength_; }
    void           setMaxLength(unsigned char n)   { maxLength_ = n; }
    // Non-zero while the entry takes keys; Return and Escape clear it.
    int            active() const                  { return active_; }
    void           setActive(int a)                { active_ = a; }

    TextEntry();
    virtual ~TextEntry();
    TextEntry(const TextEntry &) = delete;
    TextEntry &operator=(const TextEntry &) = delete;


private:
     

    char          *buffer_{};  // the owner's storage
    unsigned char  endKey_{};
    unsigned char  cursor_{};
    unsigned char  maxLength_{};  // characters, not counting the terminator
    int            active_{};
};

 