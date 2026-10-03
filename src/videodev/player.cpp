#include <SDL3/SDL.h>
#include "videodev.h"

namespace videodev {

static LogFn g_log = NULL;
void setLog(LogFn fn) { g_log = fn; }
#define VD_LOG(...) do { if (g_log) g_log(__VA_ARGS__); } while (0)

/* The SDL user event code the window passes on when the movie ends. */
static const int MOVIE_ENDED = 0x464;

Player::Player() : window_(NULL), playing_(false)
{
}

Player::~Player()
{
    VD_LOG("videodev: teardown\n");
}

bool Player::load(void *window, const char *path)
{
    VD_LOG("videodev: load(path=\"%s\")\n", path ? path : "(null)");
    window_  = window;
    playing_ = true;
    return true;
}

void Player::play()
{
    VD_LOG("videodev: play\n");
    SDL_Event e = {};
    e.type = SDL_EVENT_USER;
    e.user.code = MOVIE_ENDED;
    SDL_PushEvent(&e);
}

void Player::pause()
{
    VD_LOG("videodev: pause\n");
}

void Player::skip()
{
    VD_LOG("videodev: skip\n");
    playing_ = false;
}

bool Player::handleWindowMessage(unsigned msg, unsigned long, long)
{
    if (msg != (unsigned)MOVIE_ENDED)
        return false;
    playing_ = false;
    return true;
}

}  // namespace videodev
