/* The platform video layer: intro-movie playback behind an opaque, window
 * system free header, so the game's start-up logic (src/app) never sees how a
 * movie is decoded or drawn.  A port replaces the .cpp beside this file.
 *
 * Today's implementation plays nothing: a movie "loads", and "plays" for no
 * time, ending when the window gets its end-of-movie message. */
#pragma once

namespace videodev {

/* Where the layer reports problems.  printf-style; may stay unset. */
typedef void (*LogFn)(const char *fmt, ...);
void setLog(LogFn fn);

class Player {
public:
    Player();
    ~Player();
    Player(const Player &) = delete;
    Player &operator=(const Player &) = delete;

    /* Opens the movie at path for the native window; its end-of-movie message
     * is sent there.  False if it cannot be played, after which playing() is
     * false. */
    bool load(void *window, const char *path);

    /* True from a successful load until the movie ends or is skipped. */
    bool playing() const { return playing_; }

    /* Starts or resumes (also after the window regains focus), and pauses. */
    void play();
    void pause();

    /* Gives up on the movie early. */
    void skip();

    /* Offered every window message; true if it was the player's. */
    bool handleWindowMessage(unsigned msg, unsigned long wParam, long lParam);

private:
    void *window_;
    bool  playing_;
};

}  // namespace videodev
