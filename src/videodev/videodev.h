/* The platform video layer: intro-movie playback behind an opaque, window
 * system free header, so the game's start-up logic (src/app) never sees how a
 * movie is decoded.  A port replaces the .cpp beside this file.
 *
 * The player decodes the movie (any format FFmpeg reads) and plays its sound;
 * the game draws each new frame itself, with whatever it renders with, so the
 * player never touches the display. */
#pragma once

namespace videodev {

/* Where the layer reports problems.  printf-style; may stay unset. */
typedef void (*LogFn)(const char *fmt, ...);
void setLog(LogFn fn);

struct PlayerState;

class Player {
public:
    Player();
    ~Player();
    Player(const Player &) = delete;
    Player &operator=(const Player &) = delete;

    /* Opens the movie file at path.  With show false (a headless or test
     * run) nothing is opened, decoded or heard, and the movie "plays" for no
     * time, ending at the window's next end-of-movie message.  False if it
     * cannot be played, after which playing() is false. */
    bool load(void *window, const char *path, bool show);

    /* True from a successful load until the movie ends or is skipped. */
    bool playing() const { return playing_; }

    /* Starts or resumes (also after the window regains focus), and pauses. */
    void play();
    void pause();

    /* Gives up on the movie early. */
    void skip();

    /* Moves to the frame due now.  True if it differs from the last one
     * frame() returned; the movie may have ended instead (playing() false). */
    bool update();

    /* The current frame: width * height pixels, top row first, four bytes
     * each (R, G, B, A); valid until the next update().  NULL before the
     * first. */
    const unsigned char *frame(int *width, int *height) const;

    /* Offered every window message; true if it was the player's. */
    bool handleWindowMessage(unsigned msg, unsigned long wParam, long lParam);

private:
    PlayerState *state_;
    bool   playing_;
};

}  // namespace videodev
