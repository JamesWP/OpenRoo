/* The movie player for a build without FFmpeg (-DOPENROO_VIDEO=OFF, the
 * rg35xx port): a movie to show fails to load, so the game goes straight
 * on; one not shown "plays" for no time, as in player.cpp. */
#include <stddef.h>
#include "videodev.h"

namespace videodev {

static LogFn g_log = NULL;
void setLog(LogFn fn) { g_log = fn; }

struct PlayerState {};

Player::Player() : state_(NULL), playing_(false) {}
Player::~Player() {}

bool Player::load(const char *path, bool show)
{
    if (!show) {
        playing_ = true;
        return true;
    }
    if (g_log)
        g_log("videodev: built without movie support; skipping '%s'\n", path ? path : "(null)");
    playing_ = false;
    return false;
}

void Player::play() { playing_ = false; }
void Player::pause() {}
void Player::skip() { playing_ = false; }
bool Player::update() { return false; }
const unsigned char *Player::frame(int *, int *) const { return NULL; }

}  // namespace videodev
