#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>
#include "audiodev.h"
#include "dsound_internal.h"

namespace audiodev {

/* Played through MCI waveaudio under the alias "km".  The window gets
 * MM_MCINOTIFY when a track ends. */
struct MusicState {
    HWND window;
    bool repeat;
    char path[MAX_PATH];  // the track playing, to restart it
};

Music::Music() : state_(new MusicState())
{
    state_->window = NULL;
    state_->repeat = false;
    state_->path[0] = '\0';
}

Music::~Music()
{
    stop();
    delete state_;
}

void Music::setWindow(void *window)
{
    state_->window = (HWND)window;
}

static void close_device()
{
    mciSendStringA("stop km", NULL, 0, NULL);
    mciSendStringA("close km", NULL, 0, NULL);
}

void Music::play(const char *path, bool repeat)
{
    close_device();

    state_->repeat = repeat;
    snprintf(state_->path, sizeof(state_->path), "%s", path);

    char cmd[MAX_PATH + 64];
    snprintf(cmd, sizeof(cmd), "open \"%s\" type waveaudio alias km", path);
    MCIERROR err = mciSendStringA(cmd, NULL, 0, NULL);
    if (err) {
        AD_LOG("audiodev: Music open failed err=%lu\n", err);
        return;
    }

    // Plays without blocking; the window gets MM_MCINOTIFY when it ends.
    mciSendStringA("play km notify", NULL, 0, state_->window);
}

void Music::stop()
{
    // Clear repeat so a pending notification does not restart it.
    state_->repeat = false;
    close_device();
}

bool Music::handleWindowMessage(unsigned msg, unsigned long wParam, long)
{
    if (msg != MM_MCINOTIFY)
        return false;
    if (wParam == MCI_NOTIFY_SUCCESSFUL && state_->repeat) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s", state_->path);
        play(path, true);
    }
    return true;
}

}  // namespace audiodev
